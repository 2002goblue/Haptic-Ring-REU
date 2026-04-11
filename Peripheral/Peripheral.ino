// BuzzPeripheral - Unified (BLE + Serial)
// 8 rapid clicks toggles mode and reboots.
// Mode is persisted to flash (separate from session data).

#include <ArduinoBLE.h>
#include <Wire.h>
#include "Adafruit_DRV2605.h"
#include <NanoBLEFlashPrefs.h>

// ── Pin Definitions ──────────────────────────────────────────────
#define BUTTON_PIN 1

const int RED_PIN   = LEDR;
const int GREEN_PIN = LEDG;
const int BLUE_PIN  = LEDB;

// ── BLE Service & Characteristics ────────────────────────────────
BLEService deviceService("19B10000-E8F2-537E-4F6C-D104768A1212");
BLEByteCharacteristic bleCTouched("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic blePTouched("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);

// ── Signal Values ────────────────────────────────────────────────
const byte SIG_NONE     = 0;
const byte SIG_PRESSING = 1;
const byte SIG_INITIATE = 2;
const byte SIG_REUP     = 3;
const byte SIG_CANCEL   = 4;

// ── Loop State Values ────────────────────────────────────────────
const int STATE_INITIATEE = 0;
const int STATE_INITIATOR = 1;
const int STATE_MAIN_LOOP = 2;
const int STATE_IDLE      = 3;

// ── Button Event Values ──────────────────────────────────────────
const int BTN_NONE        = 0;
const int BTN_SINGLE      = 1;
const int BTN_DOUBLE      = 2;
const int BTN_LONG_HOLD   = 3;
const int BTN_HOLD        = 4;

// ── Initiation Parameters ────────────────────────────────────────
const int INITIATE_STRENGTH    = 127;
const int INITIATE_BUZZ_LENGTH = 200;
const int INITIATE_CYCLE_LEN   = 1000;
const int INITIATE_MAX_CYCLES  = 30;

// ── Consent Timer ────────────────────────────────────────────────
const unsigned long CONSENT_DURATION = 30000;

// ── Connection Buzz ──────────────────────────────────────────────
int connectionBuzzOnLength  = 200;
int connectionBuzzOffLength = 100;

// ── Button Timing ────────────────────────────────────────────────
const int DEBOUNCE_MS    = 20;
const int DC_GAP_MS      = 250;
const int HOLD_TIME_MS   = 300;
const int LONG_HOLD_MS   = 2000;

// ── Button State ─────────────────────────────────────────────────
boolean buttonVal          = HIGH;
boolean buttonLast         = HIGH;
boolean DCwaiting          = false;
boolean DConUp             = false;
boolean singleOK           = true;
long    downTime           = -1;
long    upTime             = -1;
boolean ignoreUp           = false;
boolean waitForUp          = false;
boolean holdEventPast      = false;
boolean longHoldEventPast  = false;

// ── Reboot Detection ─────────────────────────────────────────────
int rebootClickCount           = 0;
unsigned long rebootWindowStart = 0;
const int REBOOT_CLICKS        = 8;
const unsigned long REBOOT_WINDOW = 2000;

// ── Global State ─────────────────────────────────────────────────
bool lowPowerMode = false;
bool connected    = false;
int  lastInitiator;

// ── Mode Storage ─────────────────────────────────────────────────
struct ModePrefs {
  uint8_t mode;  // 0 = BLE, 1 = Serial
};

NanoBLEFlashPrefs modeFlash;
ModePrefs modePrefs;
bool serialMode = false;

// ── Serial Communication (Serial mode only) ──────────────────────
byte lastCByte = SIG_NONE;
bool cUpdated  = false;
String serialInputBuffer = "";

Adafruit_DRV2605 drv;

// ── Data Storage ─────────────────────────────────────────────────
const int MAX_SESSIONS  = 20;
const int MAX_REUPS     = 25;
const int MAX_INIT_LOG  = 75;

