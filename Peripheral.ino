//CODE WORKSPACE (SAVE CODE FROM THIS ONE TO VERSION UPDATES AT IMPORTANT MILESTONES, BUT THIS ONE ALWAYS REMAINS WORKSPACE)

#include <ArduinoBLE.h>

BLEService deviceService("19B10000-E8F2-537E-4F6C-D104768A1214"); // Bluetooth速 Low Energy LED Service

// Bluetooth速 Low Energy LED Switch Characteristic - custom 128-bit UUID, read and writable by central
BLEByteCharacteristic controlTouched("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
BLEByteCharacteristic peripheralTouched("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify);
//BLEByteCharacteristic stopCharacteristic("19B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

const int redPin = LEDR; // pin for red LED
const int bluePin = LEDB; // pin for blue LED
const int greenPin = LEDG; // pin for green LED

byte controlByte;
byte peripheralByte;
//byte stopByte;

int startByte = 0;
int serialByte;

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
  deviceService.addCharacteristic(controlTouched);
  deviceService.addCharacteristic(peripheralTouched);
  //deviceService.addCharacteristic(stopCharacteristic);
  controlTouched.setValue(0);
  peripheralTouched.setValue(0);
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


    //startConsent(central);


    if (Serial) {
      while (startByte != 1) {
        if (Serial.available() > 0) {
          startByte = Serial.parseInt();
        }
      }
    }

    // while the central is still connected to peripheral:
    while (central.connected()) {
      startByte = 0;
      //if (Serial) {
        //if (Serial.available() > 0) {
          //startByte = Serial.parseInt();
          //while (startByte == 1) {

            Serial.println("Ready");

            if (controlTouched.written()) {
              controlTouched.readValue(controlByte);
            }

            //Serial.println("Connected");
            if(Serial.available() > 0) {
              serialByte = Serial.parseInt();

              //Serial.println(serialByte);
              //Serial.println("\n");

              switch(serialByte) {
                case 1:
                  peripheralTouched.writeValue((byte) 0);
                  peripheralByte = 0;
                  Serial.println("Sending 1");
                  break;
                case 2: 
                  peripheralTouched.writeValue((byte) 1);
                  peripheralByte = 1;
                  Serial.println("Sending 2");
                  break;
                case 3:
                  peripheralTouched.writeValue((byte) 2);
                  peripheralByte = 2;
                  Serial.println("Sending 3");

                  while ((central.connected()) && (peripheralByte != 0)) {
                    peripheralTouched.readValue(peripheralByte);
                  }

                  peripheralByte = 2;

                  if (central.connected()) {
                    central.disconnect();
                  }
              }
            }

            if (controlByte == 2) {
              controlTouched.writeValue((byte) 0);
              central.disconnect();
            }

            else if (peripheralByte && controlByte) {
              digitalWrite(redPin, HIGH);
              digitalWrite(bluePin, LOW);
              //if (Serial) Serial.println("Red LED (controlTouched written)");
            }

            else if ((peripheralByte != 2) && (controlByte != 2)) {
              digitalWrite(redPin, LOW);
              digitalWrite(bluePin, HIGH);
              //if (Serial) Serial.println("BLUE LED (peripheralTouched written)");
              //if (Serial) Serial.println(peripheralTouched.read());
            }
          }
        //}
      //}
    //}

    peripheralTouched.writeValue((byte) 0);
    controlTouched.writeValue((byte) 0);
    peripheralByte = 0;
    controlByte = 0;
    // when the central disconnects, print it out:
    if (Serial) Serial.print(F("Disconnected from central: "));
    if (Serial) Serial.println(central.address());
    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, HIGH);
    digitalWrite(bluePin, HIGH);
  }
}

void startConsent(BLEDevice central) {
  if (Serial) {
    Serial.println("We made it to Start Consent");
    while ((central.connected()) && (startByte != 1)) {
      Serial.println("HEREHEHEHEHEH");

      if (controlTouched.written()) {
        controlTouched.readValue(controlByte);
        while (controlByte == 3) {
          digitalWrite(redPin, LOW);
          digitalWrite(bluePin, LOW);
          Serial.println("ARE YOU READY???");
          if (Serial.available() > 0) {
            startByte = Serial.parseInt();
            if (startByte == 1) {
              controlTouched.writeValue((byte) 0);
              controlByte = 1;
            }
          }
        }
      }

      else if (Serial.available() > 0) {
        startByte = Serial.parseInt();
      }
    }

    if (central.connected() && (controlByte == 0)) {
      peripheralTouched.writeValue((byte) 3);
      peripheralByte = 3;
      while (peripheralByte == 3) {
        if (peripheralTouched.written()) {
          peripheralTouched.readValue(peripheralByte);
          Serial.println("WAITINGGGG");
        }
        Serial.println("2nd LOOP");
      }
    }

    else {
      controlByte = 0;
    }

    startByte = 0;
  }
}