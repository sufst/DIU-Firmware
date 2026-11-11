# Driver Input Unit firmware

Firmware for the Driver Input Unit (STM32C092FCP6), built on the STM32 HAL.

Every 1 ms it:

1. reads every driver sensor through the ADS7038 SPI ADC (with oversampling),
2. gives each sensor a fault code (shorted, disconnected, out of range),
3. combines the two accelerator sensors into one pedal position and runs the
   two pedal plausibility checks,
4. handles the case button and LED (pedal calibration, health light),
5. sends the sensor frame on CAN.

Less often it reads the IMU, the board temperature and the GLV supply voltage,
sends those on CAN, and prints everything on the serial debug port.

This board only reports. Acting on the flags (cutting torque) is the VCU's job.

## CAN frames

Classic CAN, 500 kbit/s. All values are little-endian ("Intel" in a DBC);
bit 0 is the lowest bit of byte 0. IDs and periods are in
[include/config.h](include/config.h).

### 0x100 sensors, every 1 ms, 8 bytes

| Bits  | Signal                              | Scale           | Range     |
|-------|-------------------------------------|-----------------|-----------|
| 0-9   | Accelerator pedal position          | 0.1 % / bit     | 0-100 %   |
| 10-21 | Brake pressure                      | 0.1 bar / bit   | 0-200 bar |
| 22-33 | Steering angle                      | 0.1 % / bit     | 0-100 %   |
| 34-37 | Message counter                     | +1 per frame, 15 wraps to 0 | |
| 38    | Pedal not calibrated                | 1 = nominal voltages in use | |
| 39-45 | free                                |                 |           |
| 46    | APPS implausibility                 | 1 = implausible |           |
| 47    | APPS / brake plausibility (EV6.3)   | 1 = tripped     |           |
| 48-50 | APPS1 fault code                    | see below       |           |
| 51-53 | APPS2 fault code                    | see below       |           |
| 54-56 | Brake pressure sensor fault code    | see below       |           |
| 57-59 | Steering sensor fault code          | see below       |           |
| 60-62 | free                                |                 |           |
| 63    | Pedal calibration in progress       | 1 = calibrating |           |

Brake pressure and steering are clamped to their range, so check their fault
codes as well. If the message counter stops changing, the frame is stale.

| Fault code | Meaning                              |
|------------|--------------------------------------|
| 0          | OK                                   |
| 1          | Shorted to ground, or disconnected   |
| 2          | Shorted to supply                    |
| 3          | Out of range (not a dead short)      |
| 4          | No data: the ADC is not responding   |

Each input has a 100k pull-down on the board, so an unplugged sensor reads
about 0 V. That is why "disconnected" and "shorted to ground" share a code.
A code is set as soon as the reading is bad and clears when it is good again.

### 0x101 acceleration and 0x102 rotation rate, every 10 ms, 6 bytes each

| Bytes | 0x101                      | 0x102                            |
|-------|----------------------------|----------------------------------|
| 0-1   | Acceleration X, 1 mg / bit | Rotation rate X, 0.1 deg/s / bit |
| 2-3   | Acceleration Y             | Rotation rate Y                  |
| 4-5   | Acceleration Z             | Rotation rate Z                  |

Signed 16-bit. Range +/-8 g and +/-500 deg/s. The axes are the IMU chip's own;
nothing rotates them to match how the board sits in the car. These two frames
are only sent while the IMU is working.

### 0x103 status, every 100 ms, 5 bytes

| Bytes | Signal                  | Scale                    |
|-------|-------------------------|--------------------------|
| 0-1   | GLV supply voltage      | unsigned, 1 mV / bit     |
| 2-3   | Board temperature       | signed, 0.1 degC / bit   |
| 4     | bit 0: IMU working, bit 1: temperature sensor working | |

## Sensors

One table at the top of [src/sensors.c](src/sensors.c) holds the scaling and
fault limits:

| Sensor   | ADC input     | Normal range  | Output    | Code 1 below | Code 3 outside  | Code 2 above |
|----------|---------------|---------------|-----------|--------------|-----------------|--------------|
| APPS1    | AIN1          | 0.5 - 4.5 V   | 0-100 %   | 0.1 V        | 0.4 - 4.6 V     | 4.75 V       |
| APPS2    | AIN2          | 0.25 - 2.25 V | 0-100 %   | 0.1 V        | 0.2 - 2.3 V     | 4.75 V       |
| Brake    | AIN3          | 0.5 - 4.5 V   | 0-200 bar | 0.1 V        | 0.4 - 4.6 V     | 4.75 V       |
| Steering | AIN4 (SPARE1) | 0.5 - 4.5 V   | 0-100 %   | 0.1 V        | 0.4 - 4.6 V     | 4.75 V       |
| (unused) | AIN0 (SPARE2) | -             | -         | -            | -               | -            |

The small margin beyond the normal range (0.1 V, or 0.05 V for APPS2) is still
accepted as OK, so a pedal resting at exactly 0 % does not flicker in and out
of "out of range".

