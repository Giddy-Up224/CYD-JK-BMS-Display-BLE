# CYD-JK-BMS-Display-BLE

This project is based on [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms). I'm using [LVGL](https://lvgl.io/) so I can view the data and control the BMS directly on a [CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).




Current stage:
- Changing from static MAC to storing MAc addresses in Preferences and using scan results for
populating available device list.
    - If no devices are stored in memory, do not scan until the scan page is loaded.
    - If there are devices saved in Preferences, try connecting to them on boot.
    - Allow forgetting devices (delete from Preferences)
- Continuing work on converting to LVGL instead of the webserver/webpage.
- SOC, Voltage, and Current are displayed on home screen
- Cell Voltages and Wire Resistances are available from their respective screens

TODO:
- Figure out why `copy_configs.py` quit running at build time. It was working previously with:
*   **Commit message:** `Made config files dynamic`
*   **Commit ID:** `b0939e873625c80cb4075dd42cbb5476437747df`
*   **Use Build Flags** instead of the `copy_configs.py` as [sivar explained on the forum](https://community.platformio.org/t/compiles-ok-on-linux-and-works-compiles-ok-on-windows-but-does-not-work/52634/6?u=guidable8662)

Future Features:
- List and select available devices
- Show device name on home screen header (Or check out idea below)
- Variable screen dimming using onboard LDR (option selectable from settings)
- Add ability to show multiple devices by scrolling through main screen. (Or check out idea below)
- Add voltage based SOC calculation in case Jikong's SOC calculations are as bad as the rest of the Chinese's SOC calculations.
- Aggregate data from all connected devices.

Where I'm going:
- branch: `dev_redo_ui`
- It is working half decently.
- Check TODO for `update_bms_display()` in `screens.cpp`
- I was thinking of having the UI cycle through all the connected BMSes and showing the connected BMS name with its data


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

## Help

If you have any questions or simply want to share how you've used this repo, please head over to [Discussions](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/discussions/1) where you can introduce yourself and post your project in the [show and tell](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/discussions/categories/show-and-tell) channel.

## Credits

Thanks to [@peff74](https://github.com/peff74/), FINALLY a solution that actually has the backend working out of the box!

## References

* Here's a potentially useful library for CYD: https://github.com/rzeldent/esp32-smartdisplay