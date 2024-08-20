//CODE WORKSPACE (SAVE CODE FROM THIS ONE TO VERSION UPDATES AT IMPORTANT MILESTONES, BUT THIS ONE ALWAYS REMAINS WORKSPACE)

#include <ArduinoBLE.h>
#include <Wire.h>
#include "Adafruit_DRV2605.h"

#define buttonPin 1        // analog input pin to use as a digital input

const char* deviceService = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* controlTouchedCharacteristic = "19B10001-E8F2-537E-4F6C-D104768A1214";
const char* peripheralTouchedCharacteristic = "19B10002-E8F2-537E-4F6C-D104768A1214";
const int redPin = LEDR; // pin for red LED
const int bluePin = LEDB; // pin for blue LED
const int greenPin = LEDG; // pin for green LED
Adafruit_DRV2605 drv;

// Button timing variables
int debounce = 20;          // ms debounce period to prevent flickering when pressing or releasing the button
int DCgap = 250;            // max ms between clicks for a double click event
int holdTime = 1500;        // ms hold period: how long to wait for press+hold event
int longHoldTime = 4000;    // ms long hold period: how long to wait for press+hold event

// Button variables
boolean buttonVal = HIGH;   // value read from button
boolean buttonLast = HIGH;  // buffered value of the button's previous state
boolean DCwaiting = false;  // whether we're waiting for a double click (down)
boolean DConUp = false;     // whether to register a double click on next release, or whether to wait and click
boolean singleOK = true;    // whether it's OK to do a single click
long downTime = -1;         // time the button was pressed down
long upTime = -1;           // time the button was released
boolean ignoreUp = false;   // whether to ignore the button release because the click+hold was triggered
boolean waitForUp = false;        // when held, whether to wait for the up event
boolean holdEventPast = false;    // whether or not the hold event happened already
boolean longHoldEventPast = false;// whether or not the long hold event happened already

const int initiateStrength = 127;
const int initiateBuzzLength = 200;
const int initiateLength = 1000;
const int initiateIterations = 31; 

bool lowPowerMode = false;
bool connected = false;

int connectionBuzzOnLength = 200;
int connectionBuzzOffLength = 100;

const int consentDuration = 30000;

byte cLast;

void setup() {
  //Serial Output:
  Serial.begin(9600);

  drv.begin();
  drv.setMode(DRV2605_MODE_REALTIME);

  BLE.begin();
  
  // Set button input pin
  pinMode(buttonPin, INPUT_PULLUP);

  // set advertised local name and service UUID:
  BLE.setLocalName("BuzzControl");
  //BLE.advertise();
}

void loop() {
  //Serial Output:
  Serial.println("Beginning of Loop Function");

  connectToPeripheral();
}

void connectionBuzz(bool lowPowerModeActivated = false) {
  
  int motorCycles;
  if(lowPowerModeActivated) {
    motorCycles = 3;
    connectionBuzzOnLength = 100;
    connectionBuzzOffLength = 100;
    digitalWrite(bluePin, LOW);
  } else {
    motorCycles = 2;
    if (connected) {
      digitalWrite(greenPin, LOW);
    } else {
      digitalWrite(redPin, LOW);
    }
  }
  bool motorOn = true;
  int buzzTime = millis();
  drv.setRealtimeValue(127);

  while(motorCycles > 0) {
    if(motorOn && millis() > buzzTime + connectionBuzzOnLength) {
      drv.setRealtimeValue(0);
      buzzTime = millis();
      motorOn = false;
      motorCycles--;
    } else if(!motorOn && millis() > buzzTime + connectionBuzzOffLength) {
      drv.setRealtimeValue(127);
      buzzTime = millis();
      motorOn = true;
    }

  }
  drv.setRealtimeValue(0);
  digitalWrite(bluePin, HIGH);
  digitalWrite(greenPin, HIGH);
  digitalWrite(redPin, HIGH);
}

