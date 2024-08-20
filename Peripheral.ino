//CODE WORKSPACE (SAVE CODE FROM THIS ONE TO VERSION UPDATES AT IMPORTANT MILESTONES, BUT THIS ONE ALWAYS REMAINS WORKSPACE)

#include <ArduinoBLE.h>

BLEService deviceService("19B10000-E8F2-537E-4F6C-D104768A1214"); // Bluetooth速 Low Energy LED Service

// Bluetooth速 Low Energy LED Switch Characteristic - custom 128-bit UUID, read and writable by central
BLEByteCharacteristic cTouched("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic pTouched("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
//BLEByteCharacteristic stopCharacteristic("19B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

const int redPin = LEDR; // pin for red LED
const int bluePin = LEDB; // pin for blue LED
const int greenPin = LEDG; // pin for green LED

void setup() {
  Serial.begin(9600);


  // set LED pin to output mode
  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  digitalWrite(redPin, HIGH);
  digitalWrite(bluePin, HIGH);
  digitalWrite(greenPin, HIGH);
  
  // begin initialization
  if (!BLE.begin()) {
    if (Serial) Serial.println("starting Bluetooth速 Low Energy module failed!");

    while (1);
  }

  // set advertised local name and service UUID:
  BLE.setLocalName("XIAO");
  BLE.setAdvertisedService(deviceService);

  // add the characteristic to the service
  deviceService.addCharacteristic(cTouched);
  deviceService.addCharacteristic(pTouched);
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

  // print address
  if (Serial) Serial.print("Address: ");
  if (Serial) Serial.println(BLE.address());

  if (Serial)Serial.println("XIAO nRF52840 Peripheral");
}

void loop() {
  // listen for Bluetooth速 Low Energy peripherals to connect:
  BLEDevice central = BLE.central();

  // if a central is connected to peripheral:
  if (central) {
    if (Serial) Serial.print("Connected to central: ");
    // print the central's MAC address:
    if (Serial)Serial.println(central.address());

    while (central.connected()) {

      int switchInt;
      int start = 0;
      int x = 2;
      byte cByte = 0;
      byte pByte = 0;

      //cTouched.writeValue((byte) 0);
      //pTouched.writeValue((byte) 0);

      Serial.println("entering waitforstart");
      if (Serial) waitForStart(central, start, x, cByte);
      Serial.println("exiting waitforstart");
      
      if (x == 1) {
        Serial.println("entering initiator");
        if (Serial) initiator(central, x, cByte);
        Serial.println("exiting initiator");
      }
      
      else if (x == 0) {
        Serial.println("entering initiatee");
        if (Serial) initiatee(central, x);
        Serial.println("exiting initiatee");
      }

      if (x == 2) {
        Serial.println("entering main loop");
        if (Serial) mainLoop(central, start, cByte, pByte);
        Serial.println("exiting main loop");
      }

      digitalWrite(redPin, HIGH);
      digitalWrite(greenPin, HIGH);
      digitalWrite(bluePin, HIGH);
    }

    // when the central disconnects, print it out:
    if (Serial) Serial.print(F("Disconnected from central: "));
    if (Serial) Serial.println(central.address());
  }
}

void mainLoop(BLEDevice central, int &start, byte &cByte, byte &pByte) {

  int switchInt;

  // while the central is still connected to peripheral:
  while ((central.connected()) && (start != 1)) {
    //NEW LINE ADDED
    unsigned long time = millis();
    //Serial.println("Ready");

    //If the control board button is touched, update cTouched's value
    if (cTouched.written()) {
      cTouched.readValue(cByte);
      Serial.println("control touched");
      Serial.println(cByte);
      colorChange(start, cByte, pByte);
    }

    //Serial.println("Connected");

    //If we input something
    if (Serial.available() > 0) {
      switchInt = Serial.parseInt();
      Serial.println("switchInt: ");
      Serial.println(switchInt);

      //Serial.println(switchInt);
      //Serial.println("\n");

      switch(switchInt) {
        case 1: 
          pTouched.writeValue((byte) 1);
          pByte = 1;
          Serial.println("Sending 1");
          break;
        case 2:
          pTouched.writeValue((byte) 2);
          pByte = 2;
          Serial.println("Sending 2");

          while ((central.connected()) && (pByte != 2)) {
            if (pTouched.written()) {
              pTouched.readValue(pByte);
            }
            //NEW LINE ADDED  
            //We need to add something to break out of the loop if the other ring sends off signal
            //or if the time runs out
            
            if(millis() > time + 10000) break;
          }

          pByte = 2;
          start = 1;
      }
      colorChange(start, cByte, pByte);
    }
    //NEW LINE ADDED
    if(millis() > time + 10000) break;
  }
}

void colorChange(int &start, byte &cByte, byte &pByte) {

  if (cByte == 2) {
    cTouched.writeValue((byte) 0);
    start = 1;
    Serial.println("I wrote a 0 to ya");
  }

  else if (pByte && cByte) {
    digitalWrite(redPin, HIGH);
    digitalWrite(bluePin, LOW);
    //if (Serial) Serial.println("Red LED (cTouched written)");
  }

  else if ((pByte != 2) && (cByte != 2)) {
    digitalWrite(redPin, LOW);
    digitalWrite(bluePin, HIGH);
    //if (Serial) Serial.println("BLUE LED (pTouched written)");
    //if (Serial) Serial.println(pTouched.read());
  }
}

void waitForStart(BLEDevice central, int &start, int &x, byte &cByte) {
  int i;
  Serial.println("start: ");
  Serial.println(start);

  while ((central.connected()) && (start != 1)) {
    //Serial.println("Waiting for start");

    if (cTouched.written()) {
      cTouched.readValue(cByte);
      if (cByte == 2) {
        cTouched.writeValue((byte) 0);
        cByte = 0;
        x = 0;
        start = 1;
      }
    }

    else if (Serial.available() > 0) {
      i = Serial.parseInt();
      if (i == 1) {
        pTouched.writeValue((byte) 2);
        x = 1;
        start = 1;
      }
    }
  }
  start = 0;
}

void initiator(BLEDevice central, int &x, byte &cByte) {
  unsigned long time = millis();
  int count = 0;

  while ((central.connected()) && (x == 1) && (count != 5)) {

    if (millis() > (time + 1000)) {
      digitalWrite(redPin, LOW);
      time = millis();
      count++;
    }

    if ((millis() > (time + 50)) && (millis() < (time + 100))) {
      digitalWrite(redPin, HIGH);
    }

    //Serial.println("waiting for start (i initiated)");
    if (cTouched.written()) {
      cTouched.readValue(cByte);
      if (cByte == 2) {
        cTouched.writeValue((byte) 0);
        cByte = 0;
        x = 2;
      }
    }
  }
}

void initiatee(BLEDevice central, int &x) {
  int i;
  unsigned long time = millis();
  int count = 0;

  while ((central.connected()) && (x == 0) && (count != 5)) {

    if (millis() > (time + 1000)) {
      digitalWrite(redPin, LOW);
      time = millis();
      count++;
    }

    if ((millis() > (time + 50)) && (millis() < (time + 100))) {
      digitalWrite(redPin, HIGH);
    }

    //Serial.println("waiting for start (other initiated)");
    if (Serial.available() > 0) {
      i = Serial.parseInt();
      if (i == 1) {
        pTouched.writeValue((byte) 2);
        x = 2;
      }
    }
  }
}