struct SessionData {
  unsigned long beginTime;
  unsigned long endTime;
  unsigned long reUpTimes[MAX_REUPS];
  int8_t attemptedP[MAX_REUPS];
  int8_t attemptedC[MAX_REUPS];
  bool    initiator;
  int8_t  reUpCount;
  bool    endedByButton;
};

struct AllSessionData {
  SessionData sessions[MAX_SESSIONS];
  int8_t      numSessions;
  unsigned long pInitTimes[MAX_INIT_LOG];
  int8_t        numPInits;
  unsigned long cInitTimes[MAX_INIT_LOG];
  int8_t        numCInits;
};

// Separate flash instance for session data
NanoBLEFlashPrefs dataFlash;
AllSessionData allData;

// ─────────────────────────────────────────────────────────────────
// Connection Buzz Feedback
// ─────────────────────────────────────────────────────────────────
void connectionBuzz(bool lowPowerActivated = false) {
  int motorCycles;
  if (lowPowerActivated) {
    motorCycles = 3;
    connectionBuzzOnLength  = 100;
    connectionBuzzOffLength = 100;
    digitalWrite(BLUE_PIN, LOW);
  } else {
    motorCycles = 2;
    digitalWrite(connected ? GREEN_PIN : RED_PIN, LOW);
  }

  bool motorOn = true;
  unsigned long buzzTime = millis();
  drv.setRealtimeValue(127);

  while (motorCycles > 0) {
    if (motorOn && millis() > buzzTime + connectionBuzzOnLength) {
      drv.setRealtimeValue(0);
      buzzTime = millis();
      motorOn = false;
      motorCycles--;
    } else if (!motorOn && millis() > buzzTime + connectionBuzzOffLength) {
      drv.setRealtimeValue(127);
      buzzTime = millis();
      motorOn = true;
    }
  }

  drv.setRealtimeValue(0);
  allLEDsOff();
}

// ─────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  drv.begin();
  drv.setMode(DRV2605_MODE_REALTIME);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  allLEDsOff();

  // Read mode from flash
  if (modeFlash.readPrefs(&modePrefs, sizeof(modePrefs)) == FDS_SUCCESS) {
    serialMode = (modePrefs.mode == 1);
  } else {
    serialMode = false;  // Default to BLE
  }

  // Read session data (stored after mode prefs in flash)
  dataFlash.readPrefs(&allData, sizeof(allData));

  if (serialMode) {
    Serial.println("BuzzPeripheral Serial Ready");
    Serial.print("Current millis: ");
    Serial.println(millis());
    connected = true;
    connectionBuzz();
  } else {
    BLE.begin();
    BLE.setLocalName("Buzz On!");
    BLE.setAdvertisedService(deviceService);
    deviceService.addCharacteristic(bleCTouched);
    deviceService.addCharacteristic(blePTouched);
    bleCTouched.setValue(0);
    blePTouched.setValue(0);
    BLE.addService(deviceService);
    BLE.advertise();
    Serial.println("BuzzPeripheral BLE Ready");
  }
}

// ─────────────────────────────────────────────────────────────────
// LED Helpers
// ─────────────────────────────────────────────────────────────────
void allLEDsOff() {
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(BLUE_PIN, HIGH);
}

// ─────────────────────────────────────────────────────────────────
// Data Storage
// ─────────────────────────────────────────────────────────────────
void deleteSessionData() {
  dataFlash.deletePrefs();
  dataFlash.garbageCollection();
}

void saveSessionData() {
  deleteSessionData();
  dataFlash.writePrefs(&allData, sizeof(allData));
}