void connectToPeripheral(){
  //Serial Output:
  Serial.println("Entered connectToPeripheral");

  BLEDevice peripheral;

  if(checkButton() == 4) {
    //Serial Output:
    Serial.println("Checkbutton == 4, Sending Connection Buzz");

    lowPowerMode = true;
    connectionBuzz(true);
  }

  if(!lowPowerMode) {
    //Serial Output:
    Serial.println("lowPowerMode = false");

    do
    {
      BLE.scanForUuid(deviceService);
      peripheral = BLE.available();
      if(checkButton() == 4) {
        connectionBuzz(true);
        lowPower();
      }
    } while (!peripheral);
    
    //Serial Output:
    Serial.println("Found Peripheral");

  } else {
    lowPower();
  }
  
  if (peripheral) {
    //Serial Output:
    Serial.println("Entering controlPeripheral");

    //peripheral.address();
    //peripheral.localName();
    //peripheral.advertisedServiceUuid();
    BLE.stopScan();
    controlPeripheral(peripheral);
  }
}

// void connectToPeripheral(){
//   BLEDevice peripheral;

//   if(checkButton() == 4) {
//     lowPowerMode = true;
//     connectionBuzz(true);
//   }
//   if(!lowPowerMode) {
//     do
//     {
//       BLE.scanForUuid(deviceService);
//       peripheral = BLE.available();
//       checkButton();
//     } while (!peripheral);
//   } else {
//     //BLE.end();
//     while (checkButton() != 4) {};
//     connectionBuzz(true);
//     lowPowerMode = false;
//     //BLE.begin();
//     ignoreUp = true;
//     holdEventPast = true;
//   }
  
//   if (peripheral) {
//     //peripheral.address();
//     //peripheral.localName();
//     //peripheral.advertisedServiceUuid();
//     BLE.stopScan();
//     controlPeripheral(peripheral);
//   }
// }

void lowPower() { 
  //Serial Output:
    Serial.println("lowPowerMode = true, ending bluetooth, waiting for button hold");

    //BLE.end();
    BLE.stopScan();
    while (checkButton() != 4) {};

    //Serial Output:
    Serial.println("Button hold detected, beginning bluetooth");

    connectionBuzz(true);
    lowPowerMode = false;
    //BLE.begin();
    ignoreUp = true;
    holdEventPast = true;
}

void controlPeripheral(BLEDevice peripheral) {

  if (!peripheral.connect()) {
    //Serial Output:
    Serial.println("Peripheral Disconnected @ beginning of controlPeripheral");
    return;
  }

  connected = true;
  connectionBuzz();

  if (!peripheral.discoverAttributes()) {
    //Serial Output:
    Serial.println("Attributes not discovered, disconnecting");

    peripheral.disconnect();
    return;
  }

  BLECharacteristic cTouched = peripheral.characteristic(controlTouchedCharacteristic);
  BLECharacteristic pTouched = peripheral.characteristic(peripheralTouchedCharacteristic);

  cTouched.subscribe();
  pTouched.subscribe();
    
  if (!cTouched) {
    //Serial Output:
    Serial.println("!cTouched");

    peripheral.disconnect();
    return;
  } else if (!cTouched.canWrite()) {
    //Serial Output:    
    Serial.println("!cTouched.canWrite()");
  peripheral.disconnect();
    return;
  }

  if (!pTouched) {
    Serial.println("!pTouched");
    peripheral.disconnect();
    return;
  } else if (!pTouched.canWrite()) {
    Serial.println("!cTouched.canWrite()");
    peripheral.disconnect();
    return;
  }

  //connectionBuzz();

  //Serial Output:
  Serial.println("Entering while(peripheral.connected()) in controlPeripheral");
 
  while (peripheral.connected()) {

    int start = 0;
    int x = 2;
    byte cByte = 0;
    byte pByte = 0;

    waitForStart(peripheral, cTouched, pTouched, x, cByte, pByte);

    if (x == 1) {
      initiator(peripheral, cTouched, pTouched, x, cByte, pByte);
    }
      
    else if (x == 0) {
      initiatee(peripheral, cTouched, pTouched, cByte, pByte, x);
    }

    if (x == 2) {
      mainLoop(peripheral, cTouched, pTouched, start, cByte, pByte);
    }

    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, HIGH);
    digitalWrite(bluePin, HIGH);
  }
  connected = false;

  connectionBuzz(lowPowerMode);
}

