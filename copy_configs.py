# This is a script to copy custom configuration files for TFT_eSPI and LVGL
# into the appropriate library directories before the build process. 
# (Because they MUST be in correct location at build time)
# It is called from PlatformIO automatically. It is defined in platformio.ini as:
# extra_scripts = copy_configs.py

import shutil
import os

Import("env")

custom_tft_setup = "lib/TFT_eSPI/User_Setup.h"
custom_lv_conf = "lib/lvgl_conf/lv_conf.h"

tft_dest = os.path.join(env['PROJECT_DIR'], ".pio", "libdeps", env['PIOENV'], "TFT_eSPI", "User_Setup.h")
lvgl_dest = os.path.join(env['PROJECT_DIR'], ".pio", "libdeps", env['PIOENV'], "lvgl", "lv_conf.h")

def before_build(source, target, env):
    if os.path.exists(custom_tft_setup):
        shutil.copyfile(custom_tft_setup, tft_dest)
        print(f"Copied {custom_tft_setup} to {tft_dest}")
    if os.path.exists(custom_lv_conf):
        shutil.copyfile(custom_lv_conf, lvgl_dest)
        print(f"Copied {custom_lv_conf} to {lvgl_dest}")

env.AddPreAction("buildprog", before_build)