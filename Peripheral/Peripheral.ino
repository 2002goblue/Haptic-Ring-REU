//CODE WORKSPACE (SAVE CODE FROM THIS ONE TO VERSION UPDATES AT IMPORTANT MILESTONES, BUT THIS ONE ALWAYS REMAINS WORKSPACE)
#include <ArduinoBLE.h>
#include <Wire.h>
#include "Adafruit_DRV2605.h"
#include <NanoBLEFlashPrefs.h>

#define buttonPin 1        // analog input pin to use as a digital input

BLEService deviceService("19B10000-E8F2-537E-4F6C-D104768A1214"); // Bluetooth速 Low Energy LED Service

// Bluetooth速 Low Energy LED Switch Characteristic - custom 128-bit UUID, read and writable by central
BLEByteCharacteristic cTouched("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic pTouched("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic numOfSessions("19B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic numOfPInitiations("19B10004-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic numOfCInitiations("19B10005-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);

//BLEByteCharacteristic stopCharacteristic("19B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

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

//Variables for Data Storage
int lastInitiator;
//0 = peripheral, 1 = control
int methodOfEnd;
//0 = Decayed, 1 = broken by signal
int reUps = 0;

//For checking if control value changes
byte pLast;

const int structNumOfSessions = 20;
const int structNumOfReUps = 25;
const int structNumOfInits = 75;


struct dataStruct {
  unsigned long begin;
  unsigned long end;
  unsigned long reUpTime[structNumOfReUps];
  int8_t attemptedP[structNumOfReUps];
  int8_t attemptedC[structNumOfReUps];
  bool initiator;
  int8_t reUps = 0;
  bool methodOfEnd;
};

struct sessionData {
  dataStruct sessionArray[structNumOfSessions];
  int8_t numOfSessions = 0;
  unsigned long pInitTimes[structNumOfInits];
  int8_t numOfPInitiations = 0;
  unsigned long cInitTimes[structNumOfInits];
  int8_t numOfCInitiations = 0;
};

NanoBLEFlashPrefs myFlashPrefs;
sessionData controlSessionData;

void setup() {
  //Serial Output:
  Serial.begin(9600);

  //UNCOMMENT THIS LINE TO RESET STORAGE
  //deleteData();
  //Serial.println(myFlashPrefs.errorString(myFlashPrefs.writePrefs(&controlSessionData, sizeof(controlSessionData))));
  myFlashPrefs.readPrefs(&controlSessionData, sizeof(controlSessionData));

  drv.begin();
  drv.setMode(DRV2605_MODE_REALTIME);

  // Set button input pin
  pinMode(buttonPin, INPUT_PULLUP);


  // set LED pin to output mode
  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  digitalWrite(redPin, HIGH);
  digitalWrite(bluePin, HIGH);
  digitalWrite(greenPin, HIGH);

  // begin initialization
  BLE.begin();

  // set advertised local name and service UUID:
  BLE.setLocalName("Buzz On!");
  BLE.setAdvertisedService(deviceService);

  // add the characteristic to the service
  deviceService.addCharacteristic(cTouched);
  deviceService.addCharacteristic(pTouched);
  deviceService.addCharacteristic(numOfSessions);
  deviceService.addCharacteristic(numOfPInitiations);
  deviceService.addCharacteristic(numOfCInitiations);
  //deviceService.addCharacteristic(stopCharacteristic);
  cTouched.setValue(0);
  pTouched.setValue(0);
  //stopCharacteristic.setValue(0);

  // add service
  BLE.addService(deviceService);

  // set the initial value for the characeristic:
  //switchCharacteristic.writeValue(0);

  // start advertising
  BLE.advertise();

}

void deleteData() {
  //Serial Output:
  Serial.println("deleting data");

  myFlashPrefs.deletePrefs();
  myFlashPrefs.garbageCollection();
}

void displaySessions() {
  Serial.println("Number of Peripheral Initiations: ");
  Serial.println(controlSessionData.numOfPInitiations);
  Serial.println("Number of Control Initiations: ");
  Serial.println(controlSessionData.numOfCInitiations);
  Serial.println("Listing Peripheral Initiation Times: ");
  for(int i = 0; i < controlSessionData.numOfPInitiations; i++) {
    Serial.println(controlSessionData.pInitTimes[i]);
  }
  Serial.println("Listing Control Initiation Times: ");
  for(int i = 0; i < controlSessionData.numOfCInitiations; i++) {
    Serial.println(controlSessionData.cInitTimes[i]);
  }

  Serial.println(myFlashPrefs.statusString());
  Serial.println(sizeof(controlSessionData));

  for(int i = 0; i < controlSessionData.numOfSessions; i++) {
    Serial.println("Session");
    Serial.println(i);
    Serial.println("Begin time: ");
    Serial.println(controlSessionData.sessionArray[i].begin);
    Serial.println("End time: ");
    Serial.println(controlSessionData.sessionArray[i].end);
    Serial.println("Total time elapsed: ");
    Serial.println(controlSessionData.sessionArray[i].end - controlSessionData.sessionArray[i].begin);
    Serial.println("Number of Re-Ups");
    Serial.println(controlSessionData.sessionArray[i].reUps);
    Serial.println("Initiator: ");
    if(controlSessionData.sessionArray[i].initiator) {
      Serial.println("Peripheral");
    } else {
      Serial.println("Control");
    }
    Serial.println("Method of End");
    if(!controlSessionData.sessionArray[i].methodOfEnd) {
      Serial.println("Decay");
    } else {
      Serial.println("Button");
    }
 
    for(int j = 0; j <= controlSessionData.sessionArray[i].reUps; j++) {
      Serial.println("Re-Up Number:");
      Serial.println(j);
      Serial.println("Attempted Re-Ups by Peripheral: ");
      Serial.println(controlSessionData.sessionArray[i].attemptedP[j]);
      Serial.println("Attempted Re-Ups by Control: ");
      Serial.println(controlSessionData.sessionArray[i].attemptedC[j]);
      Serial.println("Time since last Re-Up: ");
      if(j == 0) {
        Serial.println(controlSessionData.sessionArray[i].reUpTime[j] - controlSessionData.sessionArray[i].begin);
      } else if(j == controlSessionData.sessionArray[i].reUps) {
        Serial.println(controlSessionData.sessionArray[i].end - controlSessionData.sessionArray[i].reUpTime[j-1]);
      } else {
        Serial.println(controlSessionData.sessionArray[i].reUpTime[j] - controlSessionData.sessionArray[i].reUpTime[j-1]);
      }
    }
  }
}

void displaySession(dataStruct sessionArray) {
    Serial.println("Begin time: ");
    Serial.println(sessionArray.begin);
    Serial.println("End time: ");
    Serial.println(sessionArray.end);
    Serial.println("Total time elapsed: ");
    Serial.println(sessionArray.end - sessionArray.begin);
    Serial.println("Number of Re-Ups");
    Serial.println(sessionArray.reUps);
    Serial.println("Initiator: ");
    if(sessionArray.initiator) {
      Serial.println("Peripheral");
    } else {
      Serial.println("Control");
    }
    Serial.println("Method of End");
    if(!sessionArray.methodOfEnd) {
      Serial.println("Decay");
    } else {
      Serial.println("Button");
    }
 
    for(int j = 0; j < sessionArray.reUps; j++) {
      Serial.println("Re-Up");
      Serial.println(j);
      Serial.println("Attempted Re-Ups by Peripheral: ");
      Serial.println(sessionArray.attemptedP[j]);
      Serial.println("Attempted Re-Ups by Control: ");
      Serial.println(sessionArray.attemptedC[j]);
      Serial.println("Time since last Re-Up: ");
      if(j == 0) {
        Serial.println(sessionArray.reUpTime[j] - sessionArray.begin);
      } else if(j == sessionArray.reUps - 1) {
        Serial.println(sessionArray.end);
        Serial.println(sessionArray.reUpTime[j-1]);
        Serial.println(sessionArray.end - sessionArray.reUpTime[j-1]);
      } else {
        Serial.println(sessionArray.reUpTime[j] - sessionArray.reUpTime[j-1]);
      }
    }
}


void connectionBuzz(bool lowPowerModeActivated = false) {
  //displaySessions();
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

void loop() {
  //Serial Output:
  Serial.println("Beginning of Loop Function");

  if(checkButton() == 4) {
    //Serial Output:
    Serial.println("Checkbutton == 4, Sending Connection Buzz");

    lowPowerMode = true;
    connectionBuzz(true);
  }

  if (lowPowerMode) {
    //Serial Output:
    Serial.println("lowPowerMode on");

    BLE.stopAdvertise();
    while (checkButton() != 4) {};

  //Serial Output:
    Serial.println("Button hold detected, beginning bluetooth");

    connectionBuzz(true);
    lowPowerMode = false;
    BLE.advertise();
    ignoreUp = true;
    holdEventPast = true;
  }

  // listen for Bluetooth速 Low Energy peripherals to connect:
  BLEDevice central = BLE.central();

  if (central) {
  //Serial Output:
    Serial.println("Connected to Central");

    //displaySessions();
    connected = true;
    connectionBuzz();
    while (central.connected()) {
      int start = 0;
      int x = 2;
      byte cByte = 0;
      byte pByte = 0;

      waitForStart(central, x, cByte);

      if (x == 1) {
        initiator(central, x, cByte);

        if(controlSessionData.numOfPInitiations < structNumOfInits) {
          controlSessionData.pInitTimes[controlSessionData.numOfPInitiations] = millis();
          controlSessionData.numOfPInitiations++;
        }
        lastInitiator = 1;
      }

      else if (x == 0) {
        initiatee(central, cByte, x);
        //Set initiator to Control
        lastInitiator = 0;
        if(controlSessionData.numOfCInitiations < structNumOfInits) {
          controlSessionData.cInitTimes[controlSessionData.numOfCInitiations] = millis();
          controlSessionData.numOfCInitiations++;
        }
      }

      if (x == 2 && central.connected()) {
        //Serial.println("1");
        mainLoop(central, start, cByte, pByte);
        //displaySession(controlSessionData.sessionArray[controlSessionData.numOfSessions]);
        if(controlSessionData.numOfSessions < structNumOfSessions) {
          //Serial.println(" ------------------ ");
          controlSessionData.sessionArray[controlSessionData.numOfSessions].initiator = lastInitiator;
          deleteData();
          //displaySession(controlSessionData.sessionArray[controlSessionData.numOfSessions]);
          myFlashPrefs.writePrefs(&controlSessionData, sizeof(controlSessionData));
        }
      }

      digitalWrite(redPin, HIGH);
      digitalWrite(greenPin, HIGH);
      digitalWrite(bluePin, HIGH);
    }
    //Serial Output:
    Serial.println("Disconnected from central");
    
    connected = false;
    connectionBuzz(lowPowerMode);
  }
}

void mainLoop(BLEDevice central, int &start, byte &cByte, byte &pByte) {
  //Serial Output:
  Serial.println("Entering mainLoop");

  int session;
  digitalWrite(bluePin, LOW);
  digitalWrite(redPin, HIGH);
  unsigned long time = millis();

  //Serial.println("2");

  if(central.connected()) {
    if(controlSessionData.numOfSessions < structNumOfSessions) {
      session = controlSessionData.numOfSessions++;
      controlSessionData.sessionArray[session].begin = millis();
    }
  }

  //Serial.println("3");

  // while the central is still connected to peripheral:
  while ((central.connected()) && (start != 1)) {

    drv.setRealtimeValue(map(time + consentDuration - millis(), 0, consentDuration, 30, 127));

    

    if (cTouched.written()) {
      cTouched.readValue(cByte);

      //Serial Output:
      Serial.println("cTouched value updated: ");
      Serial.println(cByte);
      Serial.println("pLast before update: ");
      Serial.println(pLast);
      Serial.println("pByte before update: ");
      Serial.println(pByte);
      
      pLast = pByte;
      pTouched.readValue(pByte);

      //Serial Output:
      Serial.println("pByte newly read: ");
      Serial.println(pByte);

      if(pByte == pLast) {
        //Serial Output:
        Serial.println("pLast == pByte");

        if (cByte == 1) {
          if(controlSessionData.numOfSessions < structNumOfSessions && controlSessionData.sessionArray[session].reUps < structNumOfReUps) {
            controlSessionData.sessionArray[session].attemptedC[controlSessionData.sessionArray[session].reUps]++;
          }
        }

        if ((cByte == 1) && (pByte == 1)) {
          //Serial Output:
          Serial.println("cByte == 1 && pByte == 1, writing pTouched to 3");

          pTouched.writeValue((byte) 3);
          pByte = 3;
          timer(cByte, pByte, time);
          if(controlSessionData.numOfSessions < structNumOfSessions && controlSessionData.sessionArray[session].reUps < structNumOfReUps) {
            controlSessionData.sessionArray[session].reUpTime[controlSessionData.sessionArray[session].reUps] = millis();
            controlSessionData.sessionArray[session].reUps++;
          }

        }

        if (cByte == 2) {
          if(controlSessionData.numOfSessions < structNumOfSessions) {
            controlSessionData.sessionArray[session].methodOfEnd = 1;
          }
          break;
        }

        if (cByte == 3) {
          //Serial.println("3 RECIEVED");
          timer(cByte, pByte, time);
          if(controlSessionData.numOfSessions < structNumOfSessions && controlSessionData.sessionArray[session].reUps < structNumOfReUps) {
            controlSessionData.sessionArray[session].reUpTime[controlSessionData.sessionArray[session].reUps] = millis();
            controlSessionData.sessionArray[session].reUps++;
          }
        }
      }
    }

      switch (checkButton()) {
        case 4:
          //Serial Output:
          Serial.println("case 4: writing pTouched to 2");

          pTouched.writeValue((byte) 2);
          pByte = 2;
          start = 1;
          if(controlSessionData.numOfSessions < structNumOfSessions) {
            controlSessionData.sessionArray[session].methodOfEnd = 1;
          }
          break;
        case 0:
          break;
        default:
          //Serial Output:
          Serial.println("default case, writing pTouched to 1");

          pTouched.writeValue((byte) 1);
          pByte = 1;
          if(controlSessionData.numOfSessions < structNumOfSessions && controlSessionData.sessionArray[session].reUps < structNumOfReUps) {
            controlSessionData.sessionArray[session].attemptedP[controlSessionData.sessionArray[session].reUps]++;
          }
          break;
      }

      if (millis() > time + consentDuration) {
        if(controlSessionData.numOfSessions < structNumOfSessions) {
          controlSessionData.sessionArray[session].methodOfEnd = 0;
        }
        break;
      }
    }
  //Serial Output:
  Serial.println("Exiting mainLoop, start: ");
  Serial.println(start);

  drv.setRealtimeValue(0);
  if(controlSessionData.numOfSessions < structNumOfSessions) {
    controlSessionData.sessionArray[session].end = millis();
  }
}

void timer(byte &cByte, byte &pByte, unsigned long &time) {
  //Serial Output:
  Serial.println("Entered Timer");

  time = millis();
  cTouched.writeValue((byte) 0);
  cByte = 0;
  pByte = 0;
}

void waitForStart(BLEDevice central, int &x, byte &cByte) {
  //Serial Output:
  Serial.println("Entered waitForStart");

  int i;
  ignoreUp = true;
  holdEventPast = true;
  while (central.connected()) {

    if (cTouched.written()) {
      cTouched.readValue(cByte);

      //Serial Output:
      Serial.println("Value Updated, cByte: ");
      Serial.println(cByte);

      if (cByte == 2) {
        //Serial Output:
        Serial.println("Writing cTouched to 0 & breaking because cByte == 2");

        cTouched.writeValue((byte) 0);
        cByte = 0;
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

      central.disconnect();
      lowPowerMode = true;
      return;
    }
    if (i != 0) {
      //Serial Output:
      Serial.println("i!=0, writing pTouched to 2");

      pTouched.writeValue((byte) 2);
      x = 1;
      break;
    }
  }

  //Serial Output:
  Serial.println("Exiting waitForStart");
  if(!central.connected()) Serial.println("Due to disconnect");
}

void initiator(BLEDevice central, int &x, byte &cByte) {
  //Serial Output:
  Serial.println("Entering Initiator");

  unsigned long time = millis();
  int count = 0;
  int y;

  while ((central.connected()) && (x == 1) && (count != initiateIterations)) {

    if (checkButton() == 4) {
      //Serial Output:
      Serial.println("checkButton() == 4, setting pTouched to 4");

      pTouched.writeValue((byte) 4);
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

    if (cTouched.written()) {
      cTouched.readValue(cByte);

      //Serial Output:
      Serial.println("cTouched value updated");
      Serial.println(cByte);

      if (cByte == 4) {
        x = 0;
        break;
      }

      if (cByte == 2) {
        //Serial Output:
        Serial.println("cByte == 2, setting cTouched to 0");

        cTouched.writeValue((byte) 0);
        cByte = 0;
        x = 2;
      }
    }
  }
  //Serial Output:
  Serial.println("Exiting Initiator");

  drv.setRealtimeValue(0);
}

void initiatee(BLEDevice central, byte cByte, int &x) {
  //Serial Output:
  Serial.println("Entering initiatee");
  
  int i;
  unsigned long time = millis();
  int count = 0;

  while ((central.connected()) && (x == 0) && (count != initiateIterations)) {

    if (cTouched.written()) {
      cTouched.readValue(cByte);
      
      //Serial Output:
      Serial.println("cTouched value updated");
      Serial.println(cByte);

      //Why is this here?
      if (cByte == 4) {
        cByte = 0;
        x = 1;
        break;
      }
    }

    if (millis() > (time + initiateLength)) {
      digitalWrite(redPin, LOW);
      drv.setRealtimeValue(initiateStrength);
      time = millis();
      count++;
    }

    if ((millis() > (time + initiateBuzzLength)) && (millis() < (time + initiateBuzzLength + 100))) {
      digitalWrite(redPin, HIGH);
      drv.setRealtimeValue(0);
    }

    switch (checkButton()) {
      case 4:
        //Serial Output:
        Serial.println("case 4: writing pTouched to 4");

        pTouched.writeValue((byte) 4);
        x = 1;
        break;
      default:
        //Serial Output:
        Serial.println("case default: writing cTouched to 2");

        pTouched.writeValue((byte) 2);
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
    if ((millis() - upTime) < DCgap && DConUp == false && DCwaiting == true)  DConUp = true;
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
  if ( buttonVal == HIGH && (millis() - upTime) >= DCgap && DCwaiting == true && DConUp == false && singleOK == true && event != 2)
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