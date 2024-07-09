# Alternative firmware for Nice BiDi-WiFi module
Unlike the original firmware, this one works completely standalone - it needs no mobile phone app, accounts or cloud services.

It acts as a simple web server that sits on your local Wi-Fi network and allows you to configure, observe and control the unit it's plugged into. In addition, it also acts as UDP server which proxies packets communicated on the T4 bus to/from your local network. This feature can be easily used for example to log communication on the bus or to control the unit with scripts.

For now, it has been tested only with RBA3R10 control unit in Robus 400 sliding gate motor, but it should (at least partially) work with other devices with T4 bus too.

The knowledge presented here is not official information, it's based on reverse-engineering of hardware and firmware.

## Build
The firmware source is a standard Arduino IDE 2 project. It's meant to be compiled and uploaded to the Nice BiDi-WiFi module - power rail and the signals for programming are exposed by test points on the PCB. The module is based on ESP32-WROOM-32E, so even if you don't have one, you can easily build your own (see the schematics also included in this repository).