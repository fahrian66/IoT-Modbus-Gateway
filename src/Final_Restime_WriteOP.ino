#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <ModbusRTU.h>
#include <ModbusIP_ESP8266.h>

// Firebase Configuration
#define API_KEY "AIzaSyAxbQbIbWQpT8aAyy3umKzN_v7Sythu23I"
#define DATABASE_URL "https://iot-modbus66-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Pin Definitions
#define RXD2 16
#define TXD2 17
#define SD_CS_PIN 10
#define LED_NODE1 4
#define LED_NODE2 5
#define LED_NODE3 6

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (7 * 3600) // WIB (UTC+7)
#define DAYLIGHT_OFFSET_SEC 0

// Modbus TCP Port
const uint16_t MODBUS_TCP_PORT = 502;

// Global Objects
RTC_DS3231 rtc;
ModbusRTU mb;
ModbusIP mbIP;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Timing Variables
unsigned long sendDataPrevMillis = 0;
unsigned long readConfigPrevMillis = 0;
unsigned long lastWiFiReconnectMillis = 0;
unsigned long lastFirebaseMillis = 0;
unsigned long lastWiFiConfigCheckMillis = 0;

// State Variables
bool signupOK = false;
bool lastWiFiStatus = false;
bool firebaseInitialized = false;
String currentSSID = "";
String currentPassword = "";

// Node Type Enum
enum NodeType {
  NODE_1,
  NODE_2,
  NODE_3
};

// Read State Enum
enum ReadState {
  READ_NODE1,
  READ_NODE2,
  READ_NODE3,
  IDLE
};

// Node 1 Configuration (Dynamic)
int slaveId1 = 1;
int functionCode1 = 3;
int startAddress1 = 0;
int endAddress1 = 15;
uint16_t registerData1[16];
uint16_t lastRegisterData1[16];
int numRegisters1 = 0;
int lastSlaveId1 = -1;
int lastFunctionCode1 = -1;
int lastStartAddress1 = -1;
int lastEndAddress1 = -1;

// Node 2 Configuration (Fixed)
const int slaveId2 = 2;
const int functionCode2 = 4;
const int startAddress2 = 0;
const int endAddress2 = 9;
uint16_t registerData2[10];
uint16_t lastRegisterData2[10];
const int numRegisters2 = 10;
int lastSlaveId2 = -1;
int lastFunctionCode2 = -1;
int lastStartAddress2 = -1;
int lastEndAddress2 = -1;

// Node 3 Configuration (Fixed)
const int slaveId3 = 3;
const int functionCode3 = 4;
const int startAddress3 = 1;
const int endAddress3 = 2;
uint16_t registerData3[2];
uint16_t lastRegisterData3[2];
const int numRegisters3 = 2;
int lastSlaveId3 = -1;
int lastFunctionCode3 = -1;
int lastStartAddress3 = -1;
int lastEndAddress3 = -1;

// Sensor Data Variables
float temperature = 0.0;
float humidity = 0.0;
float voltage = 0.0;
float current = 0.0;
float power = 0.0;
float energy = 0.0;
float frequency = 0.0;
float powerFactor = 0.0;

// State Management
ReadState currentState = READ_NODE1;
unsigned long lastSlaveReadMillis = 0;
unsigned long lastSameDataMillis = 0;
unsigned long lastSameDataMillis3 = 0;
unsigned long lastSameDataMillis1 = 0;
bool isDataSame = false;
bool isDataSame3 = false;
bool isDataSame1 = false;
const unsigned long readInterval = 3000;
const unsigned long sameDataTimeout = 30000;
bool isModbusBusy = false;
bool sdCardInitialized = false;

// Serial Configuration
long baudRate = 9600;
int dataBits = 8;
String parity = "NONE"; // NONE, EVEN, ODD
int stopBits = 1;
bool serialConfigChanged = false;

// Response Time Variables
unsigned long node1ResponseTime = 0;
unsigned long node2ResponseTime = 0;
unsigned long node3ResponseTime = 0;

// Function to sync RTC with NTP
void syncTimeWithNTP() {
  Serial.println("Attempting to sync time with NTP...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm timeinfo;
  int retryCount = 0;
  while (!getLocalTime(&timeinfo) && retryCount < 10) {
    Serial.println("Waiting for NTP sync...");
    delay(500);
    retryCount++;
  }

  if (getLocalTime(&timeinfo)) {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
    Serial.println("RTC synchronized with NTP!");
    DateTime now = rtc.now();
    Serial.printf("Current RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  } else {
    Serial.println("Failed to obtain time from NTP after multiple retries.");
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting to default time as NTP failed.");
      rtc.adjust(DateTime(2025, 5, 23, 16, 0, 0));
    } else {
      Serial.println("RTC still has power, continuing with its current time.");
    }
  }
}

// Initialize Firebase
void initializeFirebase() {
  if (firebaseInitialized) return;
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase SignUp Success");
    signupOK = true;
  } else {
    Serial.printf("SignUp Error: %s\n", config.signer.signupError.message.c_str());
  }
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  firebaseInitialized = true;
}

// Load serial configuration from SD card
void loadSerialConfigFromSD() {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping load serial config");
    return;
  }

  File file = SD.open("/serial_config.txt", FILE_READ);
  if (!file) {
    Serial.println("No serial_config.txt found, using default serial config");
    return;
  }

  String dataLine = file.readString();
  file.close();

  int firstComma = dataLine.indexOf(',');
  int secondComma = dataLine.indexOf(',', firstComma + 1);
  int thirdComma = dataLine.indexOf(',', secondComma + 1);
  if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
    baudRate = dataLine.substring(0, firstComma).toInt();
    dataBits = dataLine.substring(firstComma + 1, secondComma).toInt();
    parity = dataLine.substring(secondComma + 1, thirdComma);
    parity.trim();
    stopBits = dataLine.substring(thirdComma + 1).toInt();

    Serial.printf("Loaded serial config from SD: BaudRate=%ld, DataBits=%d, Parity=%s, StopBits=%d\n",
                  baudRate, dataBits, parity.c_str(), stopBits);
  } else {
    Serial.println("Invalid serial config format in SD, using default config");
  }
}

