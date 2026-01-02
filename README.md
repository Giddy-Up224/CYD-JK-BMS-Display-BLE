# CYD-JK-BMS-Display-BLE

This project is based on [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms). I'm using [LVGL](https://lvgl.io/) so I can view the data and control the BMS directly on a [CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) instead of using the web page interface.


Current development branch: [dev_redo_ui](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/tree/dev_redo_ui)

Current feature list:
- LVGL graphics instead of the webserver/webpage.
- SOC, Voltage, and Current are displayed on home screen
- Cell Voltages and Wire Resistances are available from their respective screens
- Minimal display options for orientation and display brightness. (Not saved on reboot as of now)
- Connect a single BMS for monitoring.

Where I'm going:
- branch: `dev_redo_ui`
- It is working OK with a single BMS. It is lacking a lot of the features at this point yet.
- Check TODO for `update_bms_display()` in `screens.cpp`
- I was thinking of having the UI cycle through all the connected BMSes and showing the connected BMS name with its data

Future Features:
- List and select available devices
    - Change from static MAC to storing MAC addresses in Preferences (to save devices through power cycle) and using scan results for populating available device list.
    - If no devices are stored in memory, do not scan until the scan page is loaded.
    - If there are devices saved in Preferences, try connecting to them on boot.
    - Allow forgetting devices (delete from Preferences)
- Show device name on home screen header (Or check out idea below)
- Variable screen dimming using onboard LDR (option selectable from settings)
- Save display orientation selection to Preferences
- Screen timeout
    - Allow setting a timeout and saving to Preferences
- Add ability to show multiple devices by scrolling through main screen. (Or check out idea below)
- Add voltage based SOC calculation in case Jikong's SOC calculations are as bad as the rest of China's SOC calculations.
- Aggregate data from all connected devices.

## Build Instructions

I installed the PlatformIO VS Code extension.
Please follow PIO's installation instructions for best results.

If you have an internet connection, you should be able to simply compile and upload.

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

## Help

If you have any questions or simply want to share how you've used this repo, please head over to [Discussions](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/discussions/1) where you can introduce yourself and post your project in the [show and tell](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/discussions/categories/show-and-tell) channel.

## Credits

Thanks to [@peff74](https://github.com/peff74/), FINALLY a solution that actually has the backend working out of the box!