// BuzzPeripheral - Advertiser/Server Device
// Advertises and accepts connections from BuzzControl.
// Reads from cTouched (written by Control), writes to pTouched.

#include <ArduinoBLE.h>
#include <Wire.h>
#include "Adafruit_DRV2605.h"
#include <NanoBLEFlashPrefs.h>

// ── Pin Definitions ──────────────────────────────────────────────
#define BUTTON_PIN 1

const int RED_PIN   = LEDR;
const int GREEN_PIN = LEDG;
const int BLUE_PIN  = LEDB;

// ── BLE UUIDs & Service ──────────────────────────────────────────
BLEService deviceService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEByteCharacteristic cTouched("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic pTouched("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);

// ── BLE Signal Values ────────────────────────────────────────────
const byte SIG_NONE     = 0;
const byte SIG_PRESSING = 1;
const byte SIG_INITIATE = 2;  // WaitForStart: "I want to initiate" / Initiatee: "I accept" / MainLoop: "End session"
const byte SIG_REUP     = 3;
const byte SIG_CANCEL   = 4;  // Initiator/Initiatee: "Cancel the initiation"

// ── Loop State Values ────────────────────────────────────────────
const int STATE_INITIATEE = 0;
const int STATE_INITIATOR = 1;
const int STATE_MAIN_LOOP = 2;

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

// ── Global State ─────────────────────────────────────────────────
bool lowPowerMode = false;
bool connected    = false;
int  lastInitiator;  // 0 = control initiated, 1 = peripheral initiated

Adafruit_DRV2605 drv;

// ── Data Storage ─────────────────────────────────────────────────
const int MAX_SESSIONS  = 20;
const int MAX_REUPS     = 25;
const int MAX_INIT_LOG  = 75;

struct SessionData {
  unsigned long beginTime;
  unsigned long endTime;
  unsigned long reUpTimes[MAX_REUPS];
  int8_t attemptedP[MAX_REUPS];  // peripheral re-up attempts per segment
  int8_t attemptedC[MAX_REUPS];  // control re-up attempts per segment
  bool    initiator;              // true = peripheral initiated
  int8_t  reUpCount;
  bool    endedByButton;          // true = button ended, false = timer decay
};

struct AllSessionData {
  SessionData sessions[MAX_SESSIONS];
  int8_t      numSessions;
  unsigned long pInitTimes[MAX_INIT_LOG];
  int8_t        numPInits;
  unsigned long cInitTimes[MAX_INIT_LOG];
  int8_t        numCInits;
};

NanoBLEFlashPrefs myFlashPrefs;
AllSessionData allData;

// ─────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  // UNCOMMENT TO RESET STORAGE:
  // deleteData();
  // myFlashPrefs.writePrefs(&allData, sizeof(allData));
  myFlashPrefs.readPrefs(&allData, sizeof(allData));

  drv.begin();
  drv.setMode(DRV2605_MODE_REALTIME);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  allLEDsOff();

  BLE.begin();
  BLE.setLocalName("Buzz On!");
  BLE.setAdvertisedService(deviceService);

  deviceService.addCharacteristic(cTouched);
  deviceService.addCharacteristic(pTouched);
  cTouched.setValue(0);
  pTouched.setValue(0);

  BLE.addService(deviceService);
  BLE.advertise();
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
void deleteData() {
  Serial.println("Deleting stored data");
  myFlashPrefs.deletePrefs();
  myFlashPrefs.garbageCollection();
}

void saveData() {
  deleteData();
  myFlashPrefs.writePrefs(&allData, sizeof(allData));
}

void displaySessions() {
  Serial.print("Peripheral initiations: "); Serial.println(allData.numPInits);
  Serial.print("Control initiations: ");    Serial.println(allData.numCInits);

  Serial.println("-- Peripheral initiation times --");
  for (int i = 0; i < allData.numPInits; i++) {
    Serial.println(allData.pInitTimes[i]);
  }
  Serial.println("-- Control initiation times --");
  for (int i = 0; i < allData.numCInits; i++) {
    Serial.println(allData.cInitTimes[i]);
  }

  Serial.println(myFlashPrefs.statusString());
  Serial.print("Data size: "); Serial.println(sizeof(allData));

  for (int i = 0; i < allData.numSessions; i++) {
    displaySession(allData.sessions[i], i);
  }
}

