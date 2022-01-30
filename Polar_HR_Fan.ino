/*********

  Controls the speed of a fan based on heartrate of a detected BLE heartrate band

  The fan interface is an opto-coupler that essentially simulates a button push on the fan itself.
    This fan turns off when the button is held for > 2 seconds
    Subsequent button presses cycles through the speeds H-M-L
    To set speeds, this code always turns the fan off and then presses the button additional times to set the desired speed
    
  Adapted from:
    Rui Santos
    Complete instructions at https://RandomNerdTutorials.com/esp32-ble-server-client/
    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
    The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include "BLEDevice.h"
#include <Wire.h>
#include <M5StickC.h>
#include <elapsedMillis.h>

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

#define FAN_OUT 0 // Fan is connected to pin G0
#define BUTTON_NOT_PRESSED 0
#define BUTTON_PRESSED 1

//Variable to store heartrate
uint8_t heartRate = 0;

//Flags to check whether new temperature and humidity readings are available
boolean newHeartRate = false;

BLEDevice* pBLEDevice;
BLEScan* pBLEScan;

// device states
enum state {stInit, stIdle, stScanning, stConnected};
enum state myState = stIdle;
enum state myOldState = stInit;

// fan speeds
enum fanSpeeds {fsInit, fsOff, fsLow, fsMed, fsHigh};
enum fanSpeeds fanSpeed = fsOff;
enum fanSpeeds fanLastSpeed = fsInit;

// fan timer to prevent from changing speeds too often
elapsedMillis msSinceFanChange;


void startScan();

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }
  
  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
    myState = stIdle;
    
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

  if ((pData[1] > 0) && (heartRate != pData[1])) {  // ignore values of 0 and unchanged values
    heartRate = (uint8_t)pData[1];
    newHeartRate = true;
  }

  Serial.print("Notify callback for characteristic ");
  for (int i = 0; i < length; i++) {
    Serial.print(pData[i]);
    Serial.print(" ");
  }
  Serial.println();  
}


//function that prints the latest sensor readings in the OLED display
void updateDisplay(){

  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(0,0);
  
  switch (myState) {
    case stIdle:
      M5.Lcd.println("Idle");
      break;
    case stScanning:
      M5.Lcd.println("Scanning");
      break;
    case stConnected:
      M5.Lcd.println("Connected");
      M5.Lcd.setCursor(0,12);
      M5.Lcd.print("HR:");
      M5.Lcd.setTextSize(3);
      M5.Lcd.setCursor(0,30);
      M5.Lcd.println(heartRate);
    
      Serial.print("HR: ");
      Serial.println(heartRate);      
      break;
    default:
      break;
  }

  // always show fan speed
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0,85);
  M5.Lcd.println("FAN SPEED:");
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(15,100);  // warning, attempts to set x=20 caused the "M" not to print below - strange bug?
  switch (fanSpeed) {
    case fsOff:
      M5.Lcd.setTextColor(TFT_BLUE);
      M5.Lcd.println("0");
      break;
    case fsLow:
      M5.Lcd.setTextColor(TFT_YELLOW);
      M5.Lcd.println("L");
      break;
    case fsMed:
      M5.Lcd.setTextColor(TFT_ORANGE);
      M5.Lcd.println("M");
      break;
    case fsHigh:
      M5.Lcd.setTextColor(TFT_RED);
      M5.Lcd.println("H");
      break;      
    default:
      break;
  }
      
}


void startScan() {
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning
  if (!pBLEScan) { 
    pBLEScan = pBLEDevice->getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    // Start the scan to run for 30 seconds.
    Serial.println("Starting scan");
  } else {
    Serial.println("Re-starting scan");
  }
  pBLEScan->clearResults();  // clear previous results to force redetection  
  pBLEScan->start(5, false);  

}

void setFanSpeed(){

  char presses = 0;
  
  // always start with the fan off
  digitalWrite(FAN_OUT,BUTTON_PRESSED);
  delay(3000);
  digitalWrite(FAN_OUT,BUTTON_NOT_PRESSED);
  
  switch (fanSpeed) {
    case fsOff:
      presses = 0;
      break;
    case fsLow:
      presses = 3;      
      break;
    case fsMed:
      presses = 2;
      break;
    case fsHigh:
      presses = 1;
      break;      
    default:
      // statements
      break;
  }

  for (int i=1; i <= presses; i++) {
    delay(250);
    digitalWrite(FAN_OUT,BUTTON_PRESSED);
    delay(250);
    digitalWrite(FAN_OUT,BUTTON_NOT_PRESSED);    
  }
}
  
void setup() {

  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_YELLOW);
  M5.Lcd.setCursor(0,0,2);
  M5.Lcd.print("BLE Client");
  
  //Start serial communication
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");

  // Setup display
  M5.begin();

  // Init fan control pin
  pinMode(FAN_OUT, OUTPUT);
  digitalWrite(FAN_OUT,BUTTON_NOT_PRESSED);  
  
  //Init BLE device
  pBLEDevice = new BLEDevice;
  pBLEDevice->init("");


}

void loop() {

  if (myState == stIdle) {
    startScan();
    myState == stScanning;
  }
  
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer(*pServerAddress)) {
      Serial.println("We are now connected to the BLE Server.");
      //Activate the Notify property of each Characteristic
      heartRateCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);
      connected = true;
      myState = stConnected;
      pBLEScan->stop();
      doConnect = false;
    } else {
      Serial.println("We have failed to connect to the server; Restart your device to scan for nearby BLE server again.");
    }
    
  }

  //if new temperature readings are available, display it
  if (newHeartRate || (myOldState != myState)) {
    updateDisplay();
  }

  // control fan based on heart rate when connected, otherwise turn or keep it off
  // but don't change the fan more than once every 15s
  if (myState == stConnected)  {
    if (newHeartRate){
      newHeartRate = false;
      if (msSinceFanChange > 15000) {
        if (heartRate > 130)
          fanSpeed = fsHigh;
        else if (heartRate > 115)
          fanSpeed = fsMed;
        else if (heartRate > 100)
          fanSpeed = fsLow;
        else
          fanSpeed = fsOff;
      }
    }
  }
  else
    fanSpeed = fsOff;
 
  Serial.print(msSinceFanChange);
  Serial.print(" ");
  Serial.print(fanLastSpeed);
  Serial.print(" ");
  Serial.println(fanSpeed);
  
  // change fan speed if needed
  if (fanLastSpeed != fanSpeed)  {
    updateDisplay();
    setFanSpeed();
    msSinceFanChange = 0;
    fanLastSpeed = fanSpeed;
 
  }

  myOldState = myState;
  
  delay(1000); // Delay a second between loops.
}