void printJSONData() {
  Serial.print("Current millis: ");
  Serial.println(millis());
  Serial.println("Printing JSON Data");

  Serial.print("{\"numPInits\":");
  Serial.print(allData.numPInits);
  Serial.print(",\"numCInits\":");
  Serial.print(allData.numCInits);
  Serial.print(",\"sessions\":[");

  for (int i = 0; i < allData.numSessions; i++) {
    if (i > 0) Serial.print(",");
    SessionData &s = allData.sessions[i];
    Serial.print("{\"id\":");
    Serial.print(i);
    Serial.print(",\"initiator\":\"");
    Serial.print(s.initiator ? "Peripheral" : "Control");
    Serial.print("\",\"endMethod\":\"");
    Serial.print(s.endedByButton ? "Button" : "Decay");
    Serial.print("\",\"begin\":");
    Serial.print(s.beginTime);
    Serial.print(",\"duration\":");
    Serial.print(s.endTime - s.beginTime);
    Serial.print(",\"numReUps\":");
    Serial.print(s.reUpCount);
    Serial.print("}");
  }

  Serial.println("]}");
}



// ─────────────────────────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────────────────────────
void loop() {
  if (serialMode) {
    loopSerial();
  } else {
    loopBLE();
  }
}

// =================================================================
//                      SERIAL MODE
// =================================================================

void serialCheckSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialInputBuffer.length() > 0) {
        if (serialInputBuffer.startsWith("C:")) {
          lastCByte = serialInputBuffer.substring(2).toInt();
          cUpdated = true;
        } else if (serialInputBuffer == "d") {
          Serial.println("Deleting all data");
          memset(&allData, 0, sizeof(allData));
          deleteSessionData();
          dataFlash.writePrefs(&allData, sizeof(allData));
          printJSONData();
        } else if (serialInputBuffer == "p") {
          printJSONData();
        }
        serialInputBuffer = "";
      }
    } else {
      serialInputBuffer += c;
    }
  }
}

void serialWritePTouched(byte value) {
  Serial.print("P:");
  Serial.println(value);
}

bool serialCTouchedUpdated(byte &cByte) {
  serialCheckSerial();
  if (cUpdated) {
    cByte = lastCByte;
    cUpdated = false;
    return true;
  }
  return false;
}

void loopSerial() {
  if (lowPowerMode) {
    while (checkButton() != BTN_LONG_HOLD) {}
    connectionBuzz(true);
    lowPowerMode = false;
    ignoreUp = true;
    holdEventPast = true;
    longHoldEventPast = true;
  }

  int loopControl = STATE_IDLE;
  byte cByte = SIG_NONE;
  byte pByte = SIG_NONE;

  waitForStart_S(loopControl, pByte, cByte);

  if (loopControl == STATE_INITIATOR) {
    initiator_S(loopControl, pByte, cByte);
    if (allData.numPInits < MAX_INIT_LOG) {
      allData.pInitTimes[allData.numPInits] = millis();
      allData.numPInits++;
    }
    lastInitiator = 1;
    saveSessionData();
    printJSONData();
  } else if (loopControl == STATE_INITIATEE) {
    initiatee_S(pByte, cByte, loopControl);
    if (allData.numCInits < MAX_INIT_LOG) {
      allData.cInitTimes[allData.numCInits] = millis();
      allData.numCInits++;
    }
    lastInitiator = 0;
    saveSessionData();
    printJSONData();
  }

  if (loopControl == STATE_MAIN_LOOP) {
    mainLoop_S(cByte, pByte);
    if (allData.numSessions <= MAX_SESSIONS) {
      int idx = allData.numSessions - 1;
      allData.sessions[idx].initiator = lastInitiator;
      saveSessionData();
      printJSONData();
    }
  }

  allLEDsOff();
}