void saveSerialConfigToSD() {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping save serial config");
    return;
  }

  File configFile = SD.open("/serial_config.txt", FILE_WRITE);
  if (!configFile) {
    Serial.println("Failed to open serial_config.txt for writing");
    return;
  }

  String configData = String(baudRate) + "," + String(dataBits) + "," + parity + "," + String(stopBits);
  if (configFile.println(configData)) {
    Serial.println("Serial config saved to SD: " + configData);
  } else {
    Serial.println("Failed to save serial config to SD");
  }
  configFile.close();
}

// Apply serial configuration to Serial2
void applySerialConfig() {
  Serial2.end(); // Stop the current Serial2 communication

  uint32_t serialConfig = SERIAL_8N1; // Default configuration
  if (dataBits == 7) {
    if (parity == "NONE") serialConfig = stopBits == 1 ? SERIAL_7N1 : SERIAL_7N2;
    else if (parity == "EVEN") serialConfig = stopBits == 1 ? SERIAL_7E1 : SERIAL_7E2;
    else if (parity == "ODD") serialConfig = stopBits == 1 ? SERIAL_7O1 : SERIAL_7O2;
  } else if (dataBits == 8) {
    if (parity == "NONE") serialConfig = stopBits == 1 ? SERIAL_8N1 : SERIAL_8N2;
    else if (parity == "EVEN") serialConfig = stopBits == 1 ? SERIAL_8E1 : SERIAL_8E2;
    else if (parity == "ODD") serialConfig = stopBits == 1 ? SERIAL_8O1 : SERIAL_8O2;
  }

  Serial2.begin(baudRate, serialConfig, RXD2, TXD2);
  mb.begin(&Serial2);
  Serial.printf("Applied serial config: BaudRate=%ld, DataBits=%d, Parity=%s, StopBits=%d\n",
                baudRate, dataBits, parity.c_str(), stopBits);
}

// Read serial configuration from Firebase
void readSerialConfigFromFirebase() {
  if (!firebaseInitialized || !Firebase.ready() || !signupOK) {
    Serial.println("Firebase not initialized or not ready, skipping readSerialConfig");
    return;
  }

  bool configValid = true;
  long newBaudRate;
  int newDataBits;
  String newParity;
  int newStopBits;

  if (Firebase.RTDB.getInt(&fbdo, "SerialConfig/BaudRate")) {
    newBaudRate = fbdo.intData();
    Serial.printf("BaudRate from Firebase: %ld\n", newBaudRate);
  } else {
    configValid = false;
  }

  if (Firebase.RTDB.getInt(&fbdo, "SerialConfig/DataBits")) {
    newDataBits = fbdo.intData();
    Serial.printf("DataBits from Firebase: %d\n", newDataBits);
  } else {
    configValid = false;
  }

  if (Firebase.RTDB.getString(&fbdo, "SerialConfig/Parity")) {
    newParity = fbdo.stringData();
    Serial.printf("Parity from Firebase: %s\n", newParity.c_str());
  } else {
    configValid = false;
  }

  if (Firebase.RTDB.getInt(&fbdo, "SerialConfig/StopBits")) {
    newStopBits = fbdo.intData();
    Serial.printf("StopBits from Firebase: %d\n", newStopBits);
  } else {
    configValid = false;
  }

  if (configValid) {
    bool validConfig = true;
    if (newBaudRate < 300 || newBaudRate > 115200) {
      Serial.println("Invalid BaudRate, using previous value");
      validConfig = false;
    }
    if (newDataBits < 7 || newDataBits > 8) {
      Serial.println("Invalid DataBits, using previous value");
      validConfig = false;
    }
    if (newParity != "NONE" && newParity != "EVEN" && newParity != "ODD") {
      Serial.println("Invalid Parity, using previous value");
      validConfig = false;
    }
    if (newStopBits != 1 && newStopBits != 2) {
      Serial.println("Invalid StopBits, using previous value");
      validConfig = false;
    }

    if (validConfig && (newBaudRate != baudRate || newDataBits != dataBits || newParity != parity || newStopBits != stopBits)) {
      baudRate = newBaudRate;
      dataBits = newDataBits;
      parity = newParity;
      stopBits = newStopBits;
      serialConfigChanged = true;

      saveSerialConfigToSD();
      applySerialConfig();
    } else if (!validConfig) {
      Serial.println("Using previous serial configuration due to invalid values");
    }
  }
}

// Load WiFi configuration from SD card
void loadWiFiConfigFromSD() {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping load WiFi config");
    return;
  }

  File file = SD.open("/wifi_config.txt", FILE_READ);
  if (!file) {
    Serial.println("No wifi_config.txt found, using default WiFi config");
    return;
  }

  String dataLine = file.readString();
  file.close();

  int commaIndex = dataLine.indexOf(',');
  if (commaIndex != -1) {
    currentSSID = dataLine.substring(0, commaIndex);
    currentPassword = dataLine.substring(commaIndex + 1);
    currentSSID.trim();
    currentPassword.trim();
    Serial.printf("Loaded WiFi config from SD: SSID=%s, Password=%s\n", currentSSID.c_str(), currentPassword.c_str());
  } else {
    Serial.println("Invalid WiFi config format in SD, using default config");
  }
}

// Save WiFi configuration to SD card
void saveWiFiConfigToSD(String ssid, String password) {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping save WiFi config");
    return;
  }

  File configFile = SD.open("/wifi_config.txt", FILE_WRITE);
  if (!configFile) {
    Serial.println("Failed to open wifi_config.txt for writing");
    return;
  }

  String configData = ssid + "," + password;
  if (configFile.println(configData)) {
    Serial.println("WiFi config saved to SD: " + configData);
  } else {
    Serial.println("Failed to save WiFi config to SD");
  }
  configFile.close();
}

// Read WiFi configuration from Firebase
void readWiFiConfigFromFirebase() {
  if (!firebaseInitialized || !Firebase.ready() || !signupOK) {
    Serial.println("Firebase not initialized or not ready, skipping readWiFiConfig");
    return;
  }

  String newSSID;
  String newPassword;

  if (Firebase.RTDB.getString(&fbdo, "WiFiConfig/SSID")) {
    newSSID = fbdo.stringData();
    Serial.printf("New SSID from Firebase: %s\n", newSSID.c_str());
  } else {
    return;
  }

  if (Firebase.RTDB.getString(&fbdo, "WiFiConfig/Password")) {
    newPassword = fbdo.stringData();
    Serial.printf("New Password from Firebase: %s\n", newPassword.c_str());
  } else {
    return;
  }

  if (newSSID.length() > 0 && newPassword.length() > 0 && (newSSID != currentSSID || newPassword != currentPassword)) {
    saveWiFiConfigToSD(newSSID, newPassword);

    currentSSID = newSSID;
    currentPassword = newPassword;
    WiFi.disconnect();
    WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
    Serial.println("Attempting to connect to new WiFi: " + currentSSID);
    lastWiFiReconnectMillis = millis();
  }
}

