# cubesat
A CubeSat data logger using Arduino UNO-compatible hardware.

## Hardware
- Arduino UNO clone
- photoresistor (analog UV proxy, A0)
- Adafruit microSD breakout (CS=10)
- PCF8523 RTC
- LSM6DS3 gyro + accelerometer
- MPL115A2 temperature + pressure

## Firmware
- File: `core.cpp`
- Logs to `cubelog.csv` on microSD
- Creates journaling file `cubelog.journal` for power-fail recovery
- Rolls over file when size >= 1 MB to `cubelog_YYYYMMDD_HHMMSS.csv`

## CSV format
UTC_ISO8601, unix_ts_s, pressure_hPa, temp_C, altitude_m,
uv_index_est, uv_lux_est, photo_raw,
accel_x_g, accel_y_g, accel_z_g,
gyro_x_dps, gyro_y_dps, gyro_z_dps

## Usage
1. Install dependencies: `RTC`, `Adafruit_LSM6DS3`, `Adafruit_MPL115A2`, `SD`, `RTClib`.
2. Upload `core.cpp` to UNO.
3. Open Serial Monitor at 115200 for status + row confirmations.
4. Power cycle to verify journaling recovery and rollover.
5. Extract `.csv` from microSD to analyze UV + altitude + motion data.