void waitForStart_S(int &loopControl, byte &pByte, byte &cByte) {
  Serial.println("waitForStart");
  ignoreUp = true;
  holdEventPast = true;
  longHoldEventPast = true;

  unsigned long settleEnd = millis() + 200;
  while (millis() < settleEnd) {
    serialCTouchedUpdated(cByte);
    checkButton();
  }

  while (true) {
    if (serialCTouchedUpdated(cByte)) {
      Serial.print("cTouched updated: "); Serial.println(cByte);
      if (cByte == SIG_INITIATE) {
        cByte = SIG_NONE;
        loopControl = STATE_INITIATEE;
        break;
      }
    }

    int btn = checkButton();
    if (btn == BTN_LONG_HOLD) {
      Serial.println("Long hold — entering low power");
      holdEventPast = true;
      longHoldEventPast = true;
      lowPowerMode = true;
      connectionBuzz(true);
      return;
    }
    if (btn != BTN_NONE && btn != BTN_HOLD) {
      Serial.println("Button press — initiating");
      serialWritePTouched(SIG_INITIATE);
      pByte = SIG_NONE;
      loopControl = STATE_INITIATOR;
      break;
    }
  }
  Serial.println("Exiting waitForStart");
}

void initiator_S(int &loopControl, byte &pByte, byte &cByte) {
  Serial.println("Entering initiator");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (count < INITIATE_MAX_CYCLES) {
    if (checkButton() == BTN_DOUBLE) {
      Serial.println("Initiator double-click — cancelling");
      serialWritePTouched(SIG_CANCEL);
      break;
    }

    if (millis() < cycleStart + INITIATE_BUZZ_LENGTH) {
      drv.setRealtimeValue(INITIATE_STRENGTH);
      digitalWrite(RED_PIN, LOW);
    } else {
      drv.setRealtimeValue(0);
      digitalWrite(RED_PIN, HIGH);
    }

    if (millis() >= cycleStart + INITIATE_CYCLE_LEN) {
      cycleStart = millis();
      count++;
    }

    if (serialCTouchedUpdated(cByte)) {
      Serial.print("cTouched in initiator: "); Serial.println(cByte);
      if (cByte == SIG_INITIATE) {
        cByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
      } else if (cByte == SIG_CANCEL) {
        cByte = SIG_NONE;
        break;
      }
    }
  }

  Serial.println("Exiting initiator");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

void initiatee_S(byte &pByte, byte &cByte, int &loopControl) {
  Serial.println("Entering initiatee");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (loopControl == STATE_INITIATEE && count < INITIATE_MAX_CYCLES) {
    if (serialCTouchedUpdated(cByte)) {
      if (cByte == SIG_CANCEL) {
        cByte = SIG_NONE;
        break;
      }
    }

    if (millis() < cycleStart + INITIATE_BUZZ_LENGTH) {
      drv.setRealtimeValue(INITIATE_STRENGTH);
      digitalWrite(RED_PIN, LOW);
    } else {
      drv.setRealtimeValue(0);
      digitalWrite(RED_PIN, HIGH);
    }

    if (millis() >= cycleStart + INITIATE_CYCLE_LEN) {
      cycleStart = millis();
      count++;
    }

    switch (checkButton()) {
      case BTN_DOUBLE:
        Serial.println("Double-click — cancelling");
        serialWritePTouched(SIG_CANCEL);
        loopControl = STATE_INITIATOR;
        break;
      case BTN_NONE:
        break;
      default:
        Serial.println("Accepting initiation");
        serialWritePTouched(SIG_INITIATE);
        pByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
    }
  }

  Serial.println("Exiting initiatee");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

void mainLoop_S(byte &cByte, byte &pByte) {
  Serial.println("Entering mainLoop");

  int session = -1;
  if (allData.numSessions < MAX_SESSIONS) {
    session = allData.numSessions++;
    allData.sessions[session].beginTime = millis();
    allData.sessions[session].reUpCount = 0;
    allData.sessions[session].endedByButton = false;
    allData.sessions[session].attemptedP[0] = 0;
    allData.sessions[session].attemptedC[0] = 0;
  }

  digitalWrite(BLUE_PIN, LOW);
  digitalWrite(RED_PIN, HIGH);
  unsigned long consentTimer = millis();
  bool sessionActive = true;

  while (sessionActive) {
    long remaining = (long)(consentTimer + CONSENT_DURATION - millis());
    int buzzValue = map(constrain(remaining, 0, CONSENT_DURATION), 0, CONSENT_DURATION, 30, 127);
    drv.setRealtimeValue(buzzValue);

    if (serialCTouchedUpdated(cByte)) {
      Serial.print("cTouched in mainLoop: "); Serial.println(cByte);

      if (cByte == SIG_PRESSING && session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
        allData.sessions[session].attemptedC[allData.sessions[session].reUpCount]++;
      }

      if (cByte == SIG_PRESSING && pByte == SIG_PRESSING) {
        serialWritePTouched(SIG_REUP);
        pByte = SIG_REUP;
        resetConsentTimer(cByte, pByte, consentTimer);
        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].reUpTimes[allData.sessions[session].reUpCount] = millis();
          allData.sessions[session].reUpCount++;
          if (allData.sessions[session].reUpCount < MAX_REUPS) {
            allData.sessions[session].attemptedP[allData.sessions[session].reUpCount] = 0;
            allData.sessions[session].attemptedC[allData.sessions[session].reUpCount] = 0;
          }
        }
      }

      if (cByte == SIG_INITIATE) {
        if (session >= 0) allData.sessions[session].endedByButton = true;
        holdEventPast = true;
        longHoldEventPast = true;
        sessionActive = false;
      }

      if (cByte == SIG_REUP) {
        resetConsentTimer(cByte, pByte, consentTimer);
        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].reUpTimes[allData.sessions[session].reUpCount] = millis();
          allData.sessions[session].reUpCount++;
          if (allData.sessions[session].reUpCount < MAX_REUPS) {
            allData.sessions[session].attemptedP[allData.sessions[session].reUpCount] = 0;
            allData.sessions[session].attemptedC[allData.sessions[session].reUpCount] = 0;
          }
        }
      }
    }

    int btn = checkButton();
    switch (btn) {
      case BTN_DOUBLE:
        Serial.println("Double-click — ending session");
        holdEventPast = true;
        longHoldEventPast = true;
        serialWritePTouched(SIG_INITIATE);
        pByte = SIG_NONE;
        if (session >= 0) allData.sessions[session].endedByButton = true;
        sessionActive = false;
        break;
      case BTN_NONE:
        break;
      default:
        serialWritePTouched(SIG_PRESSING);
        pByte = SIG_PRESSING;
        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].attemptedP[allData.sessions[session].reUpCount]++;
        }
        break;
    }

    if (millis() > consentTimer + CONSENT_DURATION) {
      Serial.println("Consent timer expired");
      if (session >= 0) allData.sessions[session].endedByButton = false;
      holdEventPast = true;
      longHoldEventPast = true;
      sessionActive = false;
    }
  }

  Serial.println("Exiting mainLoop");
  drv.setRealtimeValue(0);
  if (session >= 0) allData.sessions[session].endTime = millis();
}

