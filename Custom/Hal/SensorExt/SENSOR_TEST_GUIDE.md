# Sensor Test Guide

This document describes how to test the SensorExt sensors on the NE301 platform via the debug CLI (AICAM shell). It is based on actual test logs and covers the I2C bus scan, sensor example (sexp) modes, and expected outputs.

---

## 1. Prerequisites

- I2C bus 1 connected to the sensor expansion board
- **TFT and audio are independent:** the TFT (ST7789VW) runs on SPI6, the NAU881x audio codec on SAI1 + I2C port 1. They share no peripheral or pins and can be used at the same time. See section 8 for audio tests.
- TFT ST7789V display connected via SPI6 (for sexp modes; requires TFT option populated)
- Camera pipe2 already configured and running (for `sexp start` non-IR mode only)

---

## 2. I2C Bus Scan

Use `i2c_tool detect` to scan for I2C devices on the bus. This verifies sensor presence and I2C addresses before running the sensor example.

### Command

```
i2c_tool detect [bus] [start_addr] [end_addr]
```

| Parameter    | Default | Description                              |
|-------------|---------|------------------------------------------|
| bus         | 1       | I2C bus number (only bus 1 supported)    |
| start_addr  | 0x03    | Start 7-bit address (hex or decimal)     |
| end_addr    | 0x77    | End 7-bit address (hex or decimal)       |

### Example Output

```
AICAM> i2c_tool detect
Scanning I2C bus 1, address range 0x03-0x77
      00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
00:          -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- 1a -- -- -- -- -- 
20: -- -- 22 -- -- -- -- -- -- 29 -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- 44 -- -- -- -- -- -- -- -- -- -- -- 
50: -- 51 -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- 66 -- -- -- 6a -- -- -- -- -- 
70: -- -- -- -- -- -- -- --                        
```

### I2C Address Map (from scan)

| Addr (7-bit) | Device      | Description                          |
|--------------|-------------|--------------------------------------|
| 0x1a         | NAU881x     | Audio codec                          |
| 0x22         | LTR-31x     | Ambient light sensor (ALS)           |
| 0x29         | VL53L1X     | ToF distance sensor                  |
| 0x44         | SHT3x       | Temperature & humidity sensor        |
| 0x51         | DTS6012M    | TOF LIDAR distance sensor            |
| 0x66         | MLX90642    | Thermal IR array (32×24 pixels)      |
| 0x6a         | LSM6DSR     | IMU (accelerometer + gyroscope)      |

If any sensor is missing, verify hardware connections and power. `--` means no device responded at that address.

---

## 3. Sensor Example (sexp) Commands

The `sexp` command provides an integrated sensor demo that initializes the TFT display and all supported sensors, then displays camera preview or thermal image.

### 3.1 Start Camera Preview Mode

```
sexp start
```

**Behavior:**

- Initializes TFT ST7789VW on SPI6 (240×240)
- Initializes all sensors on I2C port 1
- Runs preview thread: camera pipe2 (256×256, RGB888) → center crop → TFT
- Runs sensor read thread (reads SHT3x, ALS, LSM6DSR, VL53L1X, DTS6012M periodically)
- Runs MLX90642 thermal read thread

**Expected Log Output (success):**

```
[DRIVER] ST7789VW: init start (SPI6)
[DRIVER] ST7789VW: init done, 240x240
[DRIVER] sexp: Initializing sensors...
[DRIVER] sexp: SHT3x initialized (periodic 2Hz)
[DRIVER] sexp: ALS initialized
[DRIVER] lsm6dsr: init OK (addr=0x6A, id=0x6B, default: acc=12.5Hz/±2g, gyro=12.5Hz/±2000dps)
[DRIVER] sexp: LSM6DSR initialized
[DRIVER] vl53l1x: Sensor ID = 0xEACC (Model ID 0xEA verified, continuing)
[DRIVER] vl53l1x: init OK (addr=0x29, sensor_id=0xEACC)
[DRIVER] sexp: VL53L1X initialized
[DRIVER] dts6012m: init OK (addr=0x51)
[DRIVER] sexp: DTS6012M initialized
[DRIVER] mlx90642: init OK (addr=0x66)  rate=2Hz  emissivity=0x3D71  Treflected=-136 (-1.36 C)
[DRIVER] sexp: MLX90642 initialized
[DRIVER] sexp: started (pipe2 256x256 bpp=3 -> TFT 240x240)
```

**Note:** Camera pipe2 must already be started by the application before running `sexp start`.

---

### 3.2 Start IR Thermal Mode

```
sexp start ir
```

**Behavior:**

