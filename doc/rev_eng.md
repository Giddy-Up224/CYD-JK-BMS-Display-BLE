# Reverse engineering the JK BLE protocol

Notes on how JK BMS and JK active balancers talk over Bluetooth LE: how to capture what the JK app
does, and everything found so far. Meant to be enough to write another client (e.g. a Python tool)
without reading the firmware.

Sources: our own captures and tests, and [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms)
(`components/jk_bms_ble`), which has the register map.

## Contents

- [Capturing the JK app on Android](#capturing-the-jk-app-on-android)
- [Testing from the CYD console](#testing-from-the-cyd-console)
- [Connection](#connection)
- [Frames](#frames)
- [Writing settings](#writing-settings)
- [Frame layouts](#frame-layouts)
- [Device quirks](#device-quirks)
- [Test log](#test-log)
- [Open questions](#open-questions)
- [Python sketch](#python-sketch)

## Capturing the JK app on Android

Android can log every Bluetooth packet the phone sends and receives (the *HCI snoop log*). Do one
action per capture (or note the times), with values that are easy to spot in hex.

### Once per phone

1. **Settings → About phone**, tap **Build number** 7 times. This unlocks **Developer options**
   (under Settings → System on most phones).
2. For pulling the log over USB: install
   [Android platform tools](https://developer.android.com/tools/releases/platform-tools) (`adb`) on
   the PC, turn on **Developer options → USB debugging**, plug in the phone and accept the prompt.
   `adb devices` should list it.

### Each capture

1. **Free the JK device.** It takes one connection at a time, so the CYD (or anything else) must not
   be connected to it: power the CYD off, or send it `forget` on the console.
2. **Developer options → Enable Bluetooth HCI snoop log.** If it offers *Disabled / Filtered /
   Enabled*, pick **Enabled**: *Filtered* cuts the packet contents out.
3. **Turn Bluetooth off and on again.** The log only starts (fresh) when Bluetooth starts.
4. Open the JK app, connect to the device, and wait until the values show.
5. Do the action, and write down what you changed and roughly when. Pick values that are easy to
   find: rename to `AAAA` then `BBBB`, a trigger of `0.077` V, and so on. Doing the same action
   twice with two values makes the changing bytes obvious.
6. Wait a few seconds, disconnect in the app, then **turn Bluetooth off** (this writes out the rest
   of the log).
7. Get the log:
   - **adb:** `adb bugreport capture.zip`. Inside the zip the log is
     `FS/data/misc/bluetooth/logs/btsnoop_hci.log` on most phones (Android 10+). Older phones:
     `adb pull /sdcard/btsnoop_hci.log`.
   - **Without a PC:** Developer options → **Take bug report** → *Full report*, then share the zip
     from the notification (e.g. to Drive) and look for the same file inside it.
   - Samsung and some other brands put it elsewhere in the zip. Search it for `btsnoop`.
8. Turn the snoop log off again when you're done (it keeps growing).

### Reading the log

Open `btsnoop_hci.log` in [Wireshark](https://www.wireshark.org/). Useful display filters:

| Filter | Shows |
|---|---|
| `btatt.opcode == 0x52` | Write Commands (without response): what the JK app sends |
| `btatt.opcode == 0x12` | Write Requests (with response), in case a device uses them |
| `btatt.opcode == 0x1b` | Notifications: what the device sends back (frames come in pieces) |
| `bthci_acl.dst.bd_addr == c8:47:8c:f7:cf:ca` | One device (use its MAC) |

The payload is under *Bluetooth Attribute Protocol → Value*. Every JK request starts with
`AA 55 90 EB`, so searching for that (Edit → Find Packet → Hex value) finds them all. Right-click the
value → Copy → ...as Hex Stream to paste it somewhere.

Keep the captures (with a note of what was done) in `doc/captures/`, named after the action, e.g.
`jk-b1a16s-rename.btsnoop`.

## Testing from the CYD console

With the display connected (115200 baud, see the README), these commands help test findings
without the app:

| Command | |
|---|---|
| `set` | Lists the settings this device's layout supports, with the current values |
| `set <key> <value>` | Writes a known setting, e.g. `set trigger 0.06`, `set balance off` |
| `wreg <reg> <len> <value>` | Writes any register: reg in hex, value with a `.` goes as a float. E.g. `wreg 64 4 0.05` |
| `dump` | The last info, settings and cell frames in hex |

Each write is logged with its 20 bytes, and the settings frame that follows is decoded, so a change
(or none) shows straight away.

## Connection

- Service `FFE0`, characteristic `FFE1`. The same characteristic takes the requests (write
  **without** response) and carries the replies (notify). Turn notifications on (CCCD) first.
- One connection at a time. A connected device stops advertising, so it vanishes from scans while
  the phone app holds it.
- Recognising JK devices in a scan: the manufacturer data ends with the device's own MAC (e.g.
  `650b88a0 c84780234f95` for C8:47:80:23:4F:95); the name starts with `JK`, or another name set in
  the app (e.g. `RR48100-1114`); some advertise `FFE0`. Many advertise no name at all.
- Asking for a bigger MTU helps (the JK-B1A16S gives 131). Replies are 300 bytes split over
  several notifications.

## Frames

### Requests: 20 bytes

```
AA 55 90 EB  <reg/cmd>  <len>  <v0 v1 v2 v3>  00 x 9  <crc>
 0  1  2  3      4        5      6  7  8  9   10..18   19
```

- `crc` = sum of bytes 0..18, low 8 bits.
- `value` is little-endian; `len` says how many of its bytes count (0 for plain commands).

Commands:

| Cmd | |
|---|---|
| `0x97` | Device info: the device answers with an info frame (0x03) |
| `0x96` | Cell info: the device answers with its settings frame (0x01), then streams cell info frames (0x02) about once a second |
| `0xA1` | Logbook (listed by esphome-jk-bms; not tried) |

Connect order that works: turn on notifications, send `0x97`, wait ~300 ms, send `0x96`.

### Replies: 300 bytes

```
55 AA EB 90  <type>  <counter>  ...data...  <crc>
 0  1  2  3     4        5        6..298     299
```

- `crc` = sum of bytes 0..298, low 8 bits.
- type `0x01` settings, `0x02` cell info, `0x03` device info.
- A new frame starts with a notification beginning `55 AA EB 90`; collect until 300 bytes. Anything
  after byte 300 in that notification is padding.

## Writing settings

A setting is written with a request frame: register in byte 4, length in byte 5, value in 6..9.
The device doesn't answer. Send `0x96` again ~300 ms later and the settings frame comes back with
the new value, so a write can be confirmed by reading it back. **A value the device doesn't
accept is ignored silently**, so always read back.

Value encoding:

- **JK02:** integer, little-endian, `len` = 4. Voltages in mV, currents in mA (value x 1000);
  switches 0 / 1.
- **JK04:** voltages and currents as a 32-bit IEEE float, `len` = 4. Cell count as an integer,
  `len` = 2. Switches 0 / 1 with `len` = 1.

Example, JK04 trigger delta 0.05 V: `AA 55 90 EB 64 04 CD CC 4C 3D 00 00 00 00 00 00 00 00 00 04`
(`CD CC 4C 3D` is 0.05 as a float).

**On JK04, the register is the field's offset in the settings frame minus 6.** That holds for every
register tested: trigger delta at offset 106 is register 0x64, max balance current at 110 is 0x68,
the balancing switch at 114 is 0x6C.

### Registers

From esphome-jk-bms, with our test results. `-`: not in that layout.

| Setting | JK04 | JK02_24S | JK02_32S | Tested |
|---|---|---|---|---|
| Balancing start voltage | 0x58 (?) | 0x26 | 0x22 | JK04: no effect on JK-B1A16S, see quirks |
| Balance trigger delta | 0x64 | 0x06 | 0x06 | JK04: works |
| Max balance current | 0x68 | 0x13 | 0x13 | JK04: works up to the device's rating |
| Power off voltage | 0x20 | 0x0B | 0x0B | not yet |
| Cell count | 0x1C | 0x1C | 0x1C | not yet |
| Balancing switch | 0x6C | 0x1F | 0x1F | JK04: works |
| Voltage calibration | 0x44 | 0x21 | 0x64 | not yet |
| Charging switch | - | 0x1D | 0x1D | not yet |
| Discharging switch | - | 0x1E | 0x1E | not yet |
| Smart sleep voltage | - | 0x01 | 0x01 | not yet |
| Cell undervoltage protection | - | 0x02 | 0x02 | not yet |
| ... recovery | - | 0x03 | 0x03 | not yet |
| Cell overvoltage protection | - | 0x04 | 0x04 | not yet |
| ... recovery | - | 0x05 | 0x05 | not yet |
| SOC 100% voltage | - | 0x07 | 0x07 | not yet |
| SOC 0% voltage | - | 0x08 | 0x08 | not yet |
| Max charge current | - | 0x0C | 0x0C | not yet |
| Max discharge current | - | 0x0F | 0x0F | not yet |
| Total battery capacity | - | 0x20 | 0x20 | not yet |
| Current calibration | - | 0x24 | 0x67 | not yet |

esphome-jk-bms has more JK02 registers (temperature limits, delays, heating, sleep and so on), see
`components/jk_bms_ble/number/__init__.py` and `switch/__init__.py` there.

The 0x58 start voltage register doesn't fit the offset-minus-6 rule: the start voltage is at
offset 6 in the JK04 settings frame, which would make it register 0x00. Not tried yet.

### Renaming

Not known yet. The name is in the device info frame (offset 46, 16 bytes), and a 4-byte value field
can't hold it, so renaming probably uses a different request. To be captured (see above).

## Frame layouts

Three layouts of the same frames are around. The CYD tells them apart from the first cell info
frame: JK04 cell 1 (offset 6) is a float between 0.5 and 5 V; otherwise the JK02 cell mask and
average decide between 24 and 32 cells, with the hardware version as a tie-break (>= 11: 32S).

| Layout | Devices | Numbers |
|---|---|---|
| `JK02_24S` | older BMS (hw < 11), JK02 balancers | integers |
| `JK02_32S` | newer BMS (hw >= 11), e.g. JK-B1A8S10P, JK-PB | integers |
| `JK04` | older balancers, e.g. JK-B1A16S hw 8.X-W | little-endian floats |

### Device info frame (0x03), all layouts

| Offset | Size | |
|---|---|---|
| 6 | 16 | Model, e.g. `JK-B1A16S` |
| 22 | 8 | Hardware version, e.g. `8.X-W`, `11.XW` |
| 30 | 8 | Software version, e.g. `8.05` |
| 42 | 4 | Power-on count |
| 46 | 16 | Device name (set in the app) |
| 78 | 8 | Manufacturing date |
| 86 | 11 | Serial number (empty on the JK-B1A16S) |

### JK04 settings frame (0x01)

Verified by changing each setting in the JK app (JK-B1A16S) and by writes.

| Offset | Type | |
|---|---|---|
| 6 | float | Balancing start voltage |
| 34 | u8 | Cell count |
| 38 | float | Power off voltage |
| 74 | float | Unknown, 3.0092 (voltage calibration is register 0x44, which would be here) |
| 98 | float | Unknown, 4.25 (a maximum?) |
| 102 | float | Unknown, 3.4 |
| 106 | float | Balance trigger delta |
| 110 | float | Max balance current |
| 114 | u8 | Balancing switch |

### JK04 cell info frame (0x02)

| Offset | Type | |
|---|---|---|
| 6 + 4n | float | Cell n voltage (V) |
| 102 + 4n | float | Cell n wire resistance (Ohm) |
| 202 | float | Average cell voltage |
| 206 | float | Delta cell voltage |
| 218 / 219 | u8 | Highest / lowest cell (0-based). Lags the voltages; better to work it out from them |
| 220 | u8 | Balancing state: 0 waiting, 2 balancing |
| 222 | float | Balance current (A), negative while balancing |
| 286 | u24 | Uptime (s) |

### JK02 settings frame (0x01)

All u32, voltages in mV, currents in mA.

| Offset | |
|---|---|
| 10 / 14 | Cell undervoltage protection / recovery |
| 18 / 22 | Cell overvoltage protection / recovery |
| 26 | Balance trigger delta |
| 46 | Power off voltage |
| 50 | Max charge current |
| 62 | Max discharge current |
| 78 | Max balance current |
| 114 | Cell count |
| 118 / 122 / 126 | Charging / discharging / balancing switch |
| 130 | Battery capacity (mAh) |
| 138 | Balancing start voltage |

### JK02 cell info frame (0x02)

`o` = 0 for 24S, 16 for 32S. Fields after the wire resistances move by `2 * o`.

| Offset | Type | |
|---|---|---|
| 6 + 2n | u16 | Cell n voltage (mV) |
| 54 + o | u32 | Cell mask (one bit per cell present) |
| 58 + o | u16 | Average cell voltage (mV) |
| 60 + o | u16 | Delta cell voltage (mV) |
| 64 + o + 2n | u16 | Cell n wire resistance (mOhm) |
| 112 + 2o | i16 | MOSFET temperature (0.1 C) |
| 118 + 2o | u32 | Pack voltage (mV) |
| 126 + 2o | i32 | Current (mA) |
| 130 / 132 + 2o | i16 | Temperature sensor 1 / 2 (0.1 C) |
| 138 + 2o | i16 | Balance current (mA) |
| 140 + 2o | u8 | Balancing: 0 off, 1 charging, 2 discharging |
| 141 + 2o | u8 | SOC (%) |
| 142 + 2o | u32 | Remaining capacity (mAh) |
| 146 + 2o | u32 | Nominal capacity (mAh) |
| 150 + 2o | u32 | Cycle count |
| 154 + 2o | u32 | Cycle capacity (mAh) |
| 162 + 2o | u32 | Uptime (s) |
| 166 / 167 / 169 + 2o | u8 | Charging / discharging / balancing on |

## Device quirks

- **JK-B1A16S (hw 8.X-W, sw 8.05, JK04):**
  - Start voltage can't be changed, not even in the JK app. It stays at 1.000 V. A write to 0x58
    does nothing.
  - Max balance current above 1.0 A is ignored (a 1 A balancer); 0.8 A is accepted.
  - No serial number in the info frame.
- **ESP32 (the CYD), not the JK device:** with ESP-IDF 5.5.3 every connection dropped with a
  supervision timeout ~4 s after connecting. ESP-IDF 5.3.1 is fine. Doesn't matter for other
  clients, but worth knowing if a link keeps dropping.

## Test log

| Date | Device | What | Result |
|---|---|---|---|
| 2026-11-18 | JK-B1A16S (JK04) | `set trigger 0.06`, then `0.05` | Changed and back |
| 2026-11-18 | JK-B1A16S (JK04) | `set balance off`, then `on` | Changed and back |
| 2026-11-18 | JK-B1A16S (JK04) | `set maxbal 1.5` | Ignored (stays 1.0) |
| 2026-11-18 | JK-B1A16S (JK04) | `set maxbal 0.8`, then `1` | Changed and back |
| 2026-11-18 | JK-B1A16S (JK04) | `set start 3.3` (reg 0x58) | Ignored; start voltage is locked on this unit (also in the app) |

## Open questions

- How renaming works (capture pending).
- The start voltage register on a JK04 balancer that allows changing it: 0x58 (esphome) or 0x00
  (offset rule)?
- What the unknown JK04 settings at offsets 74, 98 and 102 are.
- Layout and registers of the current JK balancers.

## Python sketch

For a client on a PC, e.g. with [bleak](https://github.com/hbldh/bleak). Untested outline:

```python
import asyncio, struct
from bleak import BleakClient

CHR = "0000ffe1-0000-1000-8000-00805f9b34fb"

def request(reg, length=0, value=b"\0\0\0\0"):
    f = bytearray(20)
    f[0:4] = b"\xAA\x55\x90\xEB"
    f[4], f[5] = reg, length
    f[6:10] = value
    f[19] = sum(f[:19]) & 0xFF
    return bytes(f)

def jk04_float(reg, v):            # JK04 voltages / currents
    return request(reg, 4, struct.pack("<f", v))

def jk02_int(reg, v, factor=1000):  # JK02: mV / mA
    return request(reg, 4, struct.pack("<i", round(v * factor)))

async def main(mac):
    buf = bytearray()
    def on_notify(_, data):
        nonlocal buf
        if data[:4] == b"\x55\xAA\xEB\x90":
            buf = bytearray()
        buf += data
        if len(buf) >= 300:
            frame = bytes(buf[:300])
            if sum(frame[:299]) & 0xFF == frame[299]:
                print("frame type", frame[4])
            buf = bytearray()
    async with BleakClient(mac) as c:
        await c.start_notify(CHR, on_notify)
        await c.write_gatt_char(CHR, request(0x97), response=False)
        await asyncio.sleep(0.3)
        await c.write_gatt_char(CHR, request(0x96), response=False)
        await asyncio.sleep(5)

asyncio.run(main("C8:47:8C:F7:CF:CA"))
```
