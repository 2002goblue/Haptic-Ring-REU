// BuzzControl - Central/Scanner Device
// Scans for and connects to the BuzzPeripheral device.
// Writes to cTouched, reads from pTouched.

#include <ArduinoBLE.h>
#include <Wire.h>
#include "Adafruit_DRV2605.h"

// ── Pin Definitions ──────────────────────────────────────────────
#define BUTTON_PIN 1

const int RED_PIN   = LEDR;
const int GREEN_PIN = LEDG;
const int BLUE_PIN  = LEDB;

// ── BLE UUIDs ────────────────────────────────────────────────────
const char* SERVICE_UUID   = "19B10000-E8F2-537E-4F6C-D104768A1212";
const char* C_TOUCHED_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214";
const char* P_TOUCHED_UUID = "19B10002-E8F2-537E-4F6C-D104768A1214";

// ── BLE Signal Values ────────────────────────────────────────────
const byte SIG_NONE     = 0;  // Idle / reset
const byte SIG_PRESSING = 1;  // MainLoop: "I'm pressing my button"
const byte SIG_INITIATE = 2;  // WaitForStart: "I want to initiate" / Initiatee: "I accept" / MainLoop: "End session"
const byte SIG_REUP     = 3;  // MainLoop: "Mutual consent confirmed, reset timer"
const byte SIG_CANCEL   = 4;  // Initiator/Initiatee: "Cancel the initiation"

// ── Loop State Values ────────────────────────────────────────────
const int STATE_INITIATEE = 0;
const int STATE_INITIATOR = 1;
const int STATE_MAIN_LOOP = 2;
const int STATE_IDLE      = 3;  // Default — nothing happened yet

// ── Button Event Values ──────────────────────────────────────────
const int BTN_NONE        = 0;
const int BTN_SINGLE      = 1;
const int BTN_DOUBLE      = 2;
const int BTN_LONG_HOLD   = 3;
const int BTN_HOLD        = 4;

// ── Initiation Parameters ────────────────────────────────────────
const int INITIATE_STRENGTH    = 127;
const int INITIATE_BUZZ_LENGTH = 200;   // ms buzz on per cycle
const int INITIATE_CYCLE_LEN   = 1000;  // ms total per initiation cycle
const int INITIATE_MAX_CYCLES  = 30;

// ── Consent Timer ────────────────────────────────────────────────
const unsigned long CONSENT_DURATION = 30000;  // ms

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
const unsigned long REBOOT_WINDOW = 4000;  // 4 seconds

// ── Global State ─────────────────────────────────────────────────
bool lowPowerMode = false;
bool connected    = false;

Adafruit_DRV2605 drv;

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

  BLE.begin();
  BLE.setLocalName("BuzzControl");
}

// ─────────────────────────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────────────────────────
void loop() {
  Serial.println("-- Loop: scanning for peripheral --");
  connectToPeripheral();
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
// Scanning & Connection
// ─────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────
// Low Power Mode
// ─────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────
// Main Peripheral Control (runs while connected)
// ─────────────────────────────────────────────────────────────────
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

  BLECharacteristic cTouched = peripheral.characteristic(C_TOUCHED_UUID);
  BLECharacteristic pTouched = peripheral.characteristic(P_TOUCHED_UUID);

  if (!cTouched || !cTouched.canWrite()) {
    Serial.println("cTouched characteristic invalid");
    peripheral.disconnect();
    return;
  }
  if (!pTouched || !pTouched.canWrite()) {
    Serial.println("pTouched characteristic invalid");
    peripheral.disconnect();
    return;
  }

  cTouched.subscribe();
  pTouched.subscribe();

  Serial.println("Connected — entering session loop");

  while (peripheral.connected() && !lowPowerMode) {
    int loopControl = STATE_IDLE;
    byte cByte = SIG_NONE;
    byte pByte = SIG_NONE;

    waitForStart(peripheral, cTouched, pTouched, loopControl, cByte, pByte);

    if (loopControl == STATE_INITIATOR) {
      initiator(peripheral, cTouched, pTouched, loopControl, cByte, pByte);
    } else if (loopControl == STATE_INITIATEE) {
      initiatee(peripheral, cTouched, pTouched, cByte, pByte, loopControl);
    }

    if (loopControl == STATE_MAIN_LOOP && peripheral.connected()) {
      mainLoop(peripheral, cTouched, pTouched, cByte, pByte);
    }

    allLEDsOff();
  }

  connected = false;
  Serial.println("Disconnected from peripheral");
  connectionBuzz(lowPowerMode);
}

// ─────────────────────────────────────────────────────────────────
// Wait For Start — idle until someone initiates
// ─────────────────────────────────────────────────────────────────
void waitForStart(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched,
                  int &loopControl, byte &cByte, byte &pByte) {
  Serial.println("waitForStart");
  ignoreUp = true;
  holdEventPast = true;
  longHoldEventPast = true;

  // Settling period: flush stale signals from previous state transitions.
  // This prevents e.g. a leftover "end session" (SIG_INITIATE) from being
  // misread as a new initiation request.
  unsigned long settleEnd = millis() + 200;
  while (millis() < settleEnd && peripheral.connected()) {
    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);  // read and discard
    }
    checkButton();  // keep button state machine running
  }

  while (peripheral.connected()) {
    // Check if peripheral initiated
    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);
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
      cTouched.writeValue(SIG_INITIATE);
      cByte = SIG_NONE;
      loopControl = STATE_INITIATOR;
      break;
    }
  }

  Serial.println("Exiting waitForStart");
}

