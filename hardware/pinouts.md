# SmartVest wiring and pinouts

Updated 23 October 2026 against this project's Main 1.0, Sensing 1.0,
classic 5 V ATmega328P Nano sketch, and SJKS-camera-1.0 source. This is the
current wiring reference; older INA219/Main-I2C and MPU wiring is superseded.
GPIO numbers belong to the named board, not a header position. A pin number on
Main is independent of the same number on Sensing or either camera.

Firmware assignments are checked below. Breakout supply ratings, connector order,
internal INA226 links, resistor installation and the assembled wiring still need
to match the actual hardware; a source review cannot electrically verify them.

## System connections

```text
INA226 -- I2C --> Nano -- 19200-baud UART, level shifted --> Sensing
BMI323 -- SPI --------------------------------------------> Sensing
Protected front/rear piezo group outputs ------------------> Sensing
Sensing <-- 115200-baud UART, two directions --------------> Main
GPS ---- 9600-baud receive-only UART ----------------------> Main
SIM800L EVB <---- dedicated modem UART --------------------> Main
Main ---- GPIO -------------------------------------------> RGB LED
Front camera <-- Wi-Fi / HTTP --> Main <-- Wi-Fi / HTTP --> Rear camera
```

Exactly one INA226 is used. Nano measures voltage and current; Sensing forwards
the raw readings. Main calculates the smoothed battery percentage for both
dashboards and its RGB LED. Its estimate does not require an updated Nano.
There is no battery sensor wired to Main GPIO21/22 in the current configuration.

## Main ESP32-WROOM development board

| Connection at module / other board | Main connection | Direction / owner |
| --- | --- | --- |
| SIM800L EVB TXD | GPIO16 (RX) | EVB -> Main, hardware UART2 |
| SIM800L EVB RXD | GPIO17 (TX) | Main -> EVB, hardware UART2 |
| SIM800L EVB UART/power GND | Common GND | Preserve the verified EVB interface and supply |
| NEO-6M breakout TX | GPIO4 (RX) | GPS -> Main, UART1, 9600 baud |
| NEO-6M breakout RX | Leave unconnected | No GPS configuration transmissions; GPIO5 reserved |
| NEO-6M breakout GND | Common GND | Shared UART reference |
| NEO-6M breakout VCC | Breakout-rated supply | Do not infer voltage from the NEO-6M name alone |
| Sensing GPIO17 (TX) | GPIO26 (RX) | Sensing -> Main, UART0, 115200 baud, 8N1 |
| Sensing GPIO16 (RX) | GPIO27 (TX) | Main -> Sensing, UART0, durable ACK/control return |
| Sensing GND | Main GND | Required signal reference |
| RGB module R | GPIO23 | Active HIGH, common cathode |
| RGB module G | GPIO18 | Active HIGH, common cathode |
| RGB module B | GPIO19 | Reserved color output; normally off |
| RGB module GND / minus | Main GND | Follow module labels, not an assumed pin order |
| Front and rear cameras | Main Wi-Fi AP | No camera UART/data wires to Main |
| GPIO21 / GPIO22 | Reserved, leave unconnected for battery | Former I2C reservation; inactive driver removed |
| GPIO5 | Reserved, leave unconnected | Legacy GPS TX reservation, not driven |
| GPIO1 (TX0 header / USB bridge RX) | Board USB bridge | Runtime debug output at 9600 baud, UART1 TX |
| GPIO3 (RX0 header / USB bridge TX) | Board USB bridge | Upload/bootloader input; no peripheral wiring |

SIM800L firmware probes 9600, 115200, 57600, 38400 and 19200 baud rather than
assuming a single modem baud. UART flow-control and DTR pins are not assigned.
Keep the working EVB's logic conversion and any EVB-specific VDD connection;
firmware does not establish what that breakout's VDD pin means. A bare SIM800L
and a regulated EVB do not have interchangeable supply/interface requirements.

Main uses all three UART controllers: UART2 modem; UART1 GPS RX plus console TX;
UART0 Sensing on GPIO26/27 after startup. Do not add runtime Serial.begin() or
Core Debug logging on UART0 while that link is active. Main's Serial Monitor is
9600 baud for application logs; reset/ROM output may use another baud.

## Main <-> Sensing UART: crossed TX/RX