If the ADC itself stops answering, every sensor reports code 4 and the
firmware retries setting the ADC up every 100 ms.

The ADC averages 16 conversions per reading inside the chip
(`ADC_OVERSAMPLING_LOG2`). This takes about 17 us per channel.

## Pedal position and safety flags

Thresholds and times are in [include/config.h](include/config.h); the logic is in
[src/safety.c](src/safety.c).

**APPS implausibility (bit 46).** Set when any of these has been true for more
than 100 ms without a break:

- APPS1 and APPS2 differ by more than 10 % of pedal travel
- the APPS1 and APPS2 signal wires are shorted together (the two voltages are
  within 100 mV of each other, which never happens normally because APPS2 runs
  at half the voltage of APPS1)
- either APPS has a fault code

It clears as soon as the sensors agree again.

**Accelerator pedal position (bits 0-9).** The two APPS are combined into one
value; their individual readings are only on the serial debug port.

| Situation                                        | Pedal position sent       |
|--------------------------------------------------|---------------------------|
| Sensors agree                                    | Average of the two        |
| Problem seen, but for less than 100 ms so far    | The lower of the two      |
| APPS implausibility flag set                     | 0 %                       |
| Calibration in progress                          | 0 %                       |

**APPS / brake plausibility, EV6.3 (bit 47).** Set when the brakes are actuated
(more than 5 bar) and the pedal is above 25 % at the same time for more than
500 ms. It then stays set, even if the brakes are released, until the pedal is
below 5 %. The higher of the two APPS readings is used. It will not reset while
either APPS is faulted, because an unplugged sensor reads 0 %. The pedal
position is still reported truthfully while this flag is set.

## Pedal calibration

The pedal has its own mechanical end stops, so the sensor voltages at 0 % and
100 % differ from the nominal 0.5 V / 4.5 V. Calibration records the real ones
using the case button and case LED:

| Step | Do this                                    | LED                    |
|------|--------------------------------------------|------------------------|
| 1    | Hold the button for 2 s                    | Slow blink (1 Hz)      |
| 2    | Release the pedal fully, press the button  | Fast blink (5 Hz)      |
| 3    | Press the pedal fully, press the button    | Steady = saved         |

If the LED flickers rapidly for 2 s instead, the calibration was rejected and
the previous one is kept. That happens when:

- either APPS has a fault code at the moment the button is pressed
- the pedal moved less than a quarter of the sensor's nominal span, or the
  two positions were recorded the wrong way round
- no button press arrived within 60 s
- the values could not be saved

While calibrating, the pedal position is sent as 0 % and bit 63 is set.

The calibration is stored in the I2C EEPROM
([src/cal_storage.c](src/cal_storage.c)) and loaded at start-up, so it
survives power-off and re-flashing. Until a board has been calibrated it uses
the nominal voltages and sets the "not calibrated" flag (bit 38).

A 2 % dead zone is applied at each end of the calibrated travel
(`APPS_CAL_DEADZONE`), so a released pedal reliably reads 0 % and a fully
pressed one 100 %.

Calibration only changes the scaling. The fault-code limits always come from
the sensor table.

## Case LED and button

Outside calibration the LED is a health light: on when nothing is wrong, off
when either plausibility flag is set or any sensor has a fault code.

The firmware expects the button between PA3 (`CASE_BTN`) and +3.3V, using the
microcontroller's internal pull-down. The polarity is set in
[include/board.h](include/board.h).

**In the current PCB layout the `CASE_BTN` net connects only to the
microcontroller pin: it does not reach the case connector P1** (P1 is +3.3V,
GND, LED, GND). The button will not work until that is routed or bodged.

## Serial debug output

USART2 transmit on PA2 (test point TP5), 3.3 V logic, **921600 baud, 8N1**.
In PuTTY choose connection type Serial and that speed, and widen the window
to at least 155 columns so each line fits.

A line is printed every 20 ms (50 per second) in fixed-width columns, with the
headings repeated every 40 lines:

```
   TIME_s A1_mV  A1_% A2_mV  A2_% PEDAL% BRK_mV   BAR STR_mV STR_% F1 F2 FB FS IMP BPP CAL  GLV_V TEMP_C    AX_g    AY_g    AZ_g  GX_dps  GY_dps  GZ_dps
   123.45  2500  50.0  1250  49.5   49.7    742  12.1      0   0.0  0  0  0  1   0   1  OK  12.34   -5.3   0.012  -1.003   8.000    -0.5   123.4     0.0
```

