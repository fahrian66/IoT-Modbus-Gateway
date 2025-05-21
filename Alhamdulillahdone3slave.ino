#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <ModbusRTU.h>
#include <ModbusIP_ESP8266.h>

#define WIFI_SSID "KOS GEBANG 62"
#define WIFI_PASSWORD "gebangputih62"
#define API_KEY "AIzaSyAxbQbIbWQpT8aAyy3umKzN_v7Sythu23I"
#define DATABASE_URL "https://iot-modbus66-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define RXD2 16
#define TXD2 17

ModbusRTU mb;
ModbusIP mbIP;
const uint16_t MODBUS_TCP_PORT = 502;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
unsigned long readConfigPrevMillis = 0;
unsigned long readWriteCommandsPrevMillis = 0;
bool signupOK = false;

enum NodeType {
  NODE_1,
  NODE_2,
  NODE_3
};

enum ReadState {
  READ_NODE1,
  READ_NODE2,
  READ_NODE3,
  IDLE
};

int slaveId1 = 1;
int functionCode1 = 3;
int startAddress1 = 0;
int endAddress1 = 15;
uint16_t registerData1[16];
int numRegisters1 = 0;
int lastSlaveId1 = -1;
int lastFunctionCode1 = -1;
int lastStartAddress1 = -1;
int lastEndAddress1 = -1;

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

float temperature; // Untuk Node 3 (SHT20)
float humidity;    // Untuk Node 3 (SHT20)
float voltage;     // Untuk Node 2
float current;     // Untuk Node 2
float power;
float energy;
float frequency;
float powerFactor;

ReadState currentState = READ_NODE1;
unsigned long lastSlaveReadMillis = 0;
unsigned long lastFirebaseMillis = 0;
unsigned long lastSameDataMillis = 0;
unsigned long lastSameDataMillis3 = 0;
bool isDataSame = false;
bool isDataSame3 = false;
const unsigned long readInterval = 3000;
const unsigned long sameDataTimeout = 10000;
bool isModbusBusy = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  String ipAddress = WiFi.localIP().toString();
  Serial.println("\nConnected with IP: " + ipAddress);

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

  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.setString(&fbdo, "WifiIP", ipAddress)) {
      Serial.println("Wi-Fi IP written to WifiIP: " + ipAddress);
    } else {
      Serial.println("Failed to write WifiIP: " + fbdo.errorReason());
    }
  }

  mb.master();
  mb.begin(&Serial2);
  delay(100);

  mbIP.begin();
  Serial.printf("Modbus TCP Server started at IP: %s, Port: %d\n",
                WiFi.localIP().toString().c_str(), MODBUS_TCP_PORT);

  for (int i = 0; i < 16; i++) {
    mbIP.addHreg(i, registerData1[i]);
  }
  for (int i = 0; i < 6; i++) {
    mbIP.addHreg(i * 2 + 16, 0);
    mbIP.addHreg(i * 2 + 17, 0);
  }
  for (int i = 0; i < 4; i++) {
    mbIP.addHreg(28 + i, 0); // Inisialisasi register 28-31
  }

  memset(lastRegisterData2, 0, sizeof(lastRegisterData2));
  memset(lastRegisterData3, 0, sizeof(lastRegisterData3));
}