| From | To |
| --- | --- |
| Sensing GPIO17 TX | Main GPIO26 RX |
| Main GPIO27 TX | Sensing GPIO16 RX |
| Sensing GND | Main GND |

115200 baud, 8N1, 3.3 V logic. The return wire is required for acknowledgements,
commands and the Main capability heartbeat. Do not confuse Main GPIO16/17 (GSM)
with Sensing GPIO16/17 (the Main link). Keep the wires short and share ground.

## Sensing ESP32-WROOM development board

| Module signal | Sensing connection | Purpose |
| --- | --- | --- |
| BMI323 SCK | GPIO18 | SPI clock, 100 kHz, mode 0 |
| BMI323 SDO / MISO | GPIO19 | Sensor -> Sensing |
| BMI323 SDI / MOSI | GPIO23 | Sensing -> sensor |
| BMI323 CS | GPIO21 | Chip select, idle HIGH |
| BMI323 VCC / logic supply | Sensing 3.3 V | Retain the working 3.3 V breakout arrangement |
| BMI323 GND | Sensing GND | Shared sensor reference |
| BMI323 interrupt pins | Leave unconnected | Firmware polls data readiness |
| Protected front piezo group output | GPIO34, ADC1 channel 6 | Front group, input only |
| Protected rear piezo group output | GPIO35, ADC1 channel 7 | Rear group, input only |
| Piezo conditioning reference | Sensing GND | External protection/bias/discharge circuit |
| Main GPIO27 TX | GPIO16 RX | Hardware UART2, 115200 baud |
| Main GPIO26 RX | GPIO17 TX | Hardware UART2, 115200 baud |
| Nano D1 TX through divider | GPIO32 RX | Hardware UART1, 19200 baud |
| GPIO33 TX | Leave unconnected | Reserved by battery UART1 configuration |
| GPIO1 TX0 / GPIO3 RX0 | Board USB bridge | USB upload and diagnostics, 115200 baud |
| GPIO22 | No current IMU connection | Previous I2C IMU path is not used |

The eight piezo elements are described as four front plus four rear, combined
into two protected group outputs. Connect only the conditioned outputs to ADC1,
never a bare piezo or unprotected group. GPIO34/35 have no internal pull-ups or
pull-downs; use the existing external bias/discharge circuitry. ADC attenuation
is not overvoltage protection. Readiness checks cannot prove that every element
in a passive group is attached.

BMI323 sampling is nominally 200 Hz. MPU I2C mappings do not apply. Sensing does
not run Wi-Fi. Fresh IMU/calibration, piezo readiness and the Main handshake gate
automatic alerts. Piezo impacts provide supporting evidence; tune their provisional
thresholds using the administrator impact trials before relying on corroboration.
See [calibration](../docs/CALIBRATION.md) for the implemented motion rule.

## INA226 to classic 5 V Nano

| INA226 signal | Nano / system connection | Notes |
| --- | --- | --- |
| VCC / VS | Nano regulated 5V pin | Logic power; not Nano VIN, not battery positive |
| GND | Nano GND and system common GND | Also connect protected battery/system negative |
| SDA | Nano A4 | Local I2C; keep wires short |
| SCL | Nano A5 | Local I2C, 100 kHz |
| ALERT / ALER | Leave unconnected | Firmware reads status over I2C; no alert wire required |
| A0 / A1 jumpers, if present | Retain the working 0x40 selection | Normally both GND; do not confuse with Nano A0/A1 |
| IN+ / shunt supply side | Fused, protected pack positive | High-side current input |
| IN- / shunt load side | Positive feeding all vest load branches | High-side current output |
| VBUS, if separately exposed | IN- / load-side positive for this firmware's SourceV calculation | May already be linked on the breakout |

Use printed labels and the breakout schematic/continuity, not screw-terminal
position. The photographed R010 module has a 0.010 ohm shunt; the Nano code is
configured for that shunt and a 5 A measurement range. This setting does not
certify the terminal/track/shunt current rating.

**VIN/VBUS is a measurement connection, not the INA226 VCC supply.** A terminal
marked only VIN may expose VBUS or a shunt node depending on the breakout. The
photograph alone does not establish the internal routing. With power removed,
identify IN+, IN- and VBUS using the markings/schematic or continuity before
adding a bridge. Do not connect IN- to battery negative: that would put the shunt
across the pack. Connecting only a voltage-sense VIN to pack positive and GND to
pack negative does not force the load current through the shunt.