// =================================================================
//                        BLE MODE
// =================================================================

void handleBLESerialCommands() {
  while (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'd') {
      Serial.println("Deleting all data");
      memset(&allData, 0, sizeof(allData));
      deleteSessionData();
      dataFlash.writePrefs(&allData, sizeof(allData));
      printJSONData();
    } else if (cmd == 'p') {
      printJSONData();
    }
  }
}

void loopBLE() {
  Serial.println("-- Loop: waiting for connection --");

  handleBLESerialCommands();

  if (checkButton() == BTN_LONG_HOLD) {
    lowPowerMode = true;
    connectionBuzz(true);
    holdEventPast = true;
    longHoldEventPast = true;
  }

  if (lowPowerMode) {
    Serial.println("Low power mode active");
    BLE.stopAdvertise();
    while (checkButton() != BTN_LONG_HOLD) {}
    Serial.println("Waking from low power");
    connectionBuzz(true);
    lowPowerMode = false;
    BLE.advertise();
    ignoreUp = true;
    holdEventPast = true;
    longHoldEventPast = true;
  }

  BLEDevice central = BLE.central();

  if (central) {
    Serial.println("Connected to central");
    Serial.print("Current millis: ");
    Serial.println(millis());
    connected = true;
    connectionBuzz();

    while (central.connected() && !lowPowerMode) {
      handleBLESerialCommands();

      int loopControl = STATE_IDLE;
      byte cByte = SIG_NONE;
      byte pByte = SIG_NONE;

      waitForStart_B(central, loopControl, pByte, cByte);

      if (loopControl == STATE_INITIATOR) {
        initiator_B(central, loopControl, pByte, cByte);
        if (allData.numPInits < MAX_INIT_LOG) {
          allData.pInitTimes[allData.numPInits] = millis();
          allData.numPInits++;
        }
        lastInitiator = 1;
        saveSessionData();
        printJSONData();
      } else if (loopControl == STATE_INITIATEE) {
        initiatee_B(central, pByte, cByte, loopControl);
        if (allData.numCInits < MAX_INIT_LOG) {
          allData.cInitTimes[allData.numCInits] = millis();
          allData.numCInits++;
        }
        lastInitiator = 0;
        saveSessionData();
        printJSONData();
      }

      if (loopControl == STATE_MAIN_LOOP && central.connected()) {
        mainLoop_B(central, cByte, pByte);
        if (allData.numSessions <= MAX_SESSIONS) {
          int idx = allData.numSessions - 1;
          allData.sessions[idx].initiator = lastInitiator;
          saveSessionData();
          printJSONData();
        }
      }

      allLEDsOff();
    }

    Serial.println("Disconnected from central");
    connected = false;
    connectionBuzz(lowPowerMode);
  }
}

