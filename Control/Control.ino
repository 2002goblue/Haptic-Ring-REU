// BuzzControl - Unified (BLE + Serial)
// 8 rapid clicks toggles mode and reboots.
// Mode is persisted to flash.

#include <ArduinoBLE.h>
#include <Wire.h>
#include "Adafruit_DRV2605.h"
#include <NanoBLEFlashPrefs.h>

// ── Pin Definitions ──────────────────────────────────────────────
#define BUTTON_PIN 1

const int RED_PIN   = LEDR;
const int GREEN_PIN = LEDG;
const int BLUE_PIN  = LEDB;

// ── BLE UUIDs ────────────────────────────────────────────────────
const char* SERVICE_UUID   = "19B10000-E8F2-537E-4F6C-D104768A1212";
const char* C_TOUCHED_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214";
const char* P_TOUCHED_UUID = "19B10002-E8F2-537E-4F6C-D104768A1214";

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

// ── Mode Storage ─────────────────────────────────────────────────
// 0 = BLE, 1 = Serial
struct ModePrefs {
  uint8_t mode;
};

NanoBLEFlashPrefs modeFlash;
ModePrefs modePrefs;
bool serialMode = false;

// ── Serial Communication (Serial mode only) ──────────────────────
byte lastPByte = SIG_NONE;
bool pUpdated  = false;
String serialInputBuffer = "";

Adafruit_DRV2605 drv;

// ── BLE handles (BLE mode only) ──────────────────────────────────
BLECharacteristic bleCTouched;
BLECharacteristic blePTouched;

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

  if (serialMode) {
    Serial.println("BuzzControl Serial Ready");
    connected = true;
    connectionBuzz();
  } else {
    BLE.begin();
    BLE.setLocalName("BuzzControl");
    Serial.println("BuzzControl BLE Ready");
  }
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

// ─────────────────────────────────────────────────────────────────
// LED Helpers
// ─────────────────────────────────────────────────────────────────
void allLEDsOff() {
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(BLUE_PIN, HIGH);
}



// =================================================================
//                      SERIAL MODE
// =================================================================

void serialCheckSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialInputBuffer.length() > 0) {
        if (serialInputBuffer.startsWith("P:")) {
          lastPByte = serialInputBuffer.substring(2).toInt();
          pUpdated = true;
        }
        serialInputBuffer = "";
      }
    } else {
      serialInputBuffer += c;
    }
  }
}

void serialWriteCTouched(byte value) {
  Serial.print("C:");
  Serial.println(value);
}

