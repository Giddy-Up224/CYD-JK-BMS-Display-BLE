# CYD-JK-BMS-Display-BLE

This project is a modified version of [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms), rewritten for [CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).

*Thanks to @peff74, FINALLY a solution that actually works out of the box!*



⚠️🚧 Work in progress. Use at your own risk!

Next steps:
- Convert to LVGL instead of the webserver.




## Build Instructions

I installed the PlatformIO VS Code extension. (hereafter referred to as PIO) 
Please follow PIO's installation instructions for best results.

### Setup



#### PIO setup

I had to do the following on Windows to add ```intelhex``` to my PIO venv packages.
```bash
%USERPROFILE%\.platformio\penv\Scripts\python.exe -m pip install intelhex
```

## Notes

Works with my JK-B1A8S10P