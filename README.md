<h1>Modbus Gateway</h1>
<div align="justify">
A Modbus Gateway is a device or system that functions as a bridge between Modbus RTU and Modbus TCP. The primary purpose of this gateway is to enable integration between devices using different protocols, allowing systems to work seamlessly without the need for changes to existing hardware. In its implementation, the Modbus Gateway acts as:

1. A Modbus RTU master, which reads data from slave devices using RS485 communication.
2. A Modbus TCP server, which provides this data to other systems (e.g., SCADA or Node-RED) over an IP network.

The Modbus Gateway typically maps each register from the RTU protocol to a TCP register address, allowing the monitoring system to access the data without having to re-read it from the slave device.</div>
<p><img src="assets/ModbusGateway.jpg" width="400"></p>

## Overview
* [Block Diagram](#block-diagram)
* [Circuit Design](#circuit-design)
* [Register Mapping](#register-mapping)
* [Website](#website)
* [How to Use](#how-to-use)

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

## Register Mapping