void mainLoop(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched, int &start, byte &cByte, byte &pByte) {
  //Serial Output:
  Serial.println("Entering mainLoop");
  
  digitalWrite(bluePin, LOW);
  digitalWrite(redPin, HIGH);
  unsigned long time = millis();

  while (peripheral.connected() && (start != 1)) {
    
    drv.setRealtimeValue(map(time+consentDuration-millis(), 0, consentDuration, 30, 127));

    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);

      //Serial Output:
      Serial.println("pTouched value updated: ");
      Serial.println(pByte);
      //Serial.println("cLast before update: ");
      //Serial.println(cLast);
      Serial.println("cByte before update: ");
      Serial.println(cByte);

      //cLast = cByte;
      //cTouched.readValue(cByte);

      //Serial Output:
      //Serial.println("cByte newly read: ");
      //Serial.println(cByte);

      //if(cLast == cByte) {
        //Serial Output:
        Serial.println("cLast == cByte");

        if ((cByte == 1) && (pByte == 1)) {
          //Serial Output:
          Serial.println("cByte == 1 && pByte == 1, writing cTouched to 3");

          cTouched.writeValue((byte) 3);
          cByte = 3;
          timer(cTouched, pTouched, cByte, pByte, time);
        }

        if (pByte == 2) {
          break;
        }

        if (pByte == 3) {
          timer (cTouched, pTouched, cByte, pByte, time);
        }
      //}
    }

    switch(checkButton()) {
      case 4:
        //Serial Output:
        Serial.println("case 4: writing cTouched to 2");

        cTouched.writeValue((byte) 2);
        cByte = 0;
        start = 1;
      case 0:
        break;
      default: 
        //Serial Output:
        Serial.println("default case, writing cTouched to 1");

        cTouched.writeValue((byte) 1);
        cByte = 1;
        break;
    }

    if (millis() > time + consentDuration) {
      break;
    }
  }

  //Serial Output:
  Serial.println("Exiting mainLoop, start: ");
  Serial.println(start);
  
  drv.setRealtimeValue(0);
}

void timer(BLECharacteristic cTouched, BLECharacteristic pTouched, byte &cByte, byte &pByte, unsigned long &time) {
  //Serial Output:
  Serial.println("Entered Timer");

  time = millis();
  //pTouched.writeValue((byte) 0);
  pByte = 0;
  cByte = 0;
}

void waitForStart(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched, int &x, byte &cByte, byte &pByte) {
  //Serial Output:
  Serial.println("Entered waitForStart");

  // pTouched.readValue(pByte);
  // if (pByte == 2) {

  //   Serial.println("Found 2 early");
  //   pByte = 0;
  //   x = 0;
  //   return;
  // }

  int i;
  ignoreUp = true;
  holdEventPast = true;
  while (peripheral.connected()) {

    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);

      //Serial Output:
      Serial.println("Value Updated, pByte: ");
      Serial.println(pByte);

      if (pByte == 2) {
        //Serial Output:
        Serial.println("Writing pTouched to 0 & breaking because pByte == 2");

        //pTouched.writeValue((byte) 0);
        pByte = 0;
        x = 0;
        break;
      }
    }

    i = checkButton();
    //Serial Output:
    //Serial.println("Button value: ");
    //Serial.println(i);

    if (i == 4 || i == 3) {
      //Serial Output:
      Serial.println("Disconnecting because long hold detected");

      peripheral.disconnect();
      lowPowerMode = true;
      return;
    }

    if (i != 0) {
      //Serial Output:
      Serial.println("i!=0, writing cTouched to 2");

      cTouched.writeValue((byte) 2);
      cByte = 0;
      x = 1;
      break;
    }
  }

  //Serial Output:
  Serial.println("Exiting waitForStart");
  if(!peripheral.connected()) Serial.println("Due to disconnect");

}

