# Battery measurement and display

The only active measurement route is **INA226 → classic Nano → Sensing → Main**. Main's `BatteryService` consumes the checked remote measurements; the inactive direct INA219 driver and voltage-classifier settings have been removed. Main GPIO21/22 remain reserved rather than being reassigned.

Nano uses one INA226 at 0x40, a configured 0.010-ohm shunt and a 5 A measurement range. Read validation checks identity, calibration/configuration registers, complete I2C reads and overflow/range errors, with bounded retry. Source voltage correction assumes VBUS is connected to the load side of the shunt. Follow the actual [power/pin wiring](../hardware/pinouts.md).

The normal sample interval is one second. A 40-byte version-3 CRC packet is wrapped in `|B1:` plus 80 hexadecimal characters on the 19200-baud Nano serial line. The prefix is readable status; only the validated packet supplies measurements. Sensing forwards source age, sends due incident frames first and reserves room for urgent traffic. A pending acknowledgement does not stop battery forwarding between incident retries. Deferred battery packets retry at bounded 20 ms intervals; the one-second forwarding period advances only after a successful send. Duplicate identities do not refresh an old reading.

## Percentage

| Source voltage | Raw approximate percent |
| --- | --- |
| 10.600 V or below | 0 |
| 10.705 V | 10 |
| 10.810 V | 20 |
| 11.125 V | 50 |
| 11.440 V | 80 |
| 11.650 V and above | 100 |

Main maps the owner's observed loaded operating range linearly: 10.6 V is empty for this system, and 11.65 V is full. The observed full range of 11.65 to 11.80 V therefore stays at 100%. This estimates usable voltage range, not measured remaining capacity or charge termination. Values use the same source voltage shown by Main: bus voltage plus the signed shunt drop, rounded to the nearest millivolt on Main. This keeps the observed full and empty thresholds on one measurement basis. Raw voltage and current are unchanged. Main applies a five-sample median, a symmetric 30-second voltage filter, normal movement of one percentage point per five seconds and ten-percent display steps with hysteresis. Large persistent upward steps invoke a 60-second hold and slower upward movement. Gaps/faults do not earn percentage movement credit.

Three consecutive valid readings at/below 10.810 V cap the internal estimate at 20%; at/below 10.705 V cap it at 10%; at/below 10.600 V force 0%. Confirmed empty also lowers the stored voltage filter so an earlier high reading cannot inflate recovery. After three valid readings at/above 10.750 V and a positive internal estimate, displayed 0 can recover to the minimum 10. These display policies do not disconnect the pack or provide individual-cell protection.

Flash Main only for the current battery and dashboard updates. No Nano cable or Nano firmware upload is required. Main ignores the incoming Nano percentage and calculates its own estimate from the checked raw bus and shunt measurements. It advances the filter once per newly accepted valid sample, using source acquisition time; duplicate packets, repeated page refreshes and normal loop cycles cannot advance it. The existing six-second freshness limit and Sensing relay packet layout are unchanged. Older Nano packets remain supported, including valid raw readings without a Nano percentage.

Main retains the last percentage/age in RAM for the rider while live validity expires after six seconds. A page reload can fetch that retained snapshot; reboot cannot invent a prior reading.

## Voltage display

The developer Power monitor shows source voltage with browser presentation filtering, one decimal place and hysteresis. Its five-reading median and six-second filter reduce visible fluctuations. Source voltage at or below 10.8 V bypasses that filter immediately, and the low-voltage warning uses the raw measurement. Invalid or stale measurements are unavailable. Detailed readings retain raw source voltage, bus voltage, current and power. This display filter runs on the viewing device and does not change Nano's percentage, telemetry, LED policy or electrical output. A smoothed value such as 11.7 V can therefore accompany a 100% estimate throughout the observed full-charge range.

## LED and charging

Fresh 0–10% blinks red; above 10% through 30% is steady red. Above 30%, including 100%, and unknown/stale readings are off. Automatic full-battery green indication is disabled. Temporary developer LED diagnostics remain bounded and do not alter actual battery data. The three-second white startup sequence and crash indicators retain priority.

Active Nano charging detection remains removed. Charge compatibility fields are unknown/zero and the normal UI does not infer charging from load current or rising voltage. Protocol parsing still understands compatible historical packets so saved/source interfaces are not broken. Use the charger's own indication for charging status.


## Test the physical indicator

Administrators: open **Pairing / SMS Test → Vest indicator light**, or follow the link on Power / Sensing. Choose blinking red, steady red, steady green, blinking green, blue or off and inspect the actual vest. The illustration shows the requested pattern; it cannot measure whether the physical LED is lit or synchronize its blink phase to the board.

Each request lasts at most 15 seconds, then Main resumes its current battery policy. **Return to battery indicator** ends it sooner. Battery telemetry and crash monitoring continue. Leaving the page, losing Wi-Fi or signing out does not extend the timeout. Normal indication may be off if readings are stale. Tests are RAM-only and never rewrite calibration, battery levels or stored settings.

Steady or blinking green is available only as a requested diagnostic pattern; the active Nano does not measure charging. Blue checks the blue channel and has no battery meaning. These patterns do not certify charge termination. A steady red test does not create a low-battery measurement. The API requires an administrator session, valid local host/origin and CSRF token, plus enabled bench tests and indicator configuration for starting; Stop remains allowed if those configuration gates are off.

## Automatic crash indication

A real incident blinks orange for five seconds. The first successful emergency SMS submission replaces it with steady orange for a fresh five seconds. Emergency signals take priority over battery colors and manual tests, then the current indication resumes. See [crash light behavior and limits](CRASH_LED.md).

## Distinguishing communication from measurement faults

`nanoConnected` means Main has a recent checked Nano packet through Sensing, independently of whether the INA226 measurement is valid. The administrator can therefore show a connected Nano with a missing/invalid monitor reading. Invalid data never becomes a displayed voltage or battery percentage. The existing 8–15 V measurement check accepts 11.2 V.

Sensing USB diagnostics at 115200 baud include received bytes/frames, rejected frames, UART errors, forwarding count, pending incidents, Nano error, flags and last reported raw millivolts. `nano=1 valid=0` means protocol packets are arriving but no valid measurement is available. Raw diagnostic fields may contain the preceding measurement after an I2C fault; use `valid` and the error code, rather than treating them as a live battery value.

Nano errors: 2/3 = I2C address/read request not acknowledged; 5/12 = timeout; 6 = unexpected sensor identity; 7 = initialization failure; 8 = configuration/calibration mismatch; 9 = overflow; 10 = measurement outside configured limits; 11 = incomplete register read. A valid battery reading requires more than a readable voltage. Upload matching Main and Sensing builds to apply the relay fix and expose the current fault. Nano firmware and measurement limits were not changed by this fix.
