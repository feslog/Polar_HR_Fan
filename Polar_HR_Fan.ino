/*********


  Adapted from:
    Rui Santos
    Complete instructions at https://RandomNerdTutorials.com/esp32-ble-server-client/
    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
    The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include "BLEDevice.h"
#include <Wire.h>
#include <M5StickC.h>


/* UUID's of the service, characteristic that we want to read*/
// BLE Service
static BLEUUID bmeServiceUUID(BLEUUID((uint16_t)0x180D));

// BLE Characteristics
//HR Characteristic
static BLEUUID heartRateCharacteristicUUID(BLEUUID((uint16_t)0x2A37));

//Flags stating if should begin connecting and if the connection is up
static boolean doConnect = false;
static boolean connected = false;

//Address of the peripheral device. Address will be found during scanning...
static BLEAddress *pServerAddress;
 
//Characteristicd that we want to read
static BLERemoteCharacteristic* heartRateCharacteristic;


//Activate notify
const uint8_t notificationOn[] = {0x1, 0x0};
const uint8_t notificationOff[] = {0x0, 0x0};

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels


//Variables to store temperature and humidity
char* heartRateChar;


//Flags to check whether new temperature and humidity readings are available
boolean newHeartRate = false;

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }
  
  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

//Connect to the BLE Server that has the name, Service, and Characteristics
bool connectToServer(BLEAddress pAddress) {
   BLEClient* pClient = BLEDevice::createClient();
 
  // Connect to the remote BLE Server.
  pClient->connect(pAddress,BLE_ADDR_TYPE_RANDOM );
  if(!pClient->isConnected()) {
    Serial.println(" - Not connected to server");
    return false;
  }

  pClient->setClientCallbacks(new MyClientCallback());

  Serial.println(" - Connected to server");
 
  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(bmeServiceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(bmeServiceUUID.toString().c_str());
    return (false);
  }
 
 Serial.println(" - Got service");
 
  // Obtain a reference to the characteristics in the service of the remote BLE server.
  heartRateCharacteristic = pRemoteService->getCharacteristic(heartRateCharacteristicUUID);
  

  if (heartRateCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID");
    return false;
  }
  Serial.println(" - Found our characteristics");
 
  //Assign callback functions for the Characteristics
  heartRateCharacteristic->registerForNotify(heartRateNotifyCallback);
  return true;
}

//Callback function that gets called, when another device's advertisement has been received
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(bmeServiceUUID)) { //Check if the advertised service UUID matches
      advertisedDevice.getScan()->stop(); //Scan can be stopped, we found what we are looking for
      pServerAddress = new BLEAddress(advertisedDevice.getAddress()); //Address of advertiser is the one we need
      doConnect = true; //Set indicator, stating that we are ready to connect
      Serial.println("Device found. Connecting!");
    }
  }
};

// HR Characteristic data format (https://www.bluetooth.com/wp-content/uploads/Sitecore-Media-Library/Gatt/Xml/Characteristics/org.bluetooth.characteristic.heart_rate_measurement.xml)
// Byte 0 - Flags: e.g. 22 (0001 0110)
// Bits are numbered from LSB (0) to MSB (7).

//    Bit 0 - Heart Rate Value Format: 0 => UINT8 beats per minute
//    Bit 1-2 - Sensor Contact Status: 11 => Supported and detected
//    Bit 3 - Energy Expended Status: 0 => Not present
//    Bit 4 - RR-Interval: 1 => One or more values are present
// Byte 1 - UINT8 BPM
// Byte 2+ - Optional/extra data, see spec (ignore)

//When the BLE Server sends a new HR reading with the notify property
static void heartRateNotifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, 
                                        uint8_t* pData, size_t length, bool isNotify) {

  if (length < 2) { // too short to contain the data we want
    Serial.print("Characteristic data too short");
    return;
  }  

  // The status field does not seem to be used for the Polar HR10 device
  //uint8_t status = pData[0];
  //boolean contactSupported = status & (1 << 1);
  //boolean contactDetected = status & (1 << 2);

  heartRateChar = (char*)pData[1];
  newHeartRate = true;

  Serial.print("Notify callback for characteristic ");
  for (int i = 0; i < length; i++) {
    Serial.print(pData[i]);
    Serial.print(" ");
  }
  Serial.println();  
}


//function that prints the latest sensor readings in the OLED display
void printReadings(){

  char tmp[12];
  sprintf(tmp,"%u",heartRateChar);

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0,0);
  M5.Lcd.print("HR: ");
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(0,10);
  M5.Lcd.println(tmp);

  Serial.print("HR:");
  Serial.println(tmp);
}

void setup() {

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_YELLOW);
  M5.Lcd.setCursor(0,0,2);
  M5.Lcd.print("BLE Client");
  
  //Start serial communication
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");

  // Setup display
  M5.begin();
  
  //Init BLE device
  BLEDevice::init("");
 
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 30 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30);
}

void loop() {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer(*pServerAddress)) {
      Serial.println("We are now connected to the BLE Server.");
      //Activate the Notify property of each Characteristic
      heartRateCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);
      connected = true;
      doConnect = false;
    } else {
      Serial.println("We have failed to connect to the server; Restart your device to scan for nearby BLE server again.");
    }
    
  }

  // check if still connected
 // if (connected) {
 //   if(!BLEDevice::connected()) {
 //     connected = false
 //     Serial.println("We are now disconnected");
 //   }  
 // }
  
  //if new temperature readings are available, print in the OLED
  if (newHeartRate){
    newHeartRate = false;
    printReadings();
  }
  delay(1000); // Delay a second between loops.
}
