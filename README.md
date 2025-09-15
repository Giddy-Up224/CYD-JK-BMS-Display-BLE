# CYD-JK-BMS-Display-BLE

This project is a modified version of [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms), rewritten using [LVGL](https://lvgl.io/) so I can view the data and control the BMS directly on a [CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).




Current stage:
- Continuing work on converting to LVGL instead of the webserver/webpage.
- SOC, Voltage, and Current are displayed on home screen
- Cell Voltages and Wire Resistances are available from there respective screens

Future Features:
- List and select available devices
- Show device name on home screen header
- Variable screen dimming using onboard LDR (option selectable from settings)


## Build Instructions

I installed the PlatformIO VS Code extension. (hereafter referred to as PIO) 
Please follow PIO's installation instructions for best results.

### Setup



#### PIO setup

I had to do the following on Windows to add ```intelhex``` to my PIO venv packages.
```bash
%USERPROFILE%\.platformio\penv\Scripts\python.exe -m pip install intelhex
```

#### Arduino IDE 2.x

You could try [this guy's script](https://runningdeveloper.com/blog/platformio-project-to-arduino-ide/) for
converting this PlatformIO project to Arduino.

Bottom line is that the ```lvgl_conf/lv_conf.h``` needs to go into the ```Documents/Arduino/libraries``` folder,
and ```TFT_eSPI/User_Setup.h``` needs to go into the ```Documents/Arduino/libraries/TFT_eSPI``` folder.
If you're using Linux you probably already know all about it. 😉

Otherwise I'll let you figure it out. <i><small>You can always Google or ChatGPT things, you know.</small></i>

## Notes

Works with my JK-B1A8S10P BMS

## Credits

Thanks to [@peff74](https://github.com/peff74/), FINALLY a solution that actually has the backend working out of the box!