Main currently calculates SourceV as VBUS plus shunt voltage; that assumes VBUS
is at IN-. Main's percentage uses that same corrected source voltage and ignores
the older Nano percentage. If the breakout fixes
VBUS to IN+ instead, the voltage/current can still be read, but Main's SourceV
correction does not match that layout. Resolve the link or correction against
actual routing; do not claim an unverified jumper exists or cut a trace from a
photograph. [TI's pin definitions](https://www.ti.com/lit/ds/symlink/ina226.pdf)
separate VS, VBUS, IN+, IN- and the open-drain ALERT output. INA226 VS accepts
2.7-5.5 V; its measurement bus supports 0-36 V. These are different limits.

## Nano -> Sensing UART and level conversion

```text
Nano D1 TX ---- 1 kohm ----+---- Sensing GPIO32 RX
                          |
                        2 kohm
                          |
Nano GND -----------------+---- Sensing GND ---- Main GND
```

Use 1% resistors and short wires: 1 kohm is between Nano TX and the junction;
2 kohm goes from that junction to GND. Nominal HIGH is 5 x 2/(1+2) = 3.33 V.
Do not connect Nano's 5 V TX directly to an ESP32 GPIO. For boards powered
independently, use an appropriate UART translator with power-off isolation.

Nano D0 RX and Sensing GPIO33 TX remain unconnected. No return commands are
required. Nano's shared D1/USB serial carries readable status plus CRC telemetry
at 19200 baud, 8N1. The optional USB-only diagnostics build uses 115200 and omits
relay frames; keep SMARTVEST_NANO_USB_DIAGNOSTICS=0 for normal operation.
Nano A4/A5 and D1/D0 match the [classic Nano pinout](https://docs.arduino.cc/resources/pinouts/A000005-full-pinout.pdf).

## Battery and power distribution

```text
Protected battery P+ -- suitable fuse -- INA226 IN+ [R010] IN- -- load + split
                                                              |-- USB 5 V buck --> Main, Sensing, both cameras
                                                              |-- regulated 5 V --> Nano 5V --> INA226 VCC
                                                              `-- modem buck --> SIM800L EVB rated power input
Protected battery P- -------------------------------------------- common GND
12.6 V charger ------------------------------------------------> pack's charging socket
```

This preserves the reported four-port USB buck for the ESP32 boards and a
separate modem buck. All vest load branches must be downstream of IN- for their
current to be included; INA226 logic power can be supplied from the Nano's 5 V
rail. Feed Nano from a suitable regulated 5 V source, not through an ESP32's
3.3 V regulator. Nano VIN is not used for this regulated-5-V arrangement.
The [classic Nano manual](https://www.arduino.cc/en/uploads/Main/ArduinoNanoManual23.pdf)
distinguishes its regulated +5V pin from VIN.

A 5 V modem buck may feed only an EVB input explicitly rated for 5 V. Do not
apply 5 V directly to a bare modem's VBAT, or route modem supply current through
the Nano, ESP32 power pins or a signal breadboard. Keep the verified modem supply
and level-conversion arrangement rather than assuming an EVB VDD pin is a power input.

Power ESP32 development boards through their supported USB/regulated-5-V input;
a development board's VIN/5V label is board-specific. A bare ESP32-WROOM module
requires its 3.3 V supply, not 5 V. Never put pack voltage on Nano 5V, INA226 VCC,
an ESP32 power GPIO or the camera 5V input. Check power-selection/isolation before
using PC USB and an external supply together. Common ground is required; that
does not mean paralleling separate USB/buck positive outputs. Avoid driving
unpowered boards through UART/I2C. See [ESP32-WROOM electrical limits](https://documentation.espressif.com/esp32-wroom-32_datasheet_en.pdf).

Charging detection is removed from Nano. The charger adapter supplies its own
red/green indication. The shunt in the load branch does not measure charging
current through the pack's separate charging socket. No second INA226 is used.
Pack capacity and cell count are not inferred from the six-cell outer shape;
the display uses the observed loaded range of 10.6 V empty to 11.65 V full. This is an approximate operating-range indication; charger specifications and wiring are unchanged. See [battery display](../docs/BATTERY.md).

## Main RGB LED behavior

| Fresh reported percentage | Main RGB output |
| --- | --- |
| 0-10% | Red blinking, 250 ms on / 250 ms off |
| Above 10% through 30% | Steady red |
| Above 30%, including 100% | Off |
| No valid/fresh reading or disconnected Sensing | Off |

R/G/B use Main GPIO23/18/19, common cathode. Blue is off in normal automatic
operation. Follow the physical R/G/B/GND markings; connector order can vary.
Use a module with current limiting, or correctly sized individual series
resistors for a bare RGB LED. No LED pin connects to battery voltage or 5 V.
Green is available for manual diagnostics only; automatic 100% indication is off.
The white startup sequence and crash indications retain their priority. The dashboard retains the last
reading and elapsed update age; the LED uses fresh data only. See [LED policy](../docs/BATTERY.md).

## Front and rear cameras

Both camera boards use the same AI Thinker pin layout in CameraApp.cpp, with an
OV3660 sensor ID required by this firmware and PSRAM required for recording.
Each camera has its own role/key. In CameraConfig.h, select
front for the front camera and rear for the rear camera and retain the matching fixed project key. Do not
publish keys in this wiring document. Pairing and incident requests use Wi-Fi,
not a UART cable to Main.

| Internal camera signal | GPIO on each camera board |
| --- | --- |
| D0 / D1 / D2 / D3 | 5 / 18 / 19 / 21 |
| D4 / D5 / D6 / D7 | 36 / 39 / 34 / 35 |
| XCLK / PCLK | 0 / 22 |
| VSYNC / HREF | 25 / 23 |
| SCCB SDA / SCL | 26 / 27 |
| PWDN / RESET | 32 / -1 (no dedicated reset GPIO) |
| MicroSD CLK / CMD / D0 | 14 / 15 / 2, SD_MMC 1-bit mode |
| USB/UART adapter TX -> camera RX | Adapter 3.3 V logic TX -> GPIO3 (U0R) |
| Camera TX -> USB/UART adapter RX | GPIO1 (U0T) -> adapter RX, 115200 baud |
| Bootloader selection during upload | GPIO0 -> GND for flashing; remove before normal boot |
| Board power / return | Supported regulated 5V input / GND |

The camera and SD pins are already wired on an AI Thinker-layout board; they are
not spare pins for the Nano or sensors. The [SD_MMC reference](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/sdmmc.html)
lists classic ESP32 SD pins. This code calls SD_MMC.begin with 1-bit mode and
format-on-mount-failure disabled. Power each camera from its suitable 5 V input;
an adapter's 3.3 V logic setting does not establish adequate camera power.

## Serial Monitor rates

| Board | Normal application monitor rate |
| --- | --- |
| Main | 9600 baud |
| Sensing | 115200 baud |
| Classic Nano | 19200 baud |
| Front/rear camera | 115200 baud |

Nano-to-Sensing is 19200; Sensing-to-Main is 115200; GPS-to-Main is 9600.
Monitor/upload baud and a different peripheral's UART baud are not interchangeable.

## Source of each assignment

- [Main configuration template](../Main_and_Cameras/SmartVest_SJKS01_Main/Config.h.example), ConfigValidation.h,
  NEO6MService.cpp and SIM800LService.cpp: GPIO reservations and UART ownership.
- [Sensing Config.h](../Sensing/SmartVest_SJKS01_Sensing/Config.h), EventLink.h and NanoBattery.h:
  Main/battery UARTs, BMI323 SPI and piezo ADC pins.
- [Nano sketch](../Nano/SmartVest_INA226_Nano/SmartVest_INA226_Nano.ino): I2C defaults,
  0x40 / R010 / 5 A range, 19200-baud relay and removed charging classification.
- [CameraApp.cpp](../Main_and_Cameras/SmartVest_SJKS01_Camera/src/CameraApp.cpp) and
  [camera configuration template](../Main_and_Cameras/SmartVest_SJKS01_Camera/CameraConfig.h.example): camera pins,
  SD_MMC 1-bit mode, roles, PSRAM and expected sensor.

v1.0 preserves these pin assignments, camera keys, calibration rules and power topology.
This finalization did not flash or electrically exercise the installed boards.