void initiator(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched, int &x, byte &cByte, byte &pByte) {
  //Serial Output:
  Serial.println("Entering Initiator");

  unsigned long time = millis();
  int count = 0;
  int y;

  while ((peripheral.connected()) && (x == 1) && (count != initiateIterations)) {
    
    if (checkButton() == 4) {
      //Serial Output:
      Serial.println("checkButton() == 4, setting cTouched to 4");

      cTouched.writeValue((byte) 4);
      cByte = 4;
      x = 0;
      break;
    }

    if (millis() > (time + initiateLength)) {
      drv.setRealtimeValue(initiateStrength);
      digitalWrite(redPin, LOW);
      time = millis();
      count++;
    }

    if ((millis() > (time + initiateBuzzLength)) && (millis() < (time + initiateBuzzLength + 100))) {
      digitalWrite(redPin, HIGH);
      drv.setRealtimeValue(0);
    }

    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);

      //Serial Output:
      Serial.println("pTouched value updated");
      Serial.println(pByte);

      if (pByte == 4) {
        pByte = 0;
        x = 0;
        break;
      }
      if (pByte == 2) {
        //Serial Output:
        Serial.println("pByte == 2, setting pTouched to 0");

        //pTouched.writeValue((byte) 0);
        pByte = 0;
        x = 2;
      }
    }
  } 
  //Serial Output:
  Serial.println("Exiting Initiator");

  drv.setRealtimeValue(0);
}

void initiatee(BLEDevice peripheral, BLECharacteristic cTouched, BLECharacteristic pTouched, byte cByte, byte pByte, int &x) {
  //Serial Output:
  Serial.println("Entering initiatee");

  int i;
  unsigned long time = millis();
  int count = 0;

  while ((peripheral.connected()) && (x == 0) && (count != initiateIterations)) {

    if (pTouched.valueUpdated()) {
      pTouched.readValue(pByte);
      //Serial Output:
      Serial.println("pTouched value updated");
      Serial.println(pByte);

      if (pByte == 4) {
        pByte = 0;
        x = 1;
        break;
      }
    }

    if (millis() > (time + initiateLength)) {
      drv.setRealtimeValue(initiateStrength);
      digitalWrite(redPin, LOW);
      time = millis();
      count++;
    }

    if ((millis() > (time + initiateBuzzLength)) && (millis() < (time + initiateBuzzLength + 100))) {
      digitalWrite(redPin, HIGH);
      drv.setRealtimeValue(0);
    }

    switch(checkButton()) {
      case 4:
        //Serial Output:
        Serial.println("case 4: writing cTouched to 4");

        cTouched.writeValue((byte) 4);
        cByte = 4;
        x = 1;
        break;
      default:
        //Serial Output:
        Serial.println("case default: writing cTouched to 2");

        cTouched.writeValue((byte) 2);
        cByte = 0;
        x = 2;
        break;
      case 0:

        break;
    }
  }

  //Serial Output:
  Serial.println("Exiting Initiatee");

  drv.setRealtimeValue(0);
}

int checkButton() {    
   int event = 0;
   buttonVal = digitalRead(buttonPin);
   // Button pressed down
   if (buttonVal == LOW && buttonLast == HIGH && (millis() - upTime) > debounce)
   {
       downTime = millis();
       ignoreUp = false;
       waitForUp = false;
       singleOK = true;
       holdEventPast = false;
       longHoldEventPast = false;
       if ((millis()-upTime) < DCgap && DConUp == false && DCwaiting == true)  DConUp = true;
       else  DConUp = false;
       DCwaiting = false;
   }
   // Button released
   else if (buttonVal == HIGH && buttonLast == LOW && (millis() - downTime) > debounce)
   {        
       if (not ignoreUp)
       {
           upTime = millis();
           if (DConUp == false) DCwaiting = true;
           else
           {
               event = 2;
               DConUp = false;
               DCwaiting = false;
               singleOK = false;
           }
       }
   }
   // Test for normal click event: DCgap expired
   if ( buttonVal == HIGH && (millis()-upTime) >= DCgap && DCwaiting == true && DConUp == false && singleOK == true && event != 2)
   {  
       event = 1;
       DCwaiting = false;
   }
   // Test for hold
   if (buttonVal == LOW && (millis() - downTime) >= holdTime) {
      // Trigger "normal" hold
      if (not holdEventPast)
      {
        event = 4;
        waitForUp = true;
        holdEventPast = true;
      }
       // Trigger "long" hold
       if ((millis() - downTime) >= longHoldTime)
       {
           if (not longHoldEventPast)
           {
               event = 3;
               longHoldEventPast = true;
           }
       }
   }
   buttonLast = buttonVal;
   return event;
}