void waitForStart_B(BLEDevice central, int &loopControl, byte &pByte, byte &cByte) {
  Serial.println("waitForStart");
  ignoreUp = true;
  holdEventPast = true;
  longHoldEventPast = true;

  unsigned long settleEnd = millis() + 200;
  while (millis() < settleEnd && central.connected()) {
    if (bleCTouched.written()) {
      bleCTouched.readValue(cByte);
    }
    checkButton();
  }

  while (central.connected()) {
    if (bleCTouched.written()) {
      bleCTouched.readValue(cByte);
      Serial.print("cTouched updated: "); Serial.println(cByte);
      if (cByte == SIG_INITIATE) {
        cByte = SIG_NONE;
        loopControl = STATE_INITIATEE;
        break;
      }
    }

    int btn = checkButton();
    if (btn == BTN_LONG_HOLD) {
      Serial.println("Long hold — disconnecting for low power");
      holdEventPast = true;
      longHoldEventPast = true;
      central.disconnect();
      lowPowerMode = true;
      return;
    }
    if (btn != BTN_NONE && btn != BTN_HOLD) {
      Serial.println("Button press — initiating");
      blePTouched.writeValue(SIG_INITIATE);
      pByte = SIG_NONE;
      loopControl = STATE_INITIATOR;
      break;
    }
  }
  Serial.println("Exiting waitForStart");
}

