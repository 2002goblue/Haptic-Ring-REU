#include <ArduinoBLE.h>

BLEService deviceService("19B10000-E8F2-537E-4F6C-D104768A1214"); // Bluetooth® Low Energy LED Service

// Bluetooth® Low Energy LED Switch Characteristic - custom 128-bit UUID, read and writable by central
BLECharacteristic controlTouched("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
BLECharacteristic peripheralTouched("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
BLECharacteristic stopCharacteristic("19B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

const int redPin = LEDR; // pin for red LED
const int bluePin = LEDB; // pin for blue LED
const int greenPin = LEDG; // pin for green LED

byte controlByte;
byte peripheralByte;
byte stopByte;


int serialByte;

void setup() {
  Serial.begin(9600);


  // set LED pin to output mode
  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  
  // begin initialization
  if (!BLE.begin()) {
    if (Serial) Serial.println("starting Bluetooth® Low Energy module failed!");

    while (1);
  }

  // set advertised local name and service UUID:
  BLE.setLocalName("XIAO");
  BLE.setAdvertisedService(deviceService);

  // add the characteristic to the service
  deviceService.addCharacteristic(controlTouched);
  deviceService.addCharacteristic(peripheralTouched);
  deviceService.addCharacteristic(stopCharacteristic);
  controlTouched.setValue(0);
  peripheralTouched.setValue(0);
  stopCharacteristic.setValue(0)

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
  // listen for Bluetooth® Low Energy peripherals to connect:
  BLEDevice central = BLE.central();

  // if a central is connected to peripheral:
  if (central) {
    if (Serial) Serial.print("Connected to central: ");
    // print the central's MAC address:
    if (Serial)Serial.println(central.address());

    // while the central is still connected to peripheral:
    while (central.connected()) {
      controlTouched.readValue(controlByte);
      peripheralTouched.readValue(peripheralByte);
      stopCharacteristic.readValue(stopByte);

      if(Serial) {
        //Serial.println("Connected");
        if(Serial.available() > 0) {
          Serial.println("Available");
          serialByte = Serial.parseInt();

          Serial.println(serialByte);
          Serial.println("\n");

          switch(serialByte) {
          case 1: 
            controlTouch.writeValue( (( (int) (controlTouched.value()) == 1) ? 0 : 1));
            Serial.print(controlTouched.value());
            peripheralTouched.writeValue((int) (0));
            stopCharacteristic.writeValue((int) (0));
            break;
          case 2:
            peripheralCharacteristic.writeValue( (( (int) (peripheralCharacteristic.value()) == 1) ? 0 : 1));
            Serial.print(peripheralTouched.value());
            controlTouched.writeValue((int) (0));
            stopCharacteristic.writeValue((int) (0));
            break;
          case 3:
            stopCharacteristic.writeValue( (( (int) (stopCharacteristic.value()) == 1) ? 0 : 1));
            Serial.print(stopCharacteristic.value());
            peripheralTouched.writeValue((int) (0));
            controlCharacteristic.writeValue((int) (0));
        }
        }
      }

      // if the remote device wrote to the characteristic,
      // use the value to control the LED:
      if (controlByte) {
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
        if (Serial) Serial.println(peripheralTouched.read());
      }
      if (stopCharacteristic.written()) {
        digitalWrite(redPin, HIGH);
        digitalWrite(bluePin, HIGH);
        digitalWrite(greenPin, LOW);
        if (Serial) Serial.println("GREEN LED (stopCharacteristic written)");        
      }
    }

    // when the central disconnects, print it out:
    if (Serial) Serial.print(F("Disconnected from central: "));
    if (Serial) Serial.println(central.address());
  }
}
