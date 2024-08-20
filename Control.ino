#include <ArduinoBLE.h>

const char* deviceServiceUuid = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* controlTouchedID = "19B10001-E8F2-537E-4F6C-D104768A1214";
const char* peripheralTouchedID = "19B10002-E8F2-537E-4F6C-D104768A1214";
const char* stopCharacteristicID = "19B10003-E8F2-537E-4F6C-D104768A1214";
int serialByte;

const int redPin = LEDR; // pin for red LED
const int bluePin = LEDB; // pin for blue LED
const int greenPin = LEDG; // pin for green LED

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  if (!BLE.begin()) {
    if (Serial) Serial.println("Bluetooth Failed");
    while(1);
  }

  BLE.setLocalName("ControlRing");
  BLE.advertise();
  if (Serial) Serial.println("ADVERTISING!");
}

void loop() {
  // put your main code here, to run repeatedly:
  BLEDevice peripheral;
  if (Serial) Serial.println("Looking for Peripheral");

  do {
    BLE.scanForUuid(deviceServiceUuid);
    peripheral = BLE.available();
  } while(!peripheral);

  BLE.stopScan();

  if(peripheral.connect()) {
    if (Serial) Serial.println("Connected to peripheral!");
  } else {
    if (Serial) Serial.println("Failed to connect");
    return;
  }

  if(peripheral.discoverAttributes()) {
    if (Serial) Serial.println("Attributes found!");
  } else {
    if (Serial) Serial.println("Attribute discovery failed!");
    peripheral.disconnect();
    return;
  }

  BLECharacteristic controlTouched = peripheral.characteristic(controlTouchedID);
  BLECharacteristic peripheralTouched = peripheral.characteristic(peripheralTouchedID);
  BLECharacteristic stopCharacteristic = peripheral.characteristic(stopCharacteristicID);

  while(peripheral.connected()) {
    if(Serial) {
        if(Serial.available() > 0) {
          serialByte = Serial.read();
        }
        switch(serialByte) {
          case 1: 
            controlTouched.writeValue((byte) (( (int) (controlTouched.value()) == 1) ? 0 : 1));
            Serial.println((int) controlTouched.value());
            break;
          case 2:
            peripheralTouched.writeValue((byte) (( (int) (peripheralTouched.value()) == 1) ? 0 : 1));  
            Serial.println((int) peripheralTouched.value());
            break;
          case 3:
            stopCharacteristic.writeValue((byte) (( (int) (stopCharacteristic.value()) == 1) ? 0 : 1));
            Serial.println((int) stopCharacteristic.value());
        }
      }

      // if the remote device wrote to the characteristic,
      // use the value to control the LED:
      if (controlTouched.written()) {
        digitalWrite(redPin, LOW);
        digitalWrite(bluePin, HIGH);
        digitalWrite(greenPin, HIGH);
        if (Serial) Serial.println("Red LED (controlTouched written)");
      }
      if (peripheralTouched.written()) {
        digitalWrite(redPin, HIGH);
        digitalWrite(bluePin, LOW);
        digitalWrite(greenPin, HIGH);
        if (Serial) Serial.println("BLUE LED (peripheralTouched written)");
      }
      if (stopCharacteristic.written()) {
        digitalWrite(redPin, HIGH);
        digitalWrite(bluePin, HIGH);
        digitalWrite(greenPin, LOW);
        if (Serial) Serial.println("GREEN LED (stopCharacteristic written)");        
      }
    }
  }