bool serialPTouchedUpdated(byte &pByte) {
  serialCheckSerial();
  if (pUpdated) {
    pByte = lastPByte;
    pUpdated = false;
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

  waitForStart_S(loopControl, cByte, pByte);

  if (loopControl == STATE_INITIATOR) {
    initiator_S(loopControl, cByte, pByte);
  } else if (loopControl == STATE_INITIATEE) {
    initiatee_S(cByte, pByte, loopControl);
  }

  if (loopControl == STATE_MAIN_LOOP) {
    mainLoop_S(cByte, pByte);
  }

  allLEDsOff();
}

void waitForStart_S(int &loopControl, byte &cByte, byte &pByte) {
  Serial.println("waitForStart");
  ignoreUp = true;
  holdEventPast = true;
  longHoldEventPast = true;

  unsigned long settleEnd = millis() + 200;
  while (millis() < settleEnd) {
    serialPTouchedUpdated(pByte);
    checkButton();
  }

  while (true) {
    if (serialPTouchedUpdated(pByte)) {
      Serial.print("pTouched updated: "); Serial.println(pByte);
      if (pByte == SIG_INITIATE) {
        pByte = SIG_NONE;
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
      serialWriteCTouched(SIG_INITIATE);
      cByte = SIG_NONE;
      loopControl = STATE_INITIATOR;
      break;
    }
  }
  Serial.println("Exiting waitForStart");
}

void initiator_S(int &loopControl, byte &cByte, byte &pByte) {
  Serial.println("Entering initiator");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (count < INITIATE_MAX_CYCLES) {
    if (checkButton() == BTN_DOUBLE) {
      Serial.println("Initiator double-click — cancelling");
      serialWriteCTouched(SIG_CANCEL);
      cByte = SIG_CANCEL;
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

    if (serialPTouchedUpdated(pByte)) {
      Serial.print("pTouched in initiator: "); Serial.println(pByte);
      if (pByte == SIG_INITIATE) {
        pByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
      } else if (pByte == SIG_CANCEL) {
        pByte = SIG_NONE;
        break;
      }
    }
  }

  Serial.println("Exiting initiator");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

void initiatee_S(byte &cByte, byte &pByte, int &loopControl) {
  Serial.println("Entering initiatee");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (loopControl == STATE_INITIATEE && count < INITIATE_MAX_CYCLES) {
    if (serialPTouchedUpdated(pByte)) {
      if (pByte == SIG_CANCEL) {
        pByte = SIG_NONE;
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
        serialWriteCTouched(SIG_CANCEL);
        cByte = SIG_CANCEL;
        loopControl = STATE_INITIATOR;
        break;
      case BTN_NONE:
        break;
      default:
        Serial.println("Accepting initiation");
        serialWriteCTouched(SIG_INITIATE);
        cByte = SIG_NONE;
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

  digitalWrite(BLUE_PIN, LOW);
  digitalWrite(RED_PIN, HIGH);
  unsigned long consentTimer = millis();
  bool sessionActive = true;

  while (sessionActive) {
    long remaining = (long)(consentTimer + CONSENT_DURATION - millis());
    int buzzValue = map(constrain(remaining, 0, CONSENT_DURATION), 0, CONSENT_DURATION, 30, 127);
    drv.setRealtimeValue(buzzValue);

    if (serialPTouchedUpdated(pByte)) {
      Serial.print("pTouched in mainLoop: "); Serial.println(pByte);

      if (pByte == SIG_PRESSING && cByte == SIG_PRESSING) {
        serialWriteCTouched(SIG_REUP);
        cByte = SIG_REUP;
        resetConsentTimer(cByte, pByte, consentTimer);
      }
      if (pByte == SIG_INITIATE) {
        holdEventPast = true;
        longHoldEventPast = true;
        sessionActive = false;
      }
      if (pByte == SIG_REUP) {
        resetConsentTimer(cByte, pByte, consentTimer);
      }
    }

    int btn = checkButton();
    switch (btn) {
      case BTN_DOUBLE:
        Serial.println("Double-click — ending session");
        holdEventPast = true;
        longHoldEventPast = true;
        serialWriteCTouched(SIG_INITIATE);
        cByte = SIG_NONE;
        sessionActive = false;
        break;
      case BTN_NONE:
        break;
      default:
        serialWriteCTouched(SIG_PRESSING);
        cByte = SIG_PRESSING;
        break;
    }

    if (millis() > consentTimer + CONSENT_DURATION) {
      Serial.println("Consent timer expired");
      holdEventPast = true;
      longHoldEventPast = true;
      sessionActive = false;
    }
  }

  Serial.println("Exiting mainLoop");
  drv.setRealtimeValue(0);
}

// =================================================================
//                        BLE MODE
// =================================================================

void loopBLE() {
  Serial.println("-- Loop: scanning for peripheral --");
  connectToPeripheral();
}

void connectToPeripheral() {
  BLEDevice peripheral;

  if (!lowPowerMode) {
    do {
      BLE.scanForUuid(SERVICE_UUID);
      peripheral = BLE.available();
      if (checkButton() == BTN_LONG_HOLD) {
        connectionBuzz(true);
        holdEventPast = true;
        longHoldEventPast = true;
        lowPower();
        return;
      }
    } while (!peripheral);

    Serial.println("Found peripheral");
  } else {
    lowPower();
    return;
  }

  if (peripheral) {
    BLE.stopScan();
    controlPeripheral(peripheral);
  }
}

void lowPower() {
  Serial.println("Entering low power mode");
  BLE.stopScan();

  while (checkButton() != BTN_LONG_HOLD) {}

  Serial.println("Waking from low power");
  connectionBuzz(true);
  lowPowerMode = false;
  ignoreUp = true;
  holdEventPast = true;
  longHoldEventPast = true;
}

void controlPeripheral(BLEDevice peripheral) {
  if (!peripheral.connect()) {
    Serial.println("Connection failed");
    return;
  }

  connected = true;
  connectionBuzz();

  if (!peripheral.discoverAttributes()) {
    Serial.println("Attribute discovery failed");
    peripheral.disconnect();
    return;
  }

  bleCTouched = peripheral.characteristic(C_TOUCHED_UUID);
  blePTouched = peripheral.characteristic(P_TOUCHED_UUID);

  if (!bleCTouched || !bleCTouched.canWrite()) {
    Serial.println("cTouched characteristic invalid");
    peripheral.disconnect();
    return;
  }
  if (!blePTouched || !blePTouched.canWrite()) {
    Serial.println("pTouched characteristic invalid");
    peripheral.disconnect();
    return;
  }

  bleCTouched.subscribe();
  blePTouched.subscribe();

  Serial.println("Connected — entering session loop");

  while (peripheral.connected() && !lowPowerMode) {
    int loopControl = STATE_IDLE;
    byte cByte = SIG_NONE;
    byte pByte = SIG_NONE;

    waitForStart_B(peripheral, loopControl, cByte, pByte);

    if (loopControl == STATE_INITIATOR) {
      initiator_B(peripheral, loopControl, cByte, pByte);
    } else if (loopControl == STATE_INITIATEE) {
      initiatee_B(peripheral, cByte, pByte, loopControl);
    }

    if (loopControl == STATE_MAIN_LOOP && peripheral.connected()) {
      mainLoop_B(peripheral, cByte, pByte);
    }

    allLEDsOff();
  }

  connected = false;
  Serial.println("Disconnected from peripheral");
  connectionBuzz(lowPowerMode);
}

void waitForStart_B(BLEDevice peripheral, int &loopControl, byte &cByte, byte &pByte) {
  Serial.println("waitForStart");
  ignoreUp = true;
  holdEventPast = true;
  longHoldEventPast = true;

  unsigned long settleEnd = millis() + 200;
  while (millis() < settleEnd && peripheral.connected()) {
    if (blePTouched.valueUpdated()) {
      blePTouched.readValue(pByte);
    }
    checkButton();
  }

  while (peripheral.connected()) {
    if (blePTouched.valueUpdated()) {
      blePTouched.readValue(pByte);
      Serial.print("pTouched updated: "); Serial.println(pByte);
      if (pByte == SIG_INITIATE) {
        pByte = SIG_NONE;
        loopControl = STATE_INITIATEE;
        break;
      }
    }

    int btn = checkButton();
    if (btn == BTN_LONG_HOLD) {
      Serial.println("Long hold — disconnecting for low power");
      holdEventPast = true;
      longHoldEventPast = true;
      peripheral.disconnect();
      lowPowerMode = true;
      return;
    }
    if (btn != BTN_NONE && btn != BTN_HOLD) {
      Serial.println("Button press — initiating");
      bleCTouched.writeValue(SIG_INITIATE);
      cByte = SIG_NONE;
      loopControl = STATE_INITIATOR;
      break;
    }
  }
  Serial.println("Exiting waitForStart");
}

void initiator_B(BLEDevice peripheral, int &loopControl, byte &cByte, byte &pByte) {
  Serial.println("Entering initiator");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (peripheral.connected() && count < INITIATE_MAX_CYCLES) {
    if (checkButton() == BTN_DOUBLE) {
      Serial.println("Initiator double-click — cancelling");
      bleCTouched.writeValue(SIG_CANCEL);
      cByte = SIG_CANCEL;
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

    if (blePTouched.valueUpdated()) {
      blePTouched.readValue(pByte);
      Serial.print("pTouched in initiator: "); Serial.println(pByte);
      if (pByte == SIG_INITIATE) {
        pByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
      } else if (pByte == SIG_CANCEL) {
        pByte = SIG_NONE;
        break;
      }
    }
  }

  Serial.println("Exiting initiator");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

void initiatee_B(BLEDevice peripheral, byte &cByte, byte &pByte, int &loopControl) {
  Serial.println("Entering initiatee");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (peripheral.connected() && loopControl == STATE_INITIATEE && count < INITIATE_MAX_CYCLES) {
    if (blePTouched.valueUpdated()) {
      blePTouched.readValue(pByte);
      if (pByte == SIG_CANCEL) {
        pByte = SIG_NONE;
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
        bleCTouched.writeValue(SIG_CANCEL);
        cByte = SIG_CANCEL;
        loopControl = STATE_INITIATOR;
        break;
      case BTN_NONE:
        break;
      default:
        Serial.println("Accepting initiation");
        bleCTouched.writeValue(SIG_INITIATE);
        cByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
    }
  }

  Serial.println("Exiting initiatee");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

void mainLoop_B(BLEDevice peripheral, byte &cByte, byte &pByte) {
  Serial.println("Entering mainLoop");

  digitalWrite(BLUE_PIN, LOW);
  digitalWrite(RED_PIN, HIGH);
  unsigned long consentTimer = millis();
  bool sessionActive = true;

  while (peripheral.connected() && sessionActive) {
    long remaining = (long)(consentTimer + CONSENT_DURATION - millis());
    int buzzValue = map(constrain(remaining, 0, CONSENT_DURATION), 0, CONSENT_DURATION, 30, 127);
    drv.setRealtimeValue(buzzValue);

    if (blePTouched.valueUpdated()) {
      blePTouched.readValue(pByte);
      Serial.print("pTouched in mainLoop: "); Serial.println(pByte);

      if (pByte == SIG_PRESSING && cByte == SIG_PRESSING) {
        bleCTouched.writeValue(SIG_REUP);
        cByte = SIG_REUP;
        resetConsentTimer(cByte, pByte, consentTimer);
      }
      if (pByte == SIG_INITIATE) {
        holdEventPast = true;
        longHoldEventPast = true;
        sessionActive = false;
      }
      if (pByte == SIG_REUP) {
        resetConsentTimer(cByte, pByte, consentTimer);
      }
    }

    int btn = checkButton();
    switch (btn) {
      case BTN_DOUBLE:
        Serial.println("Double-click — ending session");
        holdEventPast = true;
        longHoldEventPast = true;
        bleCTouched.writeValue(SIG_INITIATE);
        cByte = SIG_NONE;
        sessionActive = false;
        break;
      case BTN_NONE:
        break;
      default:
        bleCTouched.writeValue(SIG_PRESSING);
        cByte = SIG_PRESSING;
        break;
    }

    if (millis() > consentTimer + CONSENT_DURATION) {
      Serial.println("Consent timer expired");
      holdEventPast = true;
      longHoldEventPast = true;
      sessionActive = false;
    }
  }

  Serial.println("Exiting mainLoop");
  drv.setRealtimeValue(0);
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
  modePrefs.mode = serialMode ? 0 : 1;  // Toggle
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