void readConfigFromFirebase() {
  if (Firebase.ready() && signupOK) {
    bool configValid1 = true;
    int newSlaveId1, newFunctionCode1, newStartAddress1, newEndAddress1;

    if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/SlaveID")) {
      newSlaveId1 = fbdo.intData();
      Serial.printf("Node1 SlaveID: %d\n", newSlaveId1);
    } else {
      Serial.println("Failed to read Node1 SlaveID: " + fbdo.errorReason());
      configValid1 = false;
    }

    if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/FunctionCode")) {
      newFunctionCode1 = fbdo.intData();
      Serial.printf("Node1 FunctionCode: %d\n", newFunctionCode1);
    } else {
      Serial.println("Failed to read Node1 FunctionCode: " + fbdo.errorReason());
      configValid1 = false;
    }

    if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/StartAddress")) {
      newStartAddress1 = fbdo.intData();
      Serial.printf("Node1 StartAddress: %d\n", newStartAddress1);
    } else {
      Serial.println("Failed to read Node1 StartAddress: " + fbdo.errorReason());
      configValid1 = false;
    }

    if (Firebase.RTDB.getInt(&fbdo, "ModbusConfig/EndAddress")) {
      newEndAddress1 = fbdo.intData();
      Serial.printf("Node1 EndAddress: %d\n", newEndAddress1);
    } else {
      Serial.println("Failed to read Node1 EndAddress: " + fbdo.errorReason());
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
          String path = "SlaveData/Register" + String(i);
          if (Firebase.RTDB.setInt(&fbdo, path.c_str(), 0)) {
            Serial.printf("Reset %s to 0\n", path.c_str());
          } else {
            Serial.println("Failed to reset " + path + ": " + fbdo.errorReason());
            resetSuccess = false;
          }
        }
        if (!resetSuccess) {
          Serial.println("Warning: Some Firebase resets for Node1 failed");
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
    }
  }
}

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
      hregOffset = 16;
      registerSize = sizeof(registerData2);
      break;

    case NODE_3:
      slaveId = slaveId3;
      functionCode = functionCode3;
      startAddress = startAddress3;
      numRegisters = numRegisters3;
      registerData = registerData3;
      hregOffset = 28;
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

  if (success) {
    Serial.printf("Node%d Data received:\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3));
    for (int i = 0; i < numRegisters; i++) {
      Serial.printf("Node%d Register %d: %u\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3), startAddress + i, registerData[i]);
    }

    if (node == NODE_1) {
      for (int i = 0; i < numRegisters; i++) {
        mbIP.Hreg(i + hregOffset, registerData[i]);
      }
    } else {
      uint16_t* lastRegisterData = (node == NODE_2) ? lastRegisterData2 : lastRegisterData3;
      unsigned long* lastSameDataMillisPtr = (node == NODE_2) ? &lastSameDataMillis : &lastSameDataMillis3;
      bool* isDataSamePtr = (node == NODE_2) ? &isDataSame : &isDataSame3;

      if (memcmp(registerData, lastRegisterData, registerSize) == 0) {
        if (!(*isDataSamePtr)) {
          *lastSameDataMillisPtr = millis();
          *isDataSamePtr = true;
          Serial.printf("Node%d data is same as previous, starting timeout\n", node == NODE_2 ? 2 : 3);
        }
        if (millis() - *lastSameDataMillisPtr >= sameDataTimeout) {
          Serial.printf("Node%d data unchanged for 10 seconds, resetting to 0\n", node == NODE_2 ? 2 : 3);
          memset(registerData, 0, registerSize);
          if (node == NODE_2) {
            voltage = 0.0;
            current = 0.0;
            power = 0.0;
            energy = 0.0;
            frequency = 0.0;
            powerFactor = 0.0;
          } else {
            temperature = 0.0;
            humidity = 0.0;
          }
          for (int i = 0; i < numRegisters; i++) {
            mbIP.Hreg(i * 2 + hregOffset, 0);
            mbIP.Hreg(i * 2 + hregOffset + 1, 0);
          }
          *lastSameDataMillisPtr = 0;
          *isDataSamePtr = false;
        }
      } else {
        Serial.printf("Node%d data changed, updating lastRegisterData\n", node == NODE_2 ? 2 : 3);
        memcpy(lastRegisterData, registerData, registerSize);
        *lastSameDataMillisPtr = 0;
        *isDataSamePtr = false;
      }

      if (node == NODE_2) {
        voltage = registerData[0] / 10.0;
        current = (registerData[1] + (registerData[2] << 16)) / 1000.0;
        power = (registerData[3] + (registerData[4] << 16)) / 10.0;
        energy = (registerData[5] + (registerData[6] << 16)) * 1.0;
        frequency = registerData[7] / 10.0;
        powerFactor = registerData[8] / 100.0;

        mbIP.Hreg(hregOffset + 0, (int)voltage);
        mbIP.Hreg(hregOffset + 1, (int)((voltage - (int)voltage) * 100));
        mbIP.Hreg(hregOffset + 2, (int)current);
        mbIP.Hreg(hregOffset + 3, (int)((current - (int)current) * 100));
        mbIP.Hreg(hregOffset + 4, (int)power);
        mbIP.Hreg(hregOffset + 5, (int)((power - (int)power) * 100));
        mbIP.Hreg(hregOffset + 6, (int)energy);
        mbIP.Hreg(hregOffset + 7, (int)((energy - (int)energy) * 100));
        mbIP.Hreg(hregOffset + 8, (int)frequency);
        mbIP.Hreg(hregOffset + 9, (int)((frequency - (int)frequency) * 100));
        mbIP.Hreg(hregOffset + 10, (int)powerFactor);
        mbIP.Hreg(hregOffset + 11, (int)((powerFactor - (int)powerFactor) * 100));
      } else {
        temperature = registerData[0] / 10.0; // Register 1 untuk suhu
        humidity = registerData[1] / 10.0;    // Register 2 untuk kelembapan
        mbIP.Hreg(hregOffset + 0, (int)temperature);
        mbIP.Hreg(hregOffset + 1, (int)((temperature - (int)temperature) * 100));
        mbIP.Hreg(hregOffset + 2, (int)humidity);
        mbIP.Hreg(hregOffset + 3, (int)((humidity - (int)humidity) * 100));
      }
    }
  } else {
    Serial.printf("Failed to read Node%d slave data\n", node == NODE_1 ? 1 : (node == NODE_2 ? 2 : 3));
    memset(registerData, 0, registerSize);
    if (node != NODE_1) {
      if (node == NODE_2) {
        voltage = 0.0;
        current = 0.0;
        power = 0.0;
        energy = 0.0;
        frequency = 0.0;
        powerFactor = 0.0;
      } else {
        temperature = 0.0;
        humidity = 0.0;
      }
      uint16_t* lastRegisterData = (node == NODE_2) ? lastRegisterData2 : lastRegisterData3;
      memcpy(lastRegisterData, registerData, registerSize);
      unsigned long* lastSameDataMillisPtr = (node == NODE_2) ? &lastSameDataMillis : &lastSameDataMillis3;
      bool* isDataSamePtr = (node == NODE_2) ? &isDataSame : &isDataSame3;
      *lastSameDataMillisPtr = 0;
      *isDataSamePtr = false;
    }
    for (int i = 0; i < numRegisters; i++) {
      if (node == NODE_1) {
        mbIP.Hreg(i + hregOffset, registerData[i]);
      } else {
        mbIP.Hreg(i * 2 + hregOffset, 0);
        mbIP.Hreg(i * 2 + hregOffset + 1, 0);
      }
    }
  }

  isModbusBusy = false;
  return success;
}

void sendDataToFirebase1() {
  if (Firebase.ready() && signupOK && numRegisters1 > 0) {
    for (int i = 0; i < numRegisters1; i++) {
      String path = "SlaveData/Node1/Register" + String(i);
      if (Firebase.RTDB.setInt(&fbdo, path.c_str(), registerData1[i])) {
        Serial.printf("Sent %s: %u\n", path.c_str(), registerData1[i]);
      } else {
        Serial.println("Failed to send " + path + ": " + fbdo.errorReason());
      }
    }
  }
}

void sendDataToFirebase2() {
  if (Firebase.ready() && signupOK && numRegisters2 >= 9) {
    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node2/Voltage", voltage)) {
      Serial.printf("Sent SlaveData/Node2/Voltage: %.1f\n", voltage);
    } else {
      Serial.println("Failed to send SlaveData/Node2/Voltage: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node2/Current", current)) {
      Serial.printf("Sent SlaveData/Node2/Current: %.3f\n", current);
    } else {
      Serial.println("Failed to send SlaveData/Node2/Current: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node2/Power", power)) {
      Serial.printf("Sent SlaveData/Node2/Power: %.1f\n", power);
    } else {
      Serial.println("Failed to send SlaveData/Node2/Power: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node2/Energy", energy)) {
      Serial.printf("Sent SlaveData/Node2/Energy: %.0f\n", energy);
    } else {
      Serial.println("Failed to send SlaveData/Node2/Energy: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node2/Frequency", frequency)) {
      Serial.printf("Sent SlaveData/Node2/Frequency: %.1f\n", frequency);
    } else {
      Serial.println("Failed to send SlaveData/Node2/Frequency: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node2/PowerFactor", powerFactor)) {
      Serial.printf("Sent SlaveData/Node2/PowerFactor: %.2f\n", powerFactor);
    } else {
      Serial.println("Failed to send SlaveData/Node2/PowerFactor: " + fbdo.errorReason());
    }
  } else {
    Serial.println("Insufficient registers for Node2 to calculate parameters");
  }
}