void initiator_B(BLEDevice central, int &loopControl, byte &pByte, byte &cByte) {
  Serial.println("Entering initiator");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (central.connected() && count < INITIATE_MAX_CYCLES) {
    if (checkButton() == BTN_DOUBLE) {
      Serial.println("Initiator double-click — cancelling");
      blePTouched.writeValue(SIG_CANCEL);
      break;
    }

    if (millis() < cycleStart + INITIATE_BUZZ_LENGTH) {
      drv.setRealtimeValue(INITIATE_STRENGTH);
      digitalWrite(RED_PIN, LOW);
    } else {
      drv.setRealtimeValue(0);
      digitalWrite(RED_PIN, HIGH);
    }

    if (millis() >= cycleStart + INITIATE_CYCLE_LEN) {
      cycleStart = millis();
      count++;
    }

    if (bleCTouched.written()) {
      bleCTouched.readValue(cByte);
      Serial.print("cTouched in initiator: "); Serial.println(cByte);
      if (cByte == SIG_INITIATE) {
        cByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
      } else if (cByte == SIG_CANCEL) {
        cByte = SIG_NONE;
        break;
      }
    }
  }

  Serial.println("Exiting initiator");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

void initiatee_B(BLEDevice central, byte &pByte, byte &cByte, int &loopControl) {
  Serial.println("Entering initiatee");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (central.connected() && loopControl == STATE_INITIATEE && count < INITIATE_MAX_CYCLES) {
    if (bleCTouched.written()) {
      bleCTouched.readValue(cByte);
      if (cByte == SIG_CANCEL) {
        cByte = SIG_NONE;
        break;
      }
    }

    if (millis() < cycleStart + INITIATE_BUZZ_LENGTH) {
      drv.setRealtimeValue(INITIATE_STRENGTH);
      digitalWrite(RED_PIN, LOW);
    } else {
      drv.setRealtimeValue(0);
      digitalWrite(RED_PIN, HIGH);
    }

    if (millis() >= cycleStart + INITIATE_CYCLE_LEN) {
      cycleStart = millis();
      count++;
    }

    switch (checkButton()) {
      case BTN_DOUBLE:
        Serial.println("Double-click — cancelling");
        blePTouched.writeValue(SIG_CANCEL);
        loopControl = STATE_INITIATOR;
        break;
      case BTN_NONE:
        break;
      default:
        Serial.println("Accepting initiation");
        blePTouched.writeValue(SIG_INITIATE);
        pByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
    }
  }

  Serial.println("Exiting initiatee");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

void mainLoop_B(BLEDevice central, byte &cByte, byte &pByte) {
  Serial.println("Entering mainLoop");

  int session = -1;
  if (central.connected() && allData.numSessions < MAX_SESSIONS) {
    session = allData.numSessions++;
    allData.sessions[session].beginTime = millis();
    allData.sessions[session].reUpCount = 0;
    allData.sessions[session].endedByButton = false;
    allData.sessions[session].attemptedP[0] = 0;
    allData.sessions[session].attemptedC[0] = 0;
  }

  digitalWrite(BLUE_PIN, LOW);
  digitalWrite(RED_PIN, HIGH);
  unsigned long consentTimer = millis();
  bool sessionActive = true;

  while (central.connected() && sessionActive) {
    long remaining = (long)(consentTimer + CONSENT_DURATION - millis());
    int buzzValue = map(constrain(remaining, 0, CONSENT_DURATION), 0, CONSENT_DURATION, 30, 127);
    drv.setRealtimeValue(buzzValue);

    if (bleCTouched.written()) {
      bleCTouched.readValue(cByte);
      Serial.print("cTouched in mainLoop: "); Serial.println(cByte);

      if (cByte == SIG_PRESSING && session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
        allData.sessions[session].attemptedC[allData.sessions[session].reUpCount]++;
      }

      if (cByte == SIG_PRESSING && pByte == SIG_PRESSING) {
        blePTouched.writeValue(SIG_REUP);
        pByte = SIG_REUP;
        resetConsentTimer(cByte, pByte, consentTimer);
        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].reUpTimes[allData.sessions[session].reUpCount] = millis();
          allData.sessions[session].reUpCount++;
          if (allData.sessions[session].reUpCount < MAX_REUPS) {
            allData.sessions[session].attemptedP[allData.sessions[session].reUpCount] = 0;
            allData.sessions[session].attemptedC[allData.sessions[session].reUpCount] = 0;
          }
        }
      }

      if (cByte == SIG_INITIATE) {
        if (session >= 0) allData.sessions[session].endedByButton = true;
        holdEventPast = true;
        longHoldEventPast = true;
        sessionActive = false;
      }

      if (cByte == SIG_REUP) {
        resetConsentTimer(cByte, pByte, consentTimer);
        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].reUpTimes[allData.sessions[session].reUpCount] = millis();
          allData.sessions[session].reUpCount++;
          if (allData.sessions[session].reUpCount < MAX_REUPS) {
            allData.sessions[session].attemptedP[allData.sessions[session].reUpCount] = 0;
            allData.sessions[session].attemptedC[allData.sessions[session].reUpCount] = 0;
          }
        }
      }
    }

    int btn = checkButton();
    switch (btn) {
      case BTN_DOUBLE:
        Serial.println("Double-click — ending session");
        holdEventPast = true;
        longHoldEventPast = true;
        blePTouched.writeValue(SIG_INITIATE);
        pByte = SIG_NONE;
        if (session >= 0) allData.sessions[session].endedByButton = true;
        sessionActive = false;
        break;
      case BTN_NONE:
        break;
      default:
        blePTouched.writeValue(SIG_PRESSING);
        pByte = SIG_PRESSING;
        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].attemptedP[allData.sessions[session].reUpCount]++;
        }
        break;
    }

    if (millis() > consentTimer + CONSENT_DURATION) {
      Serial.println("Consent timer expired");
      if (session >= 0) allData.sessions[session].endedByButton = false;
      holdEventPast = true;
      longHoldEventPast = true;
      sessionActive = false;
    }
  }

  Serial.println("Exiting mainLoop");
  drv.setRealtimeValue(0);
  if (session >= 0) allData.sessions[session].endTime = millis();
}

