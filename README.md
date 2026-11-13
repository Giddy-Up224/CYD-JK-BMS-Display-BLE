# CYD-JK-BMS-Display-BLE

This project is based on [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms). I'm using [LVGL](https://lvgl.io/) so I can view the data and control the BMS directly on a [CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) instead of using the web page interface.


Current development branch: [dev_redo_ui](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/tree/dev_redo_ui)

Current feature list:
- LVGL graphics instead of the webserver/webpage.
- SOC, Voltage, and Current are displayed on home screen
- Cell Voltages and Wire Resistances are available from their respective screens
- Minimal display options for orientation and display brightness. (Not saved on reboot as of now)
- Scrollable device list (**More -> Devices**) to pick JK devices instead of hardcoding MAC addresses.
    - Opening the list scans for JK devices automatically; tap **Scan** to search again.
    - Tap a found device (blue) to save and connect to it. Saved devices are orange while connecting and green once connected.
    - Tap a saved device to see its model/firmware and to **Forget** it.
    - Up to 3 devices (`MAX_BMS_DEVICES` in `config.h`) are saved in Preferences and reconnected on boot.
    - If nothing is saved, the CYD doesn't scan until the device list is opened.
- Multiple devices: the main screen header shows the device being displayed, and the `>` button switches between connected devices.
- JK BMS **and JK active balancers**: the frame layout is detected automatically:
    - `JK02_32S`: newer BMS (hardware >= 11), e.g. JK-B1A8S10P, JK-PB series
    - `JK02_24S`: older BMS (hardware < 11) and JK active balancers
    - `JK04`: older JK-B2A16S style balancers (experimental, only cell voltages/resistances are decoded)
- Serial commands (115200 baud) for debugging: `scan`, `list`, `add <mac>`, `forget <mac>`.

Where I'm going:
- branch: `dev_redo_ui`
- It is working OK with a single BMS. It is lacking a lot of the features at this point yet.
- Check TODO for `update_bms_display()` in `screens.cpp`
- I was thinking of having the UI cycle through all the connected BMSes and showing the connected BMS name with its data

Future Features:
- Variable screen dimming using onboard LDR (option selectable from settings)
- Save display orientation selection to Preferences
- Screen timeout
    - Allow setting a timeout and saving to Preferences
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

JK devices are recognised in the scan by their manufacturer data (it ends with the device's own MAC address),
a name starting with `JK`, or the `FFE0` service. A device that is connected to the JK phone app won't advertise,
so close the app first.

If a device shows the wrong values, the serial log prints the detected protocol
(`detected protocol JK02_24S (hw ...)`) along with the decoded data, which is a good place to start.

## Help

If you have any questions or simply want to share how you've used this repo, please head over to [Discussions](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/discussions/1) where you can introduce yourself and post your project in the [show and tell](https://github.com/Giddy-Up224/CYD-JK-BMS-Display-BLE/discussions/categories/show-and-tell) channel.

## Credits

Thanks to [@peff74](https://github.com/peff74/), FINALLY a solution that actually has the backend working out of the box!