void sendDataToFirebase3() {
  if (Firebase.ready() && signupOK && numRegisters3 >= 2) {
    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node3/Temperature", temperature)) {
    Serial.printf("Sent SlaveData/Node3/Temperature: %.1f\n", temperature);
    } else {
    Serial.println("Failed to send SlaveData/Node3/Temperature: " + fbdo.errorReason());
    }
    if (Firebase.RTDB.setFloat(&fbdo, "SlaveData/Node3/Humidity", humidity)) {
      Serial.printf("Sent SlaveData/Node3/Humidity: %.1f\n", humidity);
      } else {
        Serial.println("Failed to send SlaveData/Node3/Humidity: " + fbdo.errorReason());
        }
    }
}

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

void readWriteCommandsFromFirebase() {
  if (Firebase.ready() && signupOK) {
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
          Firebase.RTDB.deleteNode(&fbdo, "ModbusWrite/Command");
        }
      } else {
        Serial.println("Invalid Write parameters");
        Firebase.RTDB.deleteNode(&fbdo, "ModbusWrite/Command");
      }
    } else if (fbdo.errorReason() != "path not exist") {
      Serial.println("Failed to read Write command: " + fbdo.errorReason());
    }
  }
}

void loop() {
  mb.task();
  mbIP.task();
  yield();

  if (millis() - readConfigPrevMillis >= 5000) {
    Firebase.RTDB.setString(&fbdo, "WifiIP", WiFi.localIP().toString());
    readConfigPrevMillis = millis();
    readConfigFromFirebase();
  }

  if (millis() - readWriteCommandsPrevMillis >= 5000) {
    readWriteCommandsPrevMillis = millis();
    readWriteCommandsFromFirebase();
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

  if (Firebase.ready() && signupOK && (millis() - lastFirebaseMillis >= 5000)) {
    lastFirebaseMillis = millis();
    sendDataToFirebase1();
    sendDataToFirebase2();
    sendDataToFirebase3();
  }
}