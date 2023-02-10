/*
   Smart Lock PoC: Lock and unlock based on smartphone or smartwatch proximity.
   The BLE Server implementation and the BLE Scanner implementation was based on Neil Kolban examples.
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEServer.h>


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
const int buttonPin = 14;
const int ledPin = 4; //show if door is locked
int buttonState = 0;
const bool useHall = false; //if using hall sensor to verify if the door is closed, use "true"

// Server calback
//Setup callbacks onConnect and onDisconnect
class MyServerCallbacks: public BLEServerCallbacks { //if more info needes, visit: https://randomnerdtutorials.com/esp32-ble-server-client/
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
          Serial.println("*************FOUND!**************");
          break;
        }
        else
          device_found = false;
      }
      Serial.printf("Advertised Device: %s RSSI: %d \n", advertisedDevice.getAddress().toString().c_str(), advertisedDevice.getRSSI() );
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
    /*if (!added) {
      if (knownBLEAddresses[i].c_str() == "" ) {
        knownBLEAddresses[i] = remoteAddress;  //<<<<<<<<converter para string????
        added = true;
      }
      else if (knownBLEAddresses[i].c_str() == remoteAddress) {
        added  = true;
      }
    else {
      if (knownBLEAddresses[i].c_str() == remoteAddress) { //if key already added, remove it to avoid key duplicity
          knownBLEAddresses[i] = "";
      }
    }
    */
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

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT);

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

  pCharacteristic->setValue("Esse é o valor dado a característica...");
  //start service and advertising 
  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristic defined! Now you can read it in your phone!");

/////scanner
  Serial.println("Scanning...");

  //BLEDevice::init("Fechadura");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
}

void loop() {
// put your main code here, to run repeatedly:

//Button control loops
  buttonState = digitalRead(buttonPin);
  if (buttonState == HIGH) { //if pressed
    if (deviceConnected) {
      addKey();    
    }
  }

//SCAN  control loops
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  
  for (int i = 0; i < foundDevices.getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    //int rssi = device.getRSSI();
    //Serial.print("RSSI: ");
    //Serial.println(rssi);
    if (/*rssi > RSSI_THRESHOLD &&*/ device_found == true && doorClosed())   {
      Serial.println("Unlock the Door!");
      digitalWrite(LED_BUILTIN, HIGH);
      digitalWrite(ledPin, LOW);
      delay(5000);
    }
  }
  
  if (!device_found && doorClosed()) {
      Serial.println("Lock the door");      
      digitalWrite(LED_BUILTIN, LOW);
      digitalWrite(ledPin, HIGH);      
  }
  delay(2000);
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory



  /**Serial.print("Devices found: ");
  Serial.println(foundDevices.getCount());
  Serial.println("Scan done!");
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
  delay(2000);
  */
}