void displaySession(SessionData &session, int index) {
  Serial.print("\n=== Session "); Serial.print(index); Serial.println(" ===");
  Serial.print("Begin: ");    Serial.println(session.beginTime);
  Serial.print("End: ");      Serial.println(session.endTime);
  Serial.print("Duration: "); Serial.println(session.endTime - session.beginTime);
  Serial.print("Re-ups: ");   Serial.println(session.reUpCount);
  Serial.print("Initiator: ");
  Serial.println(session.initiator ? "Peripheral" : "Control");
  Serial.print("Ended by: ");
  Serial.println(session.endedByButton ? "Button" : "Decay");

  for (int j = 0; j <= session.reUpCount; j++) {
    Serial.print("  Segment "); Serial.println(j);
    Serial.print("    P attempts: "); Serial.println(session.attemptedP[j]);
    Serial.print("    C attempts: "); Serial.println(session.attemptedC[j]);
    Serial.print("    Segment duration: ");
    if (j == 0) {
      Serial.println(session.reUpTimes[j] - session.beginTime);
    } else if (j == session.reUpCount) {
      Serial.println(session.endTime - session.reUpTimes[j - 1]);
    } else {
      Serial.println(session.reUpTimes[j] - session.reUpTimes[j - 1]);
    }
  }
}

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
// Main Loop
// ─────────────────────────────────────────────────────────────────
void loop() {
  Serial.println("-- Loop: waiting for connection --");

  // Check for long hold to enter low power
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
    connected = true;
    connectionBuzz();

    while (central.connected()) {
      int loopControl = STATE_MAIN_LOOP;
      byte cByte = SIG_NONE;
      byte pByte = SIG_NONE;

      waitForStart(central, loopControl, pByte, cByte);

      if (loopControl == STATE_INITIATOR) {
        initiator(central, loopControl, pByte, cByte);
        if (allData.numPInits < MAX_INIT_LOG) {
          allData.pInitTimes[allData.numPInits] = millis();
          allData.numPInits++;
        }
        lastInitiator = 1;  // peripheral initiated
      } else if (loopControl == STATE_INITIATEE) {
        initiatee(central, pByte, cByte, loopControl);
        if (allData.numCInits < MAX_INIT_LOG) {
          allData.cInitTimes[allData.numCInits] = millis();
          allData.numCInits++;
        }
        lastInitiator = 0;  // control initiated
      }

      if (loopControl == STATE_MAIN_LOOP && central.connected()) {
        mainLoop(central, cByte, pByte);
        if (allData.numSessions <= MAX_SESSIONS) {
          int idx = allData.numSessions - 1;  // mainLoop already incremented
          allData.sessions[idx].initiator = lastInitiator;
          saveData();
        }
      }

      allLEDsOff();
    }

    Serial.println("Disconnected from central");
    connected = false;
    connectionBuzz(lowPowerMode);
  }
}

// ─────────────────────────────────────────────────────────────────
// Wait For Start — idle until someone initiates
// ─────────────────────────────────────────────────────────────────
void waitForStart(BLEDevice central, int &loopControl, byte &pByte, byte &cByte) {
  Serial.println("waitForStart");
  ignoreUp = true;
  holdEventPast = true;
  longHoldEventPast = true;

  // Settling period: flush stale signals from previous state transitions.
  unsigned long settleEnd = millis() + 200;
  while (millis() < settleEnd && central.connected()) {
    if (cTouched.written()) {
      cTouched.readValue(cByte);  // read and discard
    }
    checkButton();  // keep button state machine running
  }

  while (central.connected()) {
    // Check if control initiated
    if (cTouched.written()) {
      cTouched.readValue(cByte);
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
      pTouched.writeValue(SIG_INITIATE);
      pByte = SIG_NONE;
      loopControl = STATE_INITIATOR;
      break;
    }
  }

  Serial.println("Exiting waitForStart");
}

