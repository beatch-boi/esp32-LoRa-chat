# ESP32 LoRa Chat (Prototype)

This project is a simple proof-of-concept chat system using LoRa technology for message transmission.
The LoRa module is connected to an ESP32 via UART. The ESP32 acts as a WiFi access point, allowing users
to connect via phone or PC and send messages to another device over LoRa.

## How it works

- **ESP32** runs a web server and WebSocket endpoint.
- **LoRa module** is connected to ESP32 UART2.
- **Users** connect to the ESP32 WiFi access point and open a web chat interface.
- **Messages** sent from the web interface are transmitted via LoRa to another device.
- **Incoming LoRa messages** are displayed in the web chat.

> **Note:** This is only a prototype (proof of concept).

## License

MIT License