- Same TFT and sensor init as `sexp start`
- Displays **thermal image only** (no camera); each 4×4 thermal cell maps to one TFT pixel
- Thermal grid: 32×24 cells → 240×240 display
- No dependency on camera pipe2

**Expected Log Output (success):**

```
[DRIVER] ST7789VW: init start (SPI6)
[DRIVER] ST7789VW: init done, 240x240
[DRIVER] sexp: Initializing sensors...
[DRIVER] sexp: SHT3x initialized (periodic 2Hz)
[DRIVER] sexp: ALS initialized
[DRIVER] lsm6dsr: init OK (addr=0x6A, id=0x6B, default: acc=12.5Hz/±2g, gyro=12.5Hz/±2000dps)
[DRIVER] sexp: LSM6DSR initialized
[DRIVER] vl53l1x: init OK (addr=0x29, sensor_id=0xEACC)
[DRIVER] sexp: VL53L1X initialized
[DRIVER] dts6012m: init OK (addr=0x51)
[DRIVER] sexp: DTS6012M initialized
[DRIVER] mlx90642: init OK (addr=0x66)  rate=2Hz  emissivity=0x3D71  Treflected=-134 (-1.34 C)
[DRIVER] sexp: MLX90642 initialized
[DRIVER] sexp: started in IR mode (TFT 240x240, thermal image 32x24 cells 4x4)
```

---

### 3.3 Stop Sensor Example

```
sexp stop
```

**Behavior:**

- Stops preview/thermal thread and sensor threads
- Releases TFT and other resources
- Does **not** stop camera or pipe2

**Expected Log Output:**

```
[DRIVER] sexp: stopped, resources released (camera/pipe2 unchanged)
```

---

## 4. Sensor Init Verification Summary

| Sensor   | Addr | Init Success Log                                       |
|----------|------|--------------------------------------------------------|
| SHT3x    | 0x44 | `sexp: SHT3x initialized (periodic 2Hz)`               |
| LTR-31x  | 0x22 | `sexp: ALS initialized`                                |
| LSM6DSR  | 0x6a | `lsm6dsr: init OK (addr=0x6A, id=0x6B, ...)`           |
| VL53L1X  | 0x29 | `vl53l1x: init OK (addr=0x29, sensor_id=0xEACC)`       |
| DTS6012M | 0x51 | `dts6012m: init OK (addr=0x51)`                        |
| MLX90642 | 0x66 | `mlx90642: init OK (addr=0x66) rate=2Hz emissivity=...`|
| ST7789VW | SPI6 | `ST7789VW: init done, 240x240`                         |

---

## 5. Individual Sensor Commands (Optional)

For deeper testing, use the per-sensor commands. See `sensor_exemple.h` for full reference.

| Command                  | Description                                      |
|--------------------------|--------------------------------------------------|
| `als init` / `als read`  | LTR-31x ALS init and read ALS/IR counts          |
| `sht3x init [addr]`      | SHT3x init (0x44 or 0x45)                        |
| `mlx90642 init [addr]`   | MLX90642 init, measure, dump, rate, emissivity   |
| `dts6012m init [addr]`   | DTS6012M init, read, laser on/off                |

---

## 6. Troubleshooting

| Symptom                      | Possible Cause                             | Action                                   |
|-----------------------------|--------------------------------------------|------------------------------------------|
| `--` at expected addr       | Sensor not connected or wrong address      | Check wiring, power, I2C pull-ups         |
| `sexp: SHT3x init failed`   | SHT3x missing or bus fault                 | Run `i2c_tool detect`, verify 0x44        |
| `sexp: ALS init failed`     | LTR-31x missing                            | Verify 0x22 in I2C scan                   |
| `sexp: LSM6DSR init failed` | LSM6DSR at 0x6a or 0x6b not detected       | Check SA0 pin, verify I2C address         |
| `sexp: VL53L1X init failed` | VL53L1X not found                          | Verify 0x29, check XSHUT and power        |
| `sexp: DTS6012M init failed`| DTS6012M not found                         | Verify 0x51, ensure laser power OK        |
| `sexp: MLX90642 init failed`| MLX90642 not found                         | Verify 0x66, check power and SDA/SCL      |
| `TFT init failed`           | SPI6 or display connection issue           | Check SPI6 wiring and display power       |

---

## 7. Test Sequence (Recommended)

1. `i2c_tool detect` – confirm all expected sensors appear.
2. `sexp start ir` – verify TFT and thermal image without camera.
3. `sexp stop` – confirm clean stop.
4. (Optional) `sexp start` – verify camera preview mode if pipe2 is running.
5. (Optional) `sexp stop` – release resources.
6. (Optional) `record test.wav 10` then `play test.wav` – verify the audio codec (independent of the TFT, see section 8).

