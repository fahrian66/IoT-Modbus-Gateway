<h1>IoT Modbus Gateway</h1>
<div align="justify">
A Modbus Gateway is a device or system that functions as a bridge between Modbus RTU and Modbus TCP. The primary purpose of this gateway is to enable integration between devices using different protocols, allowing systems to work seamlessly without the need for changes to existing hardware. In its implementation, the Modbus Gateway acts as:

1. A Modbus RTU master, which reads data from slave devices using RS485 communication.
2. A Modbus TCP server, which provides this data to other systems (e.g., SCADA or Node-RED) over an IP network.

The Modbus Gateway typically maps each register from the RTU protocol to a TCP register address, allowing the monitoring system to access the data without having to re-read it from the slave device.</div>
<p><img src="assets/FullCircuit.jpg"></p>

## Overview
* [Block Diagram](#block-diagram)
* [Circuit Design](#circuit-design)
* [Register Mapping](#register-mapping)
* [Dashboard](#dashboard)
* [Contact](#contact)

## Block Diagram
<p><img src="assets/BlockDiagram.jpg"></p>
<div align="justify">
In Figure above, the system is designed using RS485 as the communication channel with the slave. Slave readings on the Universal Nodes can be performed flexibly by configuring the Slave ID, Function, or Register address parameters to be read. Meanwhile, readings on the PZEM-004T and XY-MD02 are static and cannot be changed. The reading results are then sent in two outputs: a website-based dashboard via Firebase and a dashboard using the Node-RED platform.</div>
<br></br>
<p><img src="assets/MainBlock.jpg"></p>
<div align="justify">
The image above shows an overview of the Modbus Gateway workflow in a Modbus communication system. The Modbus Gateway receives data from the Slave via Modbus RTU format with RS485. The ESP32-S3 microcontroller acts as the Modbus Gateway, receiving the data and sending it to the Dashboard. The data acquisition process begins with the Modbus Gateway sending a Frame Request to the Slave using the Modbus RTU Frame Request format. </div>

## Circuit Design
<p><img src="assets/Schematic.png"></p>

The pin configuration of each component is explained in the following table.

|No.    |Pin ESP32-S3 | Components    |
| :---: | ---         | ---           |
| 1     | SDA         | RTC           |
| 2     | SCL         | RTC           |
| 3     | CS          | SDCard Module |
| 4     | MOSI        | SDCard Module |
| 5     | CLK         | SDCard Module |
| 6     | MISO        | SDCard Module |
| 7     | RX          | TTL to RS485  |
| 8     | TX          | TTL to RS485  |

## Register Mapping
### 1. Node Universal
<div align="justify">
  Throughout the system, Universal Nodes can be reconfigured based on parameters such as Slave ID, Function Code, Start Address, and End Address. The addition of the End Address parameter represents an improvement over the previous system, which only used the number of Registers as a reference for reading length. This change was made to make it easier for users to determine the parameters of the Register address to be read. The purpose of creating Universal Nodes is to increase the system's flexibility in reading Modbus-based sensors. Therefore, the Universal Nodes in the Firebase Dashboard still contain raw data, while the data in the Universal Nodes will be processed on the Node-RED platform. </div>
  
* Slave ID = 1-257
* Function Code = 3/4
* Start Address = Flex
* End Address = Start Address + 1

### 2. PZEM-004T
<div align="justify">
The PZEM-004T is an AC communication module designed to measure various electrical parameters in real time. Unlike conventional meters, this module does not have a visual display. This module has the ability to measure several key electrical parameters, including:</div>

1. Voltage in the range of 80–260 V  
2. Current in the range of 0–100 A
3. Active power up to 23 kW
4. Frequency in the range of 45–65 Hz
5. Active energy in the range of 0–9999.99 kWh, where the energy value can be reset using software.
6. Power factor from 0.00 to 1.00.

The following is the Register on the PZEM-004T that will be read.

| Address | Function Code | Description                | Resolution                           |
|:-------:|:-------------:|----------------------------|--------------------------------------|
| 0       | 0x04          | Voltage value              | 1 LSB corresponds to 0.1 V           |
| 1       | 0x04          | Current value low 16 bits  | 1 LSB corresponds to 0.001 A         |
| 2       | 0x04          | Current value high 16 bits | 1 LSB corresponds to 0.001 A         |
| 3       | 0x04          | Power value low 16 bits    | 1 LSB corresponds to 0.1 W           |
| 4       | 0x04          | Power value high 16 bits   | 1 LSB corresponds to 0.1 W           |
| 5       | 0x04          | Energy value low 16 bits   | 1 LSB corresponds to 1 Wh            |
| 6       | 0x04          | Energy value high 16 bits  | 1 LSB corresponds to 1 Wh            |
| 7       | 0x04          | Frequency value            | 1 LSB corresponds to 0.1 Hz          |
| 8       | 0x04          | Power factor value         | 1 LSB corresponds to 0.01            |
| 9       | 0x04          | Alarm status               | 0xFFFF is alarm, 0x0000 is not alarm |
| 1       | 0x06          | Power alarm threshold      | 1 LSB corresponds to 1 W             |
| 2       | 0x06          | Modbus-RTU address         | The range is 0x0001 ~ 0x00F7         |

### 3. XY-MD02
<div align="justify">
The XY-MD02 is a temperature and humidity sensor with an RS485 communication interface based on the Modbus RTU protocol. This sensor is designed as an industrial device with high reliability and accuracy.The XY-MD02 sensor is capable of measuring two main parameters</div>

1. Temperature: in degrees Celsius (°C), with an accuracy of ±0.3°C,
2. Humidity: in %RH, with an accuracy of ±3%RH.

The following is the Register on the XY-MD02 that will be read.

| Parameter       | Function Code | Address | Resolution                   |
|:---------------:|:-------------:|:-------:|:----------------------------:|
| Temperature     | 0x04          | 0x001   | 1 LSB corresponds to 0.1 °C  |
| Humidity        | 0x04          | 0x002   | 1 LSB corresponds to 0.1%    |
| Device Address  | 0x06          | 0x101   | The range is 0x0001 ~ 0x00F7 |

### TCP Address
<div align="justify">
All registers read from the three slaves will be reformatted into Modbus TCP so they can be received and processed by Node-RED. This process is performed by mapping each parameter value obtained via Modbus RTU to the corresponding Modbus TCP register address. The primary purpose of using Modbus TCP is to enable industrial system access via TCP/IP, where data can be monitored or integrated with other industrial systems using IP addresses.
Given that some parameters have decimal values, a mapping and data formatting process is performed to maintain the accuracy of these values. To support the representation of decimal values in Modbus TCP, sensor data is formatted by separating integer and decimal values into two separate registers. This approach ensures that each parameter displayed in Node-RED remains accurate, especially for parameters from the PZEM-004T and XY-MD02 slaves, which require fractional value representation.</div>

The following shows the Register Mapping in Modbus TCP format.

| No  | Slave          | Parameter              | TCP Address |
|:---:|:--------------:|:----------------------:|:-----------:|
| 1   | Node Universal | 0                      | 0           |
| 2   | Node Universal | 1                      | 1           |
| 3   | Node Universal | 2                      | 2           |
| 4   | Node Universal | 3                      | 3           |
| 5   | Node Universal | 4                      | 4           |
| 6   | Node Universal | 5                      | 5           |
| 7   | Node Universal | 6                      | 6           |
| 8   | Node Universal | 7                      | 7           |
| 9   | Node Universal | 8                      | 8           |
| 10  | Node Universal | 9                      | 9           |
| 11  | Node Universal | 10                     | 10          |
| 12  | Node Universal | 11                     | 11          |
| 13  | Node Universal | 12                     | 12          |
| 14  | Node Universal | 13                     | 13          |
| 15  | Node Universal | 14                     | 14          |
| 16  | Node Universal | 15                     | 15          |
| 17  | Node Universal | Response Time 1 (Int)  | 16          |
| 18  | Node Universal | Response Time 1 (Dec)  | 17          |
| 19  | PZEM           | Voltage (Int)          | 18          |
| 20  | PZEM           | Voltage (Dec)          | 19          |
| 21  | PZEM           | Current (Int)          | 20          |
| 22  | PZEM           | Current (Dec)          | 21          |
| 23  | PZEM           | Power (Int)            | 22          |
| 24  | PZEM           | Power (Dec)            | 23          |
| 25  | PZEM           | Energy (Int)           | 24          |
| 26  | PZEM           | Energy (Dec)           | 25          |
| 27  | PZEM           | Frequency (Int)        | 26          |
| 28  | PZEM           | Frequency (Dec)        | 27          |
| 29  | PZEM           | Power Factor (Int)     | 28          |
| 30  | PZEM           | Power Factor (Dec)     | 29          |
| 31  | PZEM           | Response Time 2 (Int)  | 30          |
| 32  | PZEM           | Response Time 2 (Dec)  | 31          |
| 33  | XY-MD02        | Temperature (Int)      | 32          |
| 34  | XY-MD02        | Temperature (Dec)      | 33          |
| 35  | XY-MD02        | Humidity (Int)         | 34          |
| 36  | XY-MD02        | Humidity (Dec)         | 35          |
| 37  | XY-MD02        | Response Time 3 (Dec)  | 36          |
| 38  | XY-MD02        | Response Time 3 (Int)  | 37          |

## Dashboard
<div align="justify">
The Dashboard is built by integrating Firebase, which acts as a data monitoring tool and provides remote configuration control features. Through this Dashboard, users can configure the Slave ID, Serial configuration, and Wi-Fi SSID and password. This Dashboard obtains data by retrieving JSON data from the Firebase Realtime Database. To access this data, the Dashboard makes a call to the Firebase Realtime Database address, which is composed of an API Key, Database URL, and Website URL.</div>

### Node Universal
<p><img src="assets/NodeUniversal.png"></p>

### PZEM-004T
<p><img src="assets/PZEM.png"></p>

### XY-MD02
<p><img src="assets/XY.png"></p>

### Write Function
<p><img src="assets/writefc.png"></p>

### Device Settings
<p><img src="assets/settings.png"></p>

## Contact

**Fahrian M Rafli**

* GitHub: [fahrian66](https://github.com/fahrian66)
* Instagram: [ryan.carlsenn](https://www.instagram.com/ryan.carlsenn)
* LinkedIn: [Fahrian M. Rafli](https://www.linkedin.com/in/fahrian-m-rafli-274608269/)