| Column           | Meaning                                                   |
|------------------|-----------------------------------------------------------|
| `A1_mV`, `A1_%`  | APPS1 voltage and position (same for `A2`)                |
| `PEDAL%`         | Combined pedal position, as sent on CAN                   |
| `BRK_mV`, `BAR`  | Brake sensor voltage and pressure                         |
| `STR_mV`, `STR_%`| Steering sensor voltage and position                      |
| `F1 F2 FB FS`    | Fault codes: APPS1, APPS2, brake, steering                |
| `IMP`, `BPP`     | APPS implausibility and EV6.3 flags                       |
| `CAL`            | `NO` not calibrated, `OK` calibrated, `RUN` calibrating   |
| `GLV_V`          | GLV supply voltage                                        |
| `TEMP_C`         | Board temperature (`---` if the sensor is not answering)  |
| `AX_g` ...       | Acceleration in g (`---` if the IMU is not answering)     |
| `GX_dps` ...     | Rotation rate in deg/s                                    |

Speed, rate and how often the headings repeat are `DEBUG_BAUD`,
`DEBUG_PERIOD_MS` and `DEBUG_HEADER_EVERY`. To add a column, see the comment at
the top of [src/debug.c](src/debug.c). Printing runs in the background (DMA)
and never delays the 1 ms loop; if the previous line is still going out, the
next one is skipped.

## Watchdog

The independent watchdog resets the microcontroller if the main loop stops
running for 250 ms (`WATCHDOG_TIMEOUT_MS`). It is paused while the CPU is
halted in a debugger.

## Other devices

| Device                  | Bus / pin        | Used for                               |
|-------------------------|------------------|----------------------------------------|
| LSM6DSO32 IMU (U9)      | I2C, 0x6A        | Acceleration and rotation rate         |
| MCP9800 (U8)            | I2C, 0x4D        | Board temperature                      |
| M24256E EEPROM (IC8)    | I2C, 0x50        | Pedal calibration (first 16 bytes)     |
| GLV monitor             | PA0, MCU ADC     | Supply voltage via the 56k/10k divider |

The I2C bus runs at 400 kHz. A device that stops answering is set up again
once a second, and the rest of the firmware carries on without it.

## Adding the spare sensor

1. Uncomment `SENSOR_SPARE2` in the enum in [include/sensors.h](include/sensors.h).
2. Uncomment its row in the table in [src/sensors.c](src/sensors.c) and set the
   voltages and output range.
3. Uncomment its row in the CAN layout table in [src/main.c](src/main.c).

That gives it a fault code in bits 60-62 of the sensor frame. Its value no
longer fits in that frame, so it also needs adding to another frame and, if
wanted, to the debug columns.

## Building

Needs the Arm GNU Toolchain (`arm-none-eabi-gcc`) on PATH, plus CMake, Ninja
and Git. The first configure downloads ST's HAL and CMSIS headers from GitHub.

```
cmake -B build -G Ninja
cmake --build build
```

Output: `build/diu.elf`, `diu.bin`, `diu.hex`. Flash over SWD (J2) with
STM32CubeProgrammer or OpenOCD.

## Tests

The logic that does not touch hardware is tested on the PC, with the ADC and
the EEPROM replaced by fakes and time stepped 1 ms at a time. Any ordinary C
compiler will do (not the ARM one).

```
cmake -S test -B build-test -G Ninja
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

| Test                 | Covers                                                        |
|----------------------|---------------------------------------------------------------|
| `test_safety`        | Scaling, fault codes, pedal value, APPS implausibility, EV6.3 |
| `test_calibration`   | Button sequence, LED patterns, saving, every rejection case   |
| `test_debug`         | Debug text: column alignment and number formatting            |

The hardware drivers (SPI, CAN, I2C, serial, watchdog) are not covered by
these and need checking on the board.

## Layout

| File                        | Purpose                                              |
|-----------------------------|------------------------------------------------------|
| `include/`                  | Header files (one per source file, plus the two below) |
| `src/main.c`                | Main loop, CAN frame layouts                         |
| `include/config.h`              | All tunable numbers                                  |
| `src/sensors.c`             | Sensor table, scaling, fault codes                   |
| `src/safety.c`              | Pedal position, APPS implausibility, EV6.3 check     |
| `src/calibration.c`         | Pedal calibration, case button and LED               |
| `src/cal_storage.c`         | Saves the calibration in the EEPROM                  |
| `src/debug.c`               | Builds the serial debug text                         |
| `src/ads7038.c`             | Sensor ADC driver (SPI1)                             |
| `src/can.c`                 | CAN transmit (FDCAN1)                                |
| `src/i2c_bus.c`             | I2C bus (I2C2)                                       |
| `src/lsm6dso32.c`           | IMU driver                                           |
| `src/mcp9800.c`             | Temperature sensor driver                            |
| `src/eeprom.c`              | EEPROM driver                                        |
| `src/glv.c`                 | GLV supply voltage (MCU ADC)                         |
| `src/serial.c`              | Serial port (USART2 with DMA)                        |
| `src/board.c`               | Clock, pin setup, watchdog (pin map in `board.h`)    |
| `include/stm32c0xx_hal_conf.h`  | Which HAL drivers are built                          |
| `stm32c092xc.ld`            | Linker script                                        |
| `test/`                     | PC tests                                             |