---

## 8. Audio Test (NAU881x)

Audio record/playback runs on the `codec` component (`Custom/Hal/codec.c`), registered at boot together with the other SensorExt commands. The legacy `nau881x` demo commands (I2S6-based) still exist in the firmware but are not the supported path.

### 8.1 Hardware and Initialization

- Codec: NAU881x on I2C port 1, 7-bit address `0x1a` (visible in `i2c_tool detect`)
- Audio data: SAI1 — Block A records (mic), Block B plays (speaker); fixed 16 kHz / 16-bit, codec in I2S slave mode
- The TFT (SPI6) and the audio codec share no peripheral or pins; both can be tested in the same session
- The codec initializes **automatically at boot** (SAI1 + I2C + codec bring-up). There is no init command; the commands below are available shortly after boot
- Audio commands are compiled in non-HaLow builds only (`NETIF_WIFI_HALOW_IS_ENABLE = 0`), same as the other SensorExt commands

### 8.2 Command Reference

| Command   | Usage                  | Description                                                        |
|-----------|------------------------|--------------------------------------------------------------------|
| `record`  | `record [file] [secs]` | Record mic to WAV (default `record.wav`, 10 s); auto-stops when the duration elapses |
| `stoprec` | `stoprec`              | Stop recording early; WAV header is fixed up before close          |
| `play`    | `play [file]`          | Play a WAV file (default `record.wav`)                             |
| `stopplay`| `stopplay`             | Stop playback early                                                |
| `micvol`  | `micvol [0-100]`       | Mic (PGA) volume in percent; 0 = mute                              |
| `spkvol`  | `spkvol [0-100]`       | Speaker volume in percent; 0 = mute (capped at 0 dB)               |
| `dacvol`  | `dacvol [0-100]`       | DAC volume in percent; 0 = soft mute                               |
| `bypass`  | `bypass on\|off`       | Route mic directly to speaker (live monitoring)                    |
| `audiotest` | `audiotest [file]`   | Read an audio file back twice and byte-compare (storage read consistency; file must be ≥ 32 KB) |

**Notes:**

- WAV I/O is fixed at **16 kHz / 16-bit**. `play` rejects other sample rates: `Error: Only 16KHz sample rate is supported! (Current: 44100)`
- Each `record` start resets the mic PGA gain to a fixed value, and each `play` start resets DAC gain / speaker volume. Run the volume commands **while** a session is active for live adjustment
- `bypass off` does not re-select the DAC playback mix; start `play` (or reboot) to restore the normal playback path

### 8.3 Test Sequence

**Prerequisites:** NAU881x populated, I2C port 1, file system available for record/play, non-HaLow build.

1. **Verify codec on the bus:**
   ```
   i2c_tool detect
   ```
   Expected: device at `0x1a`.

2. **Test playback:**
   ```
   play dx.wav
   stopplay
   ```
   Use `stopplay` to stop early. Running `play` twice prints `Already playing!`. The "Play finished" log is only visible at DRIVER debug level.

3. **Adjust volume live (while playing):**
   ```
   spkvol 80
   dacvol 80
   ```
   Expected: `Speaker volume set to 80%` / `DAC volume set to 80%`.

4. **Test record:**
   ```
   record test.wav 10
   ```
   Expected:
   ```
   Start recording: test.wav, 10 seconds
   ```
   Recording stops automatically after 10 s (or run `stoprec`). The output is a 16 kHz / 16-bit stereo WAV (~64 KB per second).

5. **Verify recording by playback:**
   ```
   play test.wav
   ```

6. **Mic monitoring (bypass):**
   ```
   bypass on
   ```
   Expected: `MIC bypass to SPK enabled.` — speak into the mic; audio comes from the speaker. Restore the normal playback path with `play`.

7. **(Optional) file read-back check:**
   ```
   audiotest test.wav
   ```
   Expected: `Audio test done: test.wav` (compares two full reads of the file byte-by-byte).

---

### 8.4 Troubleshooting

| Symptom | Action |
|---------|--------|
| `0x1a` not in I2C scan | Check codec population and I2C1 wiring |
| Audio commands not found | Build is HaLow (`NETIF_WIFI_HALOW_IS_ENABLE=1`); audio needs a non-HaLow build |
| `Error: Only 16KHz sample rate is supported!` | Resample the WAV to 16 kHz, 16-bit |
| No sound during `play` | Set `spkvol`/`dacvol` above 0; verify the WAV is 16 kHz; check speaker connection |
| Recording is silent | Raise the gain with `micvol` **during** recording (each `record` start resets it) |
| Distorted playback | Lower `dacvol`/`spkvol`; speaker output is already capped at 0 dB |
