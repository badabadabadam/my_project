# Rehab-bot

Rehabilitation robot

## Prerequisites

- [Zephyr v4.1.0](https://docs.zephyrproject.org/latest/getting_started/index.html)
- Properly configured Zephyr development environment.
- Supported hardware and tools for flashing.
- [dfu-util](https://dfu-util.sourceforge.net/releases) required for flashing
- [zadig](https://zadig.akeo.ie/) required to use dfu-util on Windows 


## Supported Boards

This application has been tested and is known to work on the following boards:
- `Arduino Portenta H7` (`arduino_portenta_h7/stm32h747xx/m7`)

## Setting Up Zephyr 

To set up Zephyr, use the following commands:

```bash
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.1.0 zp_v4.1.0
cd ~/zephyrproject
west update
```

## Fetch Binary Blobs
The board Bluetooth/WiFi module requires fetching some binary blob files, to do that run the command:

```bash
cd ~/zephyrproject
west blobs fetch hal_infineon
```

## Building and Flashing

1. Clone the repository and navigate to the project directory.
2. Build the project for the desired board using the following commands:
```bash
west build -b arduino_portenta_h7/stm32h747xx/m7
```
3. Flash the firmware to the board using the following command:
```bash
west flash 
```
4. Double-click the **RST** button on the board to put it into Arduino Bootloader mode.

## Using dfu-util on Windows

Releases of the dfu-util software can be found in the [releases](https://dfu-util.sourceforge.net/releases) folder. dfu-util uses libusb 1.0 to access your device, so on Windows you have to register the device with the WinUSB driver by using [zadig](https://zadig.akeo.ie/). 

![zadig](/doc/zadig.png)

## Additional Information

For more details on configuring and using this application, refer to the [Zephyr Project Documentation](https://docs.zephyrproject.org/latest/).

