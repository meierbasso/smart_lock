

/*
   Smart Lock PoC: Lock and unlock based on smartphone or smartwatch proximity.
   Developed by Carlos Alberto Meier Basso
   The BLE Server implementation and the BLE Scanner implementation was based on Neil Kolban examples adapted to ESP32 by Evandro Copercini.

   GPIO Map:
    4 : Turns on a led to indicate the door is locked
    12: A button to lock or unlock the door from inside
    14: A button to add a new key device (that must be connected on the BLE Server)
    17: solenoid control - to use solenoid mechanism, the var useSolenoid must be true; 
    18: servo control (yellow) - to use a servo, the var useServo must be true;
    25 or LED_BUILTIN - turns on a LED when a key device is in the unlock area
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEServer.h>

// if using Servo: Include the ESP32 Arduino Servo Library instead of the original Arduino Servo Library
#include <ESP32Servo.h> 
int servoPin = 18; //GPIO to attach servo control 
Servo myservo;  // create servo object to control a servo
const bool useServo = true; //if using a servo to lock or unlock
int solenoidPin = 17; //GPIO to attach solenoid control 
const bool useSolenoid = true; //if using a solenoid to lock or unlock

//Server definitions and vars
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
bool deviceConnected = false;
char remoteAddress[18];


//scan definitions and vars
const int MAX_KEYS = 3;
String knownBLEAddresses[MAX_KEYS] = {"77:a1:20:82:0a:4f", "", ""};
int RSSI_THRESHOLD = -100;
bool device_found;
int scanTime = 5; //In seconds
BLEScan* pBLEScan;

//periferrals definitions and vars
const int addKeyButtonPin = 14;
const int lockButtonPin = 12;
const int ledPin = 4; //show if door is locked
int buttonState = 0;
const bool useHall = false; //if using hall sensor to verify if the door is closed, use "true"
bool locked = false; //status if the door is locked or not

// calback do server
//Setup callbacks onConnect and onDisconnect
class MyServerCallbacks: public BLEServerCallbacks { 
	void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {

		//char remoteAddress[18];
    Serial.print("Dispositivo BLE conectado com o MAC: ");
		sprintf(
			remoteAddress,
			"%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
			param->connect.remote_bda[0],
			param->connect.remote_bda[1],
			param->connect.remote_bda[2],
			param->connect.remote_bda[3],
			param->connect.remote_bda[4],
			param->connect.remote_bda[5]
		);

		ESP_LOGI(LOG_TAG, "myServerCallback onConnect, MAC: %s", remoteAddress);

		deviceConnected = true;
    Serial.println(remoteAddress);
	}  
  void onDisconnect(BLEServer* pServer) {
    Serial.println("Dispositivo BLE desconectado.");    
    deviceConnected = false;
  }
};

//callback do scan
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      for (int i = 0; i < (sizeof(knownBLEAddresses) / sizeof(knownBLEAddresses[0])); i++)
      {
        //Uncomment to Enable Debug Information
        //Serial.println("*************Start**************");
        //Serial.println(sizeof(knownBLEAddresses));
        //Serial.println(sizeof(knownBLEAddresses[0]));
        //Serial.println(sizeof(knownBLEAddresses)/sizeof(knownBLEAddresses[0]));
        //Serial.println(advertisedDevice.getAddress().toString().c_str());
        //Serial.println(knownBLEAddresses[i].c_str());
        //Serial.println("*************End**************");
        if (strcmp(advertisedDevice.getAddress().toString().c_str(), knownBLEAddresses[i].c_str()) == 0)
        {
          device_found = true;
          Serial.println("Dispositivo chave detectado!");
          break;
        }
        else
          device_found = false;
      }
      Serial.printf("Advertised Device: %s RSSI: %d \n", advertisedDevice.getAddress().toString().c_str(), advertisedDevice.getRSSI() );
      Serial.println(advertisedDevice.toString().c_str()); //debug only
    }
};

void addKey() { //adds a new key to the keys array
  bool added = false;
  for (int i = 0; i < MAX_KEYS; i++) {  
    if (!added && knownBLEAddresses[i].c_str() == "") {
       knownBLEAddresses[i] = remoteAddress; // <<<<<<<<converter para string????
       added=true;
    }
    else if (knownBLEAddresses[i].c_str() == remoteAddress) {
      if (added) {//if key already present on array and already added, remove it to avoid key duplicity
        knownBLEAddresses[i] = ""; 
      }
      added == true; //key already added
    }
  }
  if (added) {//debug only
    Serial.printf("Key %s added successfully!", remoteAddress);
  }  
  else {
    Serial.printf("Key %s NOT added! Maybe there is no space for new keys (Max: %i keys).", remoteAddress, MAX_KEYS);    
  }
}

bool doorClosed() { //verify if the Hall sensor should be used and verify if the door is closed (returns true if hall is not being considered or if the door is closed)
  if (useHall && hallRead() > 0) { //if considering the Hall sensor and it not near the magnet, then it's opened (verify values according your magnet and position)
    Serial.println("Hall detects door opened."); 
    return false;
  }
  Serial.println("Hall detects door closed or sensor disabled.");
  return true; //if not considering Hall sensor, returns true, as the door is closed.
}

void unlockByServo() { //unlock the door using a servo
  Serial.println("Unlocking by servo");
  for (int pos = 0; pos <= 180; pos += 1) {
    // in steps of 1 degree
    myservo.write(pos);
    delay(15); // waits 15ms to reach the position
  }
}
void lockByServo() { //lock the door using a servo
  Serial.println("Locking by servo");
  for (int pos = 180; pos >= 0; pos -= 1) {
    myservo.write(pos);
    delay(15); // waits 15ms to reach the position
  }                   
}


void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(addKeyButtonPin, INPUT);
  pinMode(lockButtonPin, INPUT);

/////server
  //create BLE device
  BLEDevice::init("Fechadura Smart");
  //create BLE server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  //create BLE service
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setValue("Characteristic value...");
  //start service and advertising 
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();


/////scanner
  Serial.println("Scanning...");

  //BLEDevice::init("Fechadura");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value

//////servo
  myservo.setPeriodHertz(50);// Standard 50hz servo
  myservo.attach(servoPin, 500, 2400);   // attaches the servo on pin 18
}


void proceedUnlocking() {
  Serial.println("Unlock the Door!");
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(ledPin, LOW);
  if (useServo) unlockByServo();
  if (useSolenoid) digitalWrite(solenoidPin, HIGH);
  locked = false;
  delay(5000);
  /*if (useSolenoid) { //solenoid shouldn't be HIGH for more than 5 seconds. If needed, it will repeat the process.
    digitalWrite(solenoidPin, LOW);
    locked = true;
  }*/
}
void proceedLocking() {
  Serial.println("Lock the door");      
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(ledPin, HIGH); 
  if (useServo)    lockByServo();  
  if (useSolenoid) digitalWrite(solenoidPin, LOW);
  locked = true;   
}

void loop() {

//Button control loops
  buttonState = digitalRead(addKeyButtonPin);
  if (buttonState == HIGH) { //if pressed
    Serial.println("Add key button pressed");
    if (deviceConnected) {
      addKey();    
    }
  }
  buttonState = digitalRead(lockButtonPin);
  if (buttonState == HIGH) {
    if (locked) {
      proceedUnlocking();      
    }
    else {
      proceedLocking(); 
    }
  }

//SCAN  control loops
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  
  //for (int i = 0; i < foundDevices.getCount(); i++) {
  //  BLEAdvertisedDevice device = foundDevices.getDevice(i);

    //Proceed Unlocking
    if (/*rssi > RSSI_THRESHOLD &&*/ device_found == true && locked && doorClosed())   {
      proceedUnlocking();
      //break;   
    }
  //}
  
  //Proceed Locking
  if (!device_found && !locked && doorClosed()) {
    proceedLocking();
  }
  delay(2000);
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory

}