// ─────────────────────────────────────────────────────────────────
// Initiator — we started it, buzz and wait for acceptance
// ─────────────────────────────────────────────────────────────────
void initiator(BLEDevice central, int &loopControl, byte &pByte, byte &cByte) {
  Serial.println("Entering initiator");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (central.connected() && count < INITIATE_MAX_CYCLES) {
    // Double-click to cancel our own initiation
    if (checkButton() == BTN_DOUBLE) {
      Serial.println("Initiator double-click — cancelling");
      pTouched.writeValue(SIG_CANCEL);
      // loopControl stays STATE_INITIATOR → won't enter mainLoop, back to waitForStart
      break;
    }

    // Periodic buzz pattern
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

    // Check for response from control
    if (cTouched.written()) {
      cTouched.readValue(cByte);
      Serial.print("cTouched updated in initiator: "); Serial.println(cByte);

      if (cByte == SIG_INITIATE) {
        // Control accepted — proceed to mainLoop
        cByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
      } else if (cByte == SIG_CANCEL) {
        // Control cancelled — back to waitForStart
        cByte = SIG_NONE;
        // loopControl stays STATE_INITIATOR
        break;
      }
    }
  }

  Serial.println("Exiting initiator");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

// ─────────────────────────────────────────────────────────────────
// Initiatee — other side started it, buzz and wait for our response
// ─────────────────────────────────────────────────────────────────
void initiatee(BLEDevice central, byte &pByte, byte &cByte, int &loopControl) {
  Serial.println("Entering initiatee");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (central.connected() && loopControl == STATE_INITIATEE && count < INITIATE_MAX_CYCLES) {
    // Check for cancel from initiator
    if (cTouched.written()) {
      cTouched.readValue(cByte);
      Serial.print("cTouched updated in initiatee: "); Serial.println(cByte);

      if (cByte == SIG_CANCEL) {
        // Initiator cancelled — back to waitForStart
        cByte = SIG_NONE;
        // loopControl stays STATE_INITIATEE → won't enter mainLoop
        break;
      }
    }

    // Periodic buzz pattern
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

    // Check button
    switch (checkButton()) {
      case BTN_DOUBLE:
        // Double-click cancels — send cancel signal, back to waitForStart
        Serial.println("Double-click — cancelling initiation");
        pTouched.writeValue(SIG_CANCEL);
        loopControl = STATE_INITIATOR;  // won't match mainLoop check
        break;
      case BTN_NONE:
        break;
      default:
        // Any other press accepts — send initiate signal, go to mainLoop
        Serial.println("Accepting initiation");
        pTouched.writeValue(SIG_INITIATE);
        pByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
    }
  }

  Serial.println("Exiting initiatee");
  drv.setRealtimeValue(0);
  digitalWrite(RED_PIN, HIGH);
}

// ─────────────────────────────────────────────────────────────────
// Main Loop — consent timer with mutual re-up
// ─────────────────────────────────────────────────────────────────
void mainLoop(BLEDevice central, byte &cByte, byte &pByte) {
  Serial.println("Entering mainLoop");

  // Start a new session record
  int session = -1;
  if (central.connected() && allData.numSessions < MAX_SESSIONS) {
    session = allData.numSessions++;
    allData.sessions[session].beginTime = millis();
    allData.sessions[session].reUpCount = 0;
    allData.sessions[session].endedByButton = false;
    // Zero out attempt counters for first segment
    allData.sessions[session].attemptedP[0] = 0;
    allData.sessions[session].attemptedC[0] = 0;
  }

  digitalWrite(BLUE_PIN, LOW);
  digitalWrite(RED_PIN, HIGH);
  unsigned long consentTimer = millis();
  bool sessionActive = true;

  while (central.connected() && sessionActive) {
    // Buzz intensity decays over consent duration
    long remaining = (long)(consentTimer + CONSENT_DURATION - millis());
    int buzzValue = map(constrain(remaining, 0, CONSENT_DURATION), 0, CONSENT_DURATION, 30, 127);
    drv.setRealtimeValue(buzzValue);

    // Check for updates from control
    if (cTouched.written()) {
      cTouched.readValue(cByte);
      Serial.print("cTouched in mainLoop: "); Serial.println(cByte);

      // Track control's re-up attempts
      if (cByte == SIG_PRESSING && session >= 0
          && allData.sessions[session].reUpCount < MAX_REUPS) {
        allData.sessions[session].attemptedC[allData.sessions[session].reUpCount]++;
      }

      if (cByte == SIG_PRESSING && pByte == SIG_PRESSING) {
        // Both pressing — confirm re-up
        Serial.println("Mutual press — sending re-up");
        pTouched.writeValue(SIG_REUP);
        pByte = SIG_REUP;
        resetConsentTimer(cByte, pByte, consentTimer);

        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].reUpTimes[allData.sessions[session].reUpCount] = millis();
          allData.sessions[session].reUpCount++;
          // Zero out attempt counters for next segment
          if (allData.sessions[session].reUpCount < MAX_REUPS) {
            allData.sessions[session].attemptedP[allData.sessions[session].reUpCount] = 0;
            allData.sessions[session].attemptedC[allData.sessions[session].reUpCount] = 0;
          }
        }
      }

      if (cByte == SIG_INITIATE) {
        // Control ended the session
        if (session >= 0) {
          allData.sessions[session].endedByButton = true;
        }
        holdEventPast = true;
        longHoldEventPast = true;
        sessionActive = false;
      }

      if (cByte == SIG_REUP) {
        // Control confirmed re-up
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

    // Check local button
    int btn = checkButton();
    switch (btn) {
      case BTN_DOUBLE:
        Serial.println("Double-click — ending session");
        holdEventPast = true;
        longHoldEventPast = true;
        pTouched.writeValue(SIG_INITIATE);
        pByte = SIG_NONE;
        if (session >= 0) {
          allData.sessions[session].endedByButton = true;
        }
        sessionActive = false;
        break;
      case BTN_NONE:
        break;
      default:
        Serial.println("Button press — signaling");
        pTouched.writeValue(SIG_PRESSING);
        pByte = SIG_PRESSING;
        if (session >= 0 && allData.sessions[session].reUpCount < MAX_REUPS) {
          allData.sessions[session].attemptedP[allData.sessions[session].reUpCount]++;
        }
        break;
    }

    // Check consent timer expiry
    if (millis() > consentTimer + CONSENT_DURATION) {
      Serial.println("Consent timer expired");
      if (session >= 0) {
        allData.sessions[session].endedByButton = false;
      }
      holdEventPast = true;
      longHoldEventPast = true;
      sessionActive = false;
    }
  }

  Serial.println("Exiting mainLoop");
  drv.setRealtimeValue(0);

  // Record session end time
  if (session >= 0) {
    allData.sessions[session].endTime = millis();
  }
}

// ─────────────────────────────────────────────────────────────────
// Reset Consent Timer
// ─────────────────────────────────────────────────────────────────
void resetConsentTimer(byte &cByte, byte &pByte, unsigned long &consentTimer) {
  Serial.println("Timer reset");
  consentTimer = millis();
  cByte = SIG_NONE;
  pByte = SIG_NONE;
}

// ─────────────────────────────────────────────────────────────────
// Button Handler
// Returns: BTN_NONE, BTN_SINGLE, BTN_DOUBLE, BTN_LONG_HOLD, BTN_HOLD
// Note: holdEventPast and longHoldEventPast are managed manually
//       throughout the code to control when holds re-fire.
// ─────────────────────────────────────────────────────────────────
int checkButton() {
  int event = BTN_NONE;
  buttonVal = digitalRead(BUTTON_PIN);

  // Button pressed down
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
  // Button released
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

  // Single click (DC gap expired without second press)
  if (buttonVal == HIGH && (millis() - upTime) >= DC_GAP_MS
      && DCwaiting && !DConUp && singleOK && event != BTN_DOUBLE) {
    if (!ignoreUp) {
      event = BTN_SINGLE;
      DCwaiting = false;
    }
  }

  // Hold detection
  if (buttonVal == LOW && (millis() - downTime) >= HOLD_TIME_MS) {
    if (!holdEventPast) {
      event = BTN_HOLD;
      waitForUp = true;
    }
    if ((millis() - downTime) >= LONG_HOLD_MS && !longHoldEventPast) {
      event = BTN_LONG_HOLD;
    }
  }

  buttonLast = buttonVal;
  return event;
}