// =================================================================
//                      SHARED FUNCTIONS
// =================================================================

void resetConsentTimer(byte &cByte, byte &pByte, unsigned long &consentTimer) {
  Serial.println("Timer reset");
  consentTimer = millis();
  cByte = SIG_NONE;
  pByte = SIG_NONE;
}

void switchModeAndReboot() {
  modePrefs.mode = serialMode ? 0 : 1;
  modeFlash.deletePrefs();
  modeFlash.garbageCollection();
  modeFlash.writePrefs(&modePrefs, sizeof(modePrefs));
  delay(100);
  NVIC_SystemReset();
}

int checkButton() {
  int event = BTN_NONE;
  buttonVal = digitalRead(BUTTON_PIN);

  if (buttonVal == LOW && buttonLast == HIGH && (millis() - upTime) > DEBOUNCE_MS) {
    downTime = millis();
    ignoreUp = false;
    waitForUp = false;
    singleOK = true;
    holdEventPast = false;
    longHoldEventPast = false;
    if ((millis() - upTime) < DC_GAP_MS && !DConUp && DCwaiting) {
      DConUp = true;
    } else {
      DConUp = false;
    }
    DCwaiting = false;
  }
  else if (buttonVal == HIGH && buttonLast == LOW && (millis() - downTime) > DEBOUNCE_MS) {
    if (!ignoreUp) {
      upTime = millis();
      if (!DConUp) {
        DCwaiting = true;
      } else {
        event = BTN_DOUBLE;
        DConUp = false;
        DCwaiting = false;
        singleOK = false;
      }
    }
  }

  if (buttonVal == HIGH && (millis() - upTime) >= DC_GAP_MS
      && DCwaiting && !DConUp && singleOK && event != BTN_DOUBLE) {
    if (!ignoreUp) {
      event = BTN_SINGLE;
      DCwaiting = false;
    }
  }

  if (buttonVal == LOW && (millis() - downTime) >= HOLD_TIME_MS) {
    if (!holdEventPast) {
      event = BTN_HOLD;
      waitForUp = true;
    }
    if ((millis() - downTime) >= LONG_HOLD_MS && !longHoldEventPast) {
      event = BTN_LONG_HOLD;
    }
  }

  // 8-click mode switch
  if (event == BTN_SINGLE || event == BTN_DOUBLE) {
    if (rebootClickCount == 0 || (millis() - rebootWindowStart) > REBOOT_WINDOW) {
      rebootClickCount = 0;
      rebootWindowStart = millis();
    }
    rebootClickCount += (event == BTN_DOUBLE) ? 2 : 1;
    if (rebootClickCount >= REBOOT_CLICKS) {
      switchModeAndReboot();
    }
  }

  buttonLast = buttonVal;
  return event;
}