// Save Node 1 configuration to SD card
void saveNode1ConfigToSD() {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping save Node 1 config");
    return;
  }

  File configFile = SD.open("/node1_config.txt", FILE_WRITE);
  if (!configFile) {
    Serial.println("Failed to open node1_config.txt for writing");
    return;
  }

  String configData = String(slaveId1) + "," + String(functionCode1) + "," + String(startAddress1) + "," + String(endAddress1);
  if (configFile.println(configData)) {
    Serial.println("Node 1 config saved to SD: " + configData);
  } else {
    Serial.println("Failed to save Node 1 config to SD");
  }
  configFile.close();
}

// Load Node 1 configuration from SD card
void loadNode1ConfigFromSD() {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping load Node 1 config");
    return;
  }

  File file = SD.open("/node1_config.txt", FILE_READ);
  if (!file) {
    Serial.println("No node1_config.txt found, using default Node 1 config");
    return;
  }

  String dataLine = file.readString();
  file.close();

  int firstComma = dataLine.indexOf(',');
  int secondComma = dataLine.indexOf(',', firstComma + 1);
  int thirdComma = dataLine.indexOf(',', secondComma + 1);
  if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
    slaveId1 = dataLine.substring(0, firstComma).toInt();
    functionCode1 = dataLine.substring(firstComma + 1, secondComma).toInt();
    startAddress1 = dataLine.substring(secondComma + 1, thirdComma).toInt();
    endAddress1 = dataLine.substring(thirdComma + 1).toInt();

    numRegisters1 = endAddress1 - startAddress1 + 1;
    if (numRegisters1 < 1 || numRegisters1 > 16) {
      Serial.println("Invalid number of registers for Node 1: " + String(numRegisters1));
      numRegisters1 = 0;
    }
    Serial.printf("Loaded Node 1 config from SD: SlaveID=%d, FunctionCode=%d, StartAddress=%d, EndAddress=%d\n",
                  slaveId1, functionCode1, startAddress1, endAddress1);
  } else {
    Serial.println("Invalid Node 1 config format in SD, using default config");
  }
}

// Read Modbus configuration from Firebase for Node 1
void readConfigFromFirebase() {
  if (!firebaseInitialized || !Firebase.ready() || !signupOK) {
    Serial.println("Firebase not initialized or not ready, skipping readConfig");
    return;
  }
  bool configValid1 = true;
  int newSlaveId1, newFunctionCode1, newStartAddress1, newEndAddress1;

  if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/SlaveID")) {
    newSlaveId1 = fbdo.intData();
  } else {
    configValid1 = false;
  }

  if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/FunctionCode")) {
    newFunctionCode1 = fbdo.intData();
  } else {
    configValid1 = false;
  }

  if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/StartAddress")) {
    newStartAddress1 = fbdo.intData();
  } else {
    configValid1 = false;
  }

  if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/EndAddress")) {
    newEndAddress1 = fbdo.intData();
  } else {
    configValid1 = false;
  }

  if (configValid1) {
    int maxRegisters = 16;
    if (newEndAddress1 > newStartAddress1 && newEndAddress1 - newStartAddress1 + 1 > maxRegisters) {
      Serial.printf("Node1 EndAddress too high for Slave %d, limiting to %d\n", newSlaveId1, newStartAddress1 + maxRegisters - 1);
      newEndAddress1 = newStartAddress1 + maxRegisters - 1;
    }

    bool configChanged1 = (newSlaveId1 != lastSlaveId1 ||
                           newFunctionCode1 != lastFunctionCode1 ||
                           newStartAddress1 != lastStartAddress1 ||
                           newEndAddress1 != lastEndAddress1);

    if (configChanged1 && lastSlaveId1 != -1) {
      Serial.printf("Resetting Firebase data for Node1 Slave %d\n", lastSlaveId1);
      bool resetSuccess = true;
      for (int i = 0; i < 16; i++) {
        if (!Firebase.RTDB.setInt(&fbdo, "SlaveData/Node1/Register" + String(i), 0)) {
          Serial.println("Failed to reset Node1 Register" + String(i) + ": " + fbdo.errorReason());
          resetSuccess = false;
        }
      }
      if (!resetSuccess) {
        Serial.println("Warning: Firebase reset for Node1 failed");
      }
    }

    slaveId1 = newSlaveId1;
    functionCode1 = newFunctionCode1;
    startAddress1 = newStartAddress1;
    endAddress1 = newEndAddress1;

    lastSlaveId1 = newSlaveId1;
    lastFunctionCode1 = newFunctionCode1;
    lastStartAddress1 = newStartAddress1;
    lastEndAddress1 = newEndAddress1;

    numRegisters1 = endAddress1 - startAddress1 + 1;
    if (numRegisters1 < 1 || numRegisters1 > maxRegisters) {
      Serial.println("Invalid number of registers for Node1: " + String(numRegisters1));
      numRegisters1 = 0;
    }
    if (configChanged1) {
      Serial.println("Node1 ModbusConfig changed, resetting registerData1 buffer");
      memset(registerData1, 0, sizeof(registerData1));
      for (int i = 0; i < 16; i++) {
        mbIP.Hreg(i, registerData1[i]);
      }
      delay(100);
    }

    saveNode1ConfigToSD(); 
  }
}