// ─────────────────────────────────────────────────────────────────
// Initiator — we started it, buzz and wait for acceptance
// ─────────────────────────────────────────────────────────────────
void initiator(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched,
               int &loopControl, byte &cByte, byte &pByte) {
  Serial.println("Entering initiator");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (peripheral.connected() && count < INITIATE_MAX_CYCLES) {
    // Double-click to cancel our own initiation
    if (checkButton() == BTN_DOUBLE) {
      Serial.println("Initiator double-click — cancelling");
      cTouched.writeValue(SIG_CANCEL);
      cByte = SIG_CANCEL;
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

    // Check for response from peripheral
    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);
      Serial.print("pTouched updated in initiator: "); Serial.println(pByte);

      if (pByte == SIG_INITIATE) {
        // Peripheral accepted — proceed to mainLoop
        pByte = SIG_NONE;
        loopControl = STATE_MAIN_LOOP;
        break;
      } else if (pByte == SIG_CANCEL) {
        // Peripheral cancelled — back to waitForStart
        pByte = SIG_NONE;
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
void initiatee(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched,
               byte &cByte, byte &pByte, int &loopControl) {
  Serial.println("Entering initiatee");
  holdEventPast = true;
  longHoldEventPast = true;
  ignoreUp = true;

  unsigned long cycleStart = millis();
  int count = 0;

  while (peripheral.connected() && loopControl == STATE_INITIATEE && count < INITIATE_MAX_CYCLES) {
    // Check for cancel from initiator
    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);
      Serial.print("pTouched updated in initiatee: "); Serial.println(pByte);

      if (pByte == SIG_CANCEL) {
        // Initiator cancelled — back to waitForStart
        pByte = SIG_NONE;
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
        cTouched.writeValue(SIG_CANCEL);
        cByte = SIG_CANCEL;
        loopControl = STATE_INITIATOR;  // won't match mainLoop check
        break;
      case BTN_NONE:
        break;
      default:
        // Any other press accepts — send initiate signal, go to mainLoop
        Serial.println("Accepting initiation");
        cTouched.writeValue(SIG_INITIATE);
        cByte = SIG_NONE;
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
void mainLoop(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched,
              byte &cByte, byte &pByte) {
  Serial.println("Entering mainLoop");

  digitalWrite(BLUE_PIN, LOW);
  digitalWrite(RED_PIN, HIGH);
  unsigned long consentTimer = millis();
  bool sessionActive = true;

  while (peripheral.connected() && sessionActive) {
    // Buzz intensity decays over consent duration
    long remaining = (long)(consentTimer + CONSENT_DURATION - millis());
    int buzzValue = map(constrain(remaining, 0, CONSENT_DURATION), 0, CONSENT_DURATION, 30, 127);
    drv.setRealtimeValue(buzzValue);

    // Check for updates from peripheral
    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);
      Serial.print("pTouched in mainLoop: "); Serial.println(pByte);

      if (pByte == SIG_PRESSING && cByte == SIG_PRESSING) {
        // Both pressing — confirm re-up
        Serial.println("Mutual press — sending re-up");
        cTouched.writeValue(SIG_REUP);
        cByte = SIG_REUP;
        resetConsentTimer(cByte, pByte, consentTimer);
      }

      if (pByte == SIG_INITIATE) {
        // Peripheral ended the session
        holdEventPast = true;
        longHoldEventPast = true;
        sessionActive = false;
      }

      if (pByte == SIG_REUP) {
        // Peripheral confirmed re-up
        resetConsentTimer(cByte, pByte, consentTimer);
      }
    }

    // Check local button
    int btn = checkButton();
    switch (btn) {
      case BTN_DOUBLE:
        Serial.println("Double-click — ending session");
        holdEventPast = true;
        longHoldEventPast = true;
        cTouched.writeValue(SIG_INITIATE);
        cByte = SIG_NONE;
        sessionActive = false;
        break;
      case BTN_NONE:
        break;
      default:
        Serial.println("Button press — signaling");
        cTouched.writeValue(SIG_PRESSING);
        cByte = SIG_PRESSING;
        break;
    }

    // Check consent timer expiry
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

  // Reboot check: 8 clicks within 4 seconds triggers reset
  if (event == BTN_SINGLE || event == BTN_DOUBLE) {
    if (rebootClickCount == 0 || (millis() - rebootWindowStart) > REBOOT_WINDOW) {
      rebootClickCount = 0;
      rebootWindowStart = millis();
    }
    rebootClickCount += (event == BTN_DOUBLE) ? 2 : 1;
    if (rebootClickCount >= REBOOT_CLICKS) {
      NVIC_SystemReset();
    }
  }

  buttonLast = buttonVal;
  return event;
}