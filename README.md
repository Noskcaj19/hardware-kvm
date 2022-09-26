# Hardware KVM

[Barrier](https://github.com/debauchee/barrier) (or [input-leap](https://github.com/input-leap/input-leap)) are excellent programs implementing a "Software KVM" allowing a single mouse and keyboard to be seamlessly shared between multiple computers with the software installed.

However, sometimes you may not want to or be unable to install the client software on a computer.

This project allows Barrier to control computers with _no software installed_ by using a microcontroller that presents itself as a USB mouse and keyboard to the computer.  Most Barrier commands can be implemented using an absolute mode mouse and keyboard.


**Be warned, there are many bugs**

**This is a hack**, it can never be perfect due to limitations of USB.  Also, the code is hacked together.  There are many bugs I know of, and many I don't.

Fundamentally, it's just a USB mouse and keyboard, so it's not likely to kill your computer, but use this hack at your own risk.

# Usage

## Hardware

An ESP32 is required to be the virtual USB HID mouse and keyboard. 

Use `esp-idf` to build and flash the code in the `esp_device` folder to the ESP32.

The code assumes a `ESP32-S3-DevKitC-1` and is configured to use both onboard USB ports.  Other form factors can work, but the usb device pinouts will have to be changed.

## Host software

The `host_driver` folder must be compiled and run.

The host software does not need to be run on a computer running the barrier software, but it must be able to connect to the barrier server and be connected to the serial port of the ESP32.

Running it on the Barrier server will reduce latency.

* `BARRIER_SERVER` - defaults to `localhost:24800`
* `BARRIER_SCREEN_WIDTH` - defaults to `2560`
* `BARRIER_SCREEN_HEIGHT` - defaults to `1600`
* `KVM_SERIAL_ADDRESS` - defaults to `/dev/ttyUSB0`
* `KVM_SERIAL_BAUD` - defaults to `460800`
* `BARRIER_DEVICE_NAME` - defaults to `Hardware Barrier`

Then configure the client in the server like any other client.

# Features and limitations
Supports mouse movement (absolute mode only, although relative mode is in theory possible) and 6KRO keyboard


Does not (currently) support host to client clipboard, but can be implemented by intercepting the paste shortcut and entering the clipboard as text.

Cannot and will not support client to host clipboard or drag and drop in either direction.
This is both a limitation of a USB mouse and keyboard being input only devices and that this project should be able to be used in security sensitive situations, where data exfiliation is not acceptable.

Does not (currently) support TLS encryption.  **It is assumed the local network is trustworthy**.

The Barrier protocol parserusing an ESP32 opens the possiblity of using WIFI.

# Architecture

The host driver connects to the Barrier server as a client and connects to the ESP32 over serial UART.

When the Barrier server starts sending KVM commands to the host driver the host driver forwards these commands over serial to the ESP32.

The ESP32 reads these commands from serial and then performs the actions as a USB HID mouse and keyboard.

## Future improvements
An ESP32 was specifically chosen for it's wifi connectivity so that the device could be fully self contained and connect directly to the Barrier server, but I'm lazy and haven't gotten around to it.
On the other hand, direct connection to the host computer has much lower latency, so it's likely a good idea to keep the functionality.

The Barrier protocol parser should be able to be compiled and run on the ESP32.  This would allow the ESP32 to connect directly to the Barrier server over the network, and could reduce the complexity of the host program.