// Read data from Modbus slaves
bool readSlaveData(NodeType node) {
  int slaveId, functionCode, startAddress, numRegisters;
  uint16_t* registerData;
  int hregOffset;
  size_t registerSize;

  switch (node) {
    case NODE_1:
      slaveId = slaveId1;
      functionCode = functionCode1;
      startAddress = startAddress1;
      numRegisters = numRegisters1;
      registerData = registerData1;
      hregOffset = 0;
      registerSize = sizeof(registerData1);
      break;

    case NODE_2:
      slaveId = slaveId2;
      functionCode = functionCode2;
      startAddress = startAddress2;
      numRegisters = numRegisters2;
      registerData = registerData2;
      hregOffset = 18;
      registerSize = sizeof(registerData2);
      break;

    case NODE_3:
      slaveId = slaveId3;
      functionCode = functionCode3;
      startAddress = startAddress3;
      numRegisters = numRegisters3;
      registerData = registerData3;
      hregOffset = 32;
      registerSize = sizeof(registerData3);
      break;

    default:
      Serial.println("Invalid Node Type");
      return false;
  }

  if (numRegisters <= 0) {
    Serial.printf("No valid registers to read for Node%d\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3));
    return false;
  }

  if (isModbusBusy) {
    Serial.printf("Modbus bus sedang sibuk, pembacaan Node%d ditunda.\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3));
    return false;
  }
  isModbusBusy = true;

  Serial.printf("Reading Node%d Slave ID: %d, Function Code: %d, Start Address: %d, Num Registers: %d\n",
                node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3), slaveId, functionCode, startAddress, numRegisters);

  unsigned long requestStartTime = millis(); // Catat waktu pengiriman permintaan
  bool success = false;
  if (functionCode == 3) {
    success = mb.readHreg(slaveId, startAddress, registerData, numRegisters);
  } else if (functionCode == 4) {
    success = mb.readIreg(slaveId, startAddress, registerData, numRegisters);
  } else {
    Serial.printf("Invalid Function Code for Node%d: %d\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3), functionCode);
    isModbusBusy = false;
    return false;
  }
  unsigned long responseTime = millis() - requestStartTime; // Hitung response time

  if (success) {
    Serial.printf("Node%d Response Time: %lu ms\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3), responseTime);

    if (node == NODE_1) {
      node1ResponseTime = responseTime;
      uint16_t* lastRegisterData = lastRegisterData1;
      
      if (memcmp(registerData, lastRegisterData, registerSize) == 0) {
        if (!isDataSame1) {
          lastSameDataMillis1 = millis();
          isDataSame1 = true;
          Serial.println("Node1 data is same as previous, starting timeout");
        }
        if (millis() - lastSameDataMillis1 >= sameDataTimeout) {
          Serial.println("Node1 data unchanged for 30 seconds, resetting to 0");
          memset(registerData, 0, registerSize);
          for (int i = 0; i < numRegisters; i++) {
            mbIP.Hreg(i + hregOffset, 0);
          }
          digitalWrite(LED_NODE1, LOW);
          Serial.println("Node1 LED: OFF (data unchanged)");
          lastSameDataMillis1 = 0;
          isDataSame1 = false;
        }
      } else {
        Serial.println("Node1 data changed, updating lastRegisterData");
        memcpy(lastRegisterData, registerData, registerSize);
        lastSameDataMillis1 = 0;
        isDataSame1 = false;
        
        for (int i = 0; i < numRegisters; i++) {
          mbIP.Hreg(i + hregOffset, registerData[i]);
        }
        
        bool anyNonZero = false;
        for (int i = 0; i < numRegisters; i++) {
          if (registerData[i] > 0) {
            anyNonZero = true;
            break;
          }
        }
        digitalWrite(LED_NODE1, anyNonZero ? HIGH : LOW);
        Serial.printf("Node1 LED: %s\n", anyNonZero ? "ON" : "OFF");
      }
      
      // Kirim response time ke Modbus TCP untuk Node 1 dalam dua integer
      unsigned long integerPart = responseTime / 1; // Bagian utama (ms)
      unsigned long decimalPart = ((responseTime % 1) * 100); // Dua digit desimal
      mbIP.Hreg(hregOffset + 16, (uint16_t)integerPart); // Register untuk bagian utama
      mbIP.Hreg(hregOffset + 17, (uint16_t)decimalPart); // Register untuk dua digit desimal
    } else {
      uint16_t* lastRegisterData = (node == NODE_2) ? lastRegisterData2 : lastRegisterData3;
      unsigned long* lastSameDataMillisPtr = (node == NODE_2) ? &lastSameDataMillis : &lastSameDataMillis3;
      bool* isDataSamePtr = (node == NODE_2) ? &isDataSame : &isDataSame3;

      if (node == NODE_2) {
        node2ResponseTime = responseTime;
        // Kirim response time ke Modbus TCP untuk Node 2 dalam dua integer
        unsigned long integerPart = responseTime / 1; // Bagian utama (ms)
        unsigned long decimalPart = ((responseTime % 1) * 100); // Dua digit desimal
        mbIP.Hreg(hregOffset + 12, (uint16_t)integerPart); // Register untuk bagian utama
        mbIP.Hreg(hregOffset + 13, (uint16_t)decimalPart); // Register untuk dua digit desimal
      } else {
        node3ResponseTime = responseTime;
        // Kirim response time ke Modbus TCP untuk Node 3 dalam dua integer
        unsigned long integerPart = responseTime / 1; // Bagian utama (ms)
        unsigned long decimalPart = ((responseTime % 1) * 100); // Dua digit desimal
        mbIP.Hreg(hregOffset + 4, (uint16_t)integerPart); // Register untuk bagian utama
        mbIP.Hreg(hregOffset + 5, (uint16_t)decimalPart); // Register untuk dua digit desimal
      }

      if (memcmp(registerData, lastRegisterData, registerSize) == 0) {
        if (!(*isDataSamePtr)) {
          *lastSameDataMillisPtr = millis();
          *isDataSamePtr = true;
          Serial.printf("Node%d data is same as previous, starting timeout\n", node == NODE_2 ? 2 : 3);
        }
        if (millis() - *lastSameDataMillisPtr >= sameDataTimeout) {
          Serial.printf("Node%d data unchanged for 30 seconds, resetting to 0\n", node == NODE_2 ? 2 : 3);
          memset(registerData, 0, registerSize);
          if (node == NODE_2) {
            voltage = 0.0;
            current = 0.0;
            power = 0.0;
            energy = 0.0;
            frequency = 0.0;
            powerFactor = 0.0;
            for (int i = 0; i < 6; i++) {
              mbIP.Hreg(i * 2 + hregOffset, 0);
              mbIP.Hreg(i * 2 + hregOffset + 1, 0);
            }
          } else {
            temperature = 0.0;
            humidity = 0.0;
            for (int i = 0; i < 2; i++) {
              mbIP.Hreg(28 + i * 2, 0);
              mbIP.Hreg(28 + i * 2 + 1, 0);
            }
          }
          *lastSameDataMillisPtr = 0;
          *isDataSamePtr = false;
          
          int ledPin = (node == NODE_2) ? LED_NODE2 : LED_NODE3;
          digitalWrite(ledPin, LOW);
          Serial.printf("Node%d LED: OFF (data unchanged)\n", node == NODE_2 ? 2 : 3);
        }
      } else {
        Serial.printf("Node%d data changed, updating lastRegisterData\n", node == NODE_2 ? 2 : 3);
        memcpy(lastRegisterData, registerData, registerSize);
        *lastSameDataMillisPtr = 0;
        *isDataSamePtr = false;

        if (node == NODE_2) {
          voltage = registerData[0] / 10.0;
          current = (registerData[1] + (registerData[2] << 16)) / 1000.0;
          power = (registerData[3] + (registerData[4] << 16)) / 10.0;
          energy = (registerData[5] + (registerData[6] << 16)) * 1.0;
          frequency = registerData[7] / 10.0;
          powerFactor = registerData[8] / 100.0;

          mbIP.Hreg(hregOffset + 0, (uint16_t)voltage);
          mbIP.Hreg(hregOffset + 1, (uint16_t)((voltage - (uint16_t)voltage) * 100));
          mbIP.Hreg(hregOffset + 2, (uint16_t)current);
          mbIP.Hreg(hregOffset + 3, (uint16_t)((current - (uint16_t)current) * 1000));
          mbIP.Hreg(hregOffset + 4, (uint16_t)power);
          mbIP.Hreg(hregOffset + 5, (uint16_t)((power - (uint16_t)power) * 100));
          mbIP.Hreg(hregOffset + 6, (uint16_t)energy);
          mbIP.Hreg(hregOffset + 7, (uint16_t)((energy - (uint16_t)energy) * 100));
          mbIP.Hreg(hregOffset + 8, (uint16_t)frequency);
          mbIP.Hreg(hregOffset + 9, (uint16_t)((frequency - (uint16_t)frequency) * 100));
          mbIP.Hreg(hregOffset + 10, (uint16_t)powerFactor);
          mbIP.Hreg(hregOffset + 11, (uint16_t)((powerFactor - (uint16_t)powerFactor) * 100));

          bool anyNonZero = (voltage > 0 || current > 0 || power > 0 || energy > 0 || frequency > 0 || powerFactor > 0);
          digitalWrite(LED_NODE2, anyNonZero ? HIGH : LOW);
          Serial.printf("Node2 LED: %s\n", anyNonZero ? "ON" : "OFF");
        } else {
          temperature = registerData[0] / 10.0;
          humidity = registerData[1] / 10.0;
          Serial.printf("Node3 calculated: Temperature=%.1f, Humidity=%.1f\n", temperature, humidity);
          mbIP.Hreg(hregOffset + 0, (uint16_t)temperature);
          mbIP.Hreg(hregOffset + 1, (uint16_t)((temperature - (uint16_t)temperature) * 100));
          mbIP.Hreg(hregOffset + 2, (uint16_t)humidity);
          mbIP.Hreg(hregOffset + 3, (uint16_t)((humidity - (uint16_t)humidity) * 100));

          bool anyNonZero = (temperature > 0 || humidity > 0);
          digitalWrite(LED_NODE3, anyNonZero ? HIGH : LOW);
          Serial.printf("Node3 LED: %s\n", anyNonZero ? "ON" : "OFF");
        }
      }
    }

    // Tulis data ke SD card setelah pembacaan berhasil
    writeToSD();
  } else {
    Serial.printf("Failed to read Node%d slave data, Response Time: %lu ms\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3), responseTime);
  }
  isModbusBusy = false;
  return success;
}

// Send all node data to Firebase in one operation with a single timestamp
void sendAllDataToFirebase() {
  if (!firebaseInitialized || !Firebase.ready() || !signupOK) {
    Serial.println("Firebase not initialized or not ready, skipping sendAllDataToFirebase");
    return;
  }

  String timestamp = getTimestamp();
  FirebaseJson json;

  if (numRegisters1 > 0) {
    FirebaseJson node1Json;
    for (int i = 0; i < numRegisters1; i++) {
      node1Json.set("Register" + String(i), registerData1[i]);
    }
    node1Json.set("ResponseTime", node1ResponseTime);
    json.set("Node1", node1Json);
  }

  if (numRegisters2 >= 9) {
    FirebaseJson node2Json;
    node2Json.set("Voltage", voltage);
    node2Json.set("Current", current);
    node2Json.set("Power", power);
    node2Json.set("Energy", energy);
    node2Json.set("Frequency", frequency);
    node2Json.set("PowerFactor", powerFactor);
    node2Json.set("ResponseTime", node2ResponseTime);
    json.set("Node2", node2Json);
  }

  if (numRegisters3 >= 2) {
    FirebaseJson node3Json;
    node3Json.set("Temperature", temperature);
    node3Json.set("Humidity", humidity);
    node3Json.set("ResponseTime", node3ResponseTime);
    json.set("Node3", node3Json);
  }

  json.set("Timestamp", timestamp);

  if (Firebase.RTDB.setJSON(&fbdo, "SlaveData", &json)) {
    Serial.println("Sent all node data: Timestamp=" + timestamp);
  } else {
    Serial.println("Failed to send all node data: " + fbdo.errorReason());
  }
}

// Write data to Modbus slave
bool writeModbus(int writeSlaveId, int functionCode, int address, uint16_t value) {
  Serial.printf("Writing Modbus: Slave ID: %d, Function Code: %d, Address: %d, Value: %u\n",
                writeSlaveId, functionCode, address, value);

  if (isModbusBusy) {
    Serial.println("Modbus bus sedang sibuk, penulisan ditunda.");
    return false;
  }
  isModbusBusy = true;

  bool success = false;
  if (functionCode == 5) {
    success = mb.writeCoil(writeSlaveId, address, value != 0);
  } else if (functionCode == 6) {
    success = mb.writeHreg(writeSlaveId, address, value);
  } else {
    Serial.println("Invalid Function Code: " + String(functionCode));
    isModbusBusy = false;
    return false;
  }

  isModbusBusy = false;

  if (success) {
    Serial.println("Write Modbus successful");
  } else {
    Serial.println("Failed to write Modbus");
  }

  return success;
}

// Read write commands from Firebase
void readWriteCommandsFromFirebase() {
  if (!firebaseInitialized || !Firebase.ready() || !signupOK) {
    Serial.println("Firebase not initialized or not ready, skipping readWriteCommands");
    return;
  }
  bool writeValid = true;
  int writeSlaveId, writeFunctionCode, address, value;

  if (Firebase.RTDB.getInt(&fbdo, "ModbusWrite/Command/SlaveID")) {
    writeSlaveId = fbdo.intData();
    Serial.printf("Write SlaveID: %d\n", writeSlaveId);
  } else {
    writeValid = false;
  }

  if (writeValid && Firebase.RTDB.getInt(&fbdo, "ModbusWrite/Command/FunctionCode")) {
    writeFunctionCode = fbdo.intData();
    Serial.printf("Write FunctionCode: %d\n", writeFunctionCode);
  } else {
    writeValid = false;
  }

  if (writeValid && Firebase.RTDB.getInt(&fbdo, "ModbusWrite/Command/Address")) {
    address = fbdo.intData();
    Serial.printf("Write Address: %d\n", address);
  } else {
    writeValid = false;
  }

  if (writeValid && Firebase.RTDB.getInt(&fbdo, "ModbusWrite/Command/Value")) {
    value = fbdo.intData();
    Serial.printf("Write Value: %d\n", value);
  } else {
    writeValid = false;
  }

  if (writeValid) {
    if (writeSlaveId > 0 && address >= 0 && (writeFunctionCode == 5 || writeFunctionCode == 6)) {
      if (writeFunctionCode == 5 && value >= 0 && value <= 1) {
        if (writeModbus(writeSlaveId, writeFunctionCode, address, value)) {
          if (Firebase.RTDB.deleteNode(&fbdo, "ModbusWrite/Command")) {
            Serial.println("Write command deleted");
          } else {
            Serial.println("Failed to delete Write command: " + fbdo.errorReason());
          }
        }
      } else if (writeFunctionCode == 6 && value >= 0 && value <= 65535) {
        if (writeModbus(writeSlaveId, writeFunctionCode, address, value)) {
          if (Firebase.RTDB.deleteNode(&fbdo, "ModbusWrite/Command")) {
            Serial.println("Write command deleted");
          } else {
            Serial.println("Failed to delete Write command: " + fbdo.errorReason());
          }
        }
      } else {
        Serial.println("Invalid Write parameters (value out of range)");
      }
    } else {
      Serial.println("Invalid Write parameters");
    }
  } else if (fbdo.errorReason() != "path not exist") {
    Serial.println("Failed to read Write command: " + fbdo.errorReason());
  }
}

// Setup function
void setup() {
  Serial.begin(115200);

  // Initialize LED pins
  pinMode(LED_NODE1, OUTPUT);
  pinMode(LED_NODE2, OUTPUT);
  pinMode(LED_NODE3, OUTPUT);
  digitalWrite(LED_NODE1, LOW);
  digitalWrite(LED_NODE2, LOW);
  digitalWrite(LED_NODE3, LOW);

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.println("Make sure RTC is connected and working!");
  } else {
    DateTime now = rtc.now();
    Serial.printf("RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  }

  // Initialize SD Card
  Serial.println("Initializing SD Card");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed! Check SPI pins (MISO=13, MOSI=11, SCK=12, CS=10)");
    sdCardInitialized = false;
  } else {
    Serial.println("SD Card initialized");
    sdCardInitialized = true;
    loadSerialConfigFromSD();
    loadWiFiConfigFromSD();
    loadNode1ConfigFromSD();
  }

  // Initialize Serial2 with initial configuration
  Serial2.begin(baudRate, SERIAL_8N1, RXD2, TXD2);
  applySerialConfig();

  // Start WiFi with SD or default config
  if (currentSSID.length() == 0) {
    currentSSID = "xixty"; // Default SSID
    currentPassword = "1sampai8"; // Default Password
    saveWiFiConfigToSD(currentSSID, currentPassword);
  }
  WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
  Serial.println("Starting WiFi connection attempt (non-blocking)");

  // Sync time if RTC lost power
  if (rtc.lostPower()) {
    Serial.println("RTC lost power on startup. Attempting initial NTP sync.");
    syncTimeWithNTP();
  }

  // Initialize Modbus RTU
  mb.master();
  delay(100);

  // Initialize Modbus TCP
  mbIP.begin();
  Serial.printf("Modbus TCP Server started at Port: %d\n", MODBUS_TCP_PORT);

  // Initialize Modbus TCP registers
  for (int i = 0; i < 16; i++) {
    mbIP.addHreg(i, registerData1[i]);
  }
  for (int i = 0; i < 6; i++) {
    mbIP.addHreg(i * 2 + 16, 0);
    mbIP.addHreg(i * 2 + 17, 0);
  }
  for (int i = 0; i < 5; i++) {
    mbIP.addHreg(28 + i * 2, 0);
    mbIP.addHreg(28 + i * 2 + 1, 0);
  }

  // Clear last register data
  memset(lastRegisterData1, 0, sizeof(lastRegisterData1));
  memset(lastRegisterData2, 0, sizeof(lastRegisterData2));
  memset(lastRegisterData3, 0, sizeof(lastRegisterData3));
}

// Get current timestamp as string
String getTimestamp() {
  DateTime now = rtc.now();
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  return String(buffer);
}

// Write data to SD card
void writeToSD() {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping write");
    return;
  }

  String timestamp = getTimestamp();
  String fileName = "/data_all_" + String(rtc.now().year()) + "-" + String(rtc.now().month()) + "-" + String(rtc.now().day()) + ".csv";
  File file = SD.open(fileName, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file: " + fileName);
    return;
  }

  if (file.size() == 0) {
    file.println("Timestamp,Register0,Register1,Register2,Register3,Register4,Register5,Register6,Register7,Register8,Register9,Register10,Register11,Register12,Register13,Register14,Register15,Voltage,Current,Power,Energy,Frequency,PowerFactor,Temperature,Humidity,Node1ResponseTime,Node2ResponseTime,Node3ResponseTime");
  }

  String data = timestamp;
  for (int i = 0; i < 16; i++) {
    data += "," + String(i < numRegisters1 ? registerData1[i] : 0);
  }
  data += "," + String(numRegisters2 >= 9 ? voltage : 0, 1) + "," + String(numRegisters2 >= 9 ? current : 0, 3) + "," +
          String(numRegisters2 >= 9 ? power : 0, 1) + "," + String(numRegisters2 >= 9 ? energy : 0, 0) + "," +
          String(numRegisters2 >= 9 ? frequency : 0, 1) + "," + String(numRegisters2 >= 9 ? powerFactor : 0, 2);
  data += "," + String(numRegisters3 >= 2 ? temperature : 0, 1) + "," + String(numRegisters3 >= 2 ? humidity : 0, 1);
  // Tambahkan response time dalam format biasa ke SD card
  data += "," + String(node1ResponseTime) + "," + String(node2ResponseTime) + "," + String(node3ResponseTime);

  if (file.println(data)) {
    Serial.println("All node data written to main file: " + fileName + ", Timestamp: " + timestamp);
  } else {
    Serial.println("Failed to write all node data to: " + fileName);
  }
  file.close();

  if (WiFi.status() != WL_CONNECTED) {
    saveOfflaneDataToSD(data);
  }
}

// Save last data to SD card when WiFi is disconnected
void saveOfflaneDataToSD(String data) {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping save last data");
    return;
  }

  String fileName = "/offline_data_all.csv";
  File file = SD.open(fileName, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open temporary file: " + fileName);
    return;
  }

  if (file.size() == 0) {
    file.println("Timestamp,Register0,Register1,Register2,Register3,Register4,Register5,Register6,Register7,Register8,Register9,Register10,Register11,Register12,Register13,Register14,Register15,Voltage,Current,Power,Energy,Frequency,PowerFactor,Temperature,Humidity,Node1ResponseTime,Node2ResponseTime,Node3ResponseTime");
    Serial.println("All node header written to: " + fileName);
  }

  if (file.println(data)) {
    String timestamp = data.substring(0, data.indexOf(','));
    String logData = timestamp;
    logData += ", Registers: " + data.substring(data.indexOf(',') + 1, data.lastIndexOf(','));
    logData += ", Voltage: " + String(numRegisters2 >= 9 ? voltage : 0, 1) + ", Current: " + String(numRegisters2 >= 9 ? current : 0, 3) +
               ", Power: " + String(numRegisters2 >= 9 ? power : 0, 1) + ", Energy: " + String(numRegisters2 >= 9 ? energy : 0, 0) +
               ", Frequency: " + String(numRegisters2 >= 9 ? frequency : 0, 1) + ", PowerFactor: " + String(numRegisters2 >= 9 ? powerFactor : 0, 2) +
               ", Temperature: " + String(numRegisters3 >= 2 ? temperature : 0, 1) + ", Humidity: " + String(numRegisters3 >= 2 ? humidity : 0, 1);
    Serial.println("All node data appended to temporary file: " + fileName + ", Data: " + logData);
  } else {
    Serial.println("Failed to append all node data to: " + fileName);
  }
  file.close();
}

// Send last data from SD card to Firebase
void sendOfflaneDataFromSD() {
  if (!sdCardInitialized) {
    Serial.println("SD Card not initialized, skipping send last data");
    return;
  }

  if (!firebaseInitialized || !Firebase.ready() || !signupOK) {
    Serial.println("Firebase not initialized or not ready, skipping send last data");
    return;
  }

  File file = SD.open("/offline_data_all.csv", FILE_READ);
  if (!file) {
    Serial.println("No /offline_data_all.csv or cannot open");
    return;
  }

  if (file.available()) {
    file.readStringUntil('\n');
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() == 0) continue;
    char lineBuf[512];
    line.toCharArray(lineBuf, sizeof(lineBuf));
    char *token = strtok(lineBuf, ",");
    int index = 0;
    String timestamp;
    uint16_t tempRegisterData1[16] = {0};
    float tempVoltage = 0.0, tempCurrent = 0.0, tempPower = 0.0, tempEnergy = 0.0;
    float tempFrequency = 0.0, tempPowerFactor = 0.0;
    float tempTemperature = 0.0, tempHumidity = 0.0;
    unsigned long tempNode1ResponseTime = 0;
    unsigned long tempNode2ResponseTime = 0;
    unsigned long tempNode3ResponseTime = 0;

    while (token != NULL && index < 28) {
      if (index == 0) timestamp = String(token);
      else if (index >= 1 && index <= 16) tempRegisterData1[index - 1] = atoi(token);
      else if (index == 17) tempVoltage = atof(token);
      else if (index == 18) tempCurrent = atof(token);
      else if (index == 19) tempPower = atof(token);
      else if (index == 20) tempEnergy = atof(token);
      else if (index == 21) tempFrequency = atof(token);
      else if (index == 22) tempPowerFactor = atof(token);
      else if (index == 23) tempTemperature = atof(token);
      else if (index == 24) tempHumidity = atof(token);
      else if (index == 25) tempNode1ResponseTime = atol(token);
      else if (index == 26) tempNode2ResponseTime = atol(token);
      else if (index == 27) tempNode3ResponseTime = atol(token);
      token = strtok(NULL, ",");
      index++;
    }

    FirebaseJson json;

    if (numRegisters1 > 0) {
      FirebaseJson node1Json;
      for (int i = 0; i < numRegisters1; i++) {
        node1Json.set("Register" + String(i), tempRegisterData1[i]);
      }
      node1Json.set("ResponseTime", tempNode1ResponseTime);
      json.set("Node1", node1Json);

      for (int i = 0; i < numRegisters1; i++) {
        mbIP.Hreg(i, tempRegisterData1[i]);
      }

      mbIP.Hreg(16, (uint16_t)(tempNode1ResponseTime / 1)); // Integer part
      mbIP.Hreg(16, (uint16_t)((tempNode1ResponseTime % 1) * 100)); // Decimal part

      bool anyNonZero = false;
      for (int i = 0; i < numRegisters1; i++) {
        if (tempRegisterData1[i] > 0) {
          anyNonZero = true;
          break;
        }
      }
      digitalWrite(LED_NODE1, anyNonZero ? HIGH : LOW);
      Serial.printf("Node1 LED updated: %s (after sending last data)\n", anyNonZero ? "ON" : "OFF");
    }

    if (numRegisters2 >= 9) {
      FirebaseJson node2Json;
      node2Json.set("Voltage", tempVoltage);
      node2Json.set("Current", tempCurrent);
      node2Json.set("Power", tempPower);
      node2Json.set("Energy", tempEnergy);
      node2Json.set("Frequency", tempFrequency);
      node2Json.set("PowerFactor", tempPowerFactor);
      node2Json.set("ResponseTime", tempNode2ResponseTime);
      json.set("Node2", node2Json);

      int hregOffset = 18;
      mbIP.Hreg(hregOffset + 0, (uint16_t)tempVoltage);
      mbIP.Hreg(hregOffset + 1, (uint16_t)((tempVoltage - (uint16_t)tempVoltage) * 100));
      mbIP.Hreg(hregOffset + 2, (uint16_t)tempCurrent);
      mbIP.Hreg(hregOffset + 3, (uint16_t)((tempCurrent - (uint16_t)tempCurrent) * 1000));
      mbIP.Hreg(hregOffset + 4, (uint16_t)tempPower);
      mbIP.Hreg(hregOffset + 5, (uint16_t)((tempPower - (uint16_t)tempPower) * 100));
      mbIP.Hreg(hregOffset + 6, (uint16_t)tempEnergy);
      mbIP.Hreg(hregOffset + 7, (uint16_t)((tempEnergy - (uint16_t)tempEnergy) * 100));
      mbIP.Hreg(hregOffset + 8, (uint16_t)tempFrequency);
      mbIP.Hreg(hregOffset + 9, (uint16_t)((tempFrequency - (uint16_t)tempFrequency) * 100));
      mbIP.Hreg(hregOffset + 10, (uint16_t)tempPowerFactor);
      mbIP.Hreg(hregOffset + 11, (uint16_t)((tempPowerFactor - (uint16_t)tempPowerFactor) * 100));
      mbIP.Hreg(hregOffset + 12, (uint16_t)(tempNode2ResponseTime / 1)); // Integer part
      mbIP.Hreg(hregOffset + 13, (uint16_t)((tempNode2ResponseTime % 1) * 100)); // Decimal part

      bool anyNonZero = (tempVoltage > 0 || tempCurrent > 0 || tempPower > 0 || tempEnergy > 0 || tempFrequency > 0 || tempPowerFactor > 0);
      digitalWrite(LED_NODE2, anyNonZero ? HIGH : LOW);
      Serial.printf("Node2 LED updated: %s (after sending last data)\n", anyNonZero ? "ON" : "OFF");
    }

    if (numRegisters3 >= 2) {
      FirebaseJson node3Json;
      node3Json.set("Temperature", tempTemperature);
      node3Json.set("Humidity", tempHumidity);
      node3Json.set("ResponseTime", tempNode3ResponseTime);
      json.set("Node3", node3Json);

      int hregOffset = 28;
      mbIP.Hreg(hregOffset + 0, (uint16_t)tempTemperature);
      mbIP.Hreg(hregOffset + 1, (uint16_t)((tempTemperature - (uint16_t)tempTemperature) * 100));
      mbIP.Hreg(hregOffset + 2, (uint16_t)tempHumidity);
      mbIP.Hreg(hregOffset + 3, (uint16_t)((tempHumidity - (uint16_t)tempHumidity) * 100));
      mbIP.Hreg(hregOffset + 4, (uint16_t)(tempNode3ResponseTime / 1)); // Integer part
      mbIP.Hreg(hregOffset + 5, (uint16_t)((tempNode3ResponseTime % 1) * 100)); // Decimal part

      bool anyNonZero = (tempTemperature > 0 || tempHumidity > 0);
      digitalWrite(LED_NODE3, anyNonZero ? HIGH : LOW);
      Serial.printf("Node3 LED updated: %s (after sending last data)\n", anyNonZero ? "ON" : "OFF");
    }

    json.set("Timestamp", timestamp);

    if (Firebase.RTDB.setJSON(&fbdo, "SlaveData", &json)) {
      Serial.println("Sent all last data: Timestamp=" + timestamp);
    } else {
      Serial.println("Failed to send all last data: " + fbdo.errorReason());
      file.close();
      SD.remove("/offline_data_all.csv");
      return;
    }
  }
  file.close();
  SD.remove("/offline_data_all.csv");
  Serial.println("All data sent to Firebase and Modbus TCP");
}

// Main loop
void loop() {
  mb.task();
  mbIP.task();
  yield();

  bool currentWiFiStatus = (WiFi.status() == WL_CONNECTED);

  if (currentWiFiStatus != lastWiFiStatus) {
    if (currentWiFiStatus) {
      Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
      syncTimeWithNTP();
      initializeFirebase();
      if (firebaseInitialized && Firebase.ready() && signupOK) {
        Firebase.RTDB.setString(&fbdo, "WifiIP", WiFi.localIP().toString());
        sendOfflaneDataFromSD();
      }
    } else {
      Serial.println("WiFi disconnected");
      firebaseInitialized = false;
      signupOK = false;
    }
    lastWiFiStatus = currentWiFiStatus;
  }

  if (!currentWiFiStatus && millis() - lastWiFiReconnectMillis >= 15000) {
    lastWiFiReconnectMillis = millis();
    Serial.println("Attempting WiFi reconnect...");
    WiFi.reconnect();
  }

  if (millis() - lastWiFiConfigCheckMillis >= 10000) {
    lastWiFiConfigCheckMillis = millis();
    if (currentWiFiStatus && firebaseInitialized && Firebase.ready() && signupOK) {
      readWiFiConfigFromFirebase();
    }
  }

  if (millis() - readConfigPrevMillis >= 10000) {
    readConfigPrevMillis = millis();
    if (currentWiFiStatus) {
      readConfigFromFirebase();
      readSerialConfigFromFirebase();
      readWriteCommandsFromFirebase();
    }
  }

  if (millis() - lastSlaveReadMillis >= readInterval) {
    lastSlaveReadMillis = millis();
    switch (currentState) {
      case READ_NODE1:
        readSlaveData(NODE_1);
        currentState = READ_NODE2;
        break;
      case READ_NODE2:
        readSlaveData(NODE_2);
        currentState = READ_NODE3;
        break;
      case READ_NODE3:
        readSlaveData(NODE_3);
        currentState = READ_NODE1;
        break;
      case IDLE:
        currentState = READ_NODE1;
        break;
    }
  }

  if (millis() - lastFirebaseMillis >= 10000) {
    lastFirebaseMillis = millis();
    if (currentWiFiStatus && firebaseInitialized && Firebase.ready() && signupOK) {
      sendAllDataToFirebase();
    }
  }
}