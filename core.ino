#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_LSM6DS3.h>
#include <Adafruit_MPL115A2.h>

// Pin definitions
const int SD_CS = 10;         // SPI CS for microSD on UNO
const int PHOTO_PIN = A0;     // Photoresistor analog input
const char DATA_FILENAME[] = "cubelog.csv";
const char JOURNAL_FILENAME[] = "cubelog.journal";
const unsigned long MAX_LOG_FILE_BYTES = 1024UL * 1024UL; // rollover at 1 MB

// Sensor objects
RTC_PCF8523 rtc;
Adafruit_LSM6DS3 lsm6ds3;
Adafruit_MPL115A2 mpl115A2;

unsigned long lastLogMillis = 0;
const unsigned long LOG_INTERVAL_MS = 10000; // 10 seconds

void recoverJournal();
void checkLogRollover();
void rotateLogFile();

void setup() {
  Serial.begin(115200);
  while (!Serial) ;

  Serial.println("CubeSat logger init...");

  Wire.begin();

  if (!rtc.begin()) {
    Serial.println("Error: PCF8523 RTC not found!");
    while (1);
  }
  if (!rtc.initialized() || rtc.lostPower()) {
    Serial.println("RTC lost power, setting to compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  if (!lsm6ds3.begin_I2C()) {
    Serial.println("Error: LSM6DS3 not detected!");
    while (1);
  }
  if (!mpl115A2.begin()) {
    Serial.println("Error: MPL115A2 not detected!");
    while (1);
  }

  Serial.print("Initializing SD card on CS=");
  Serial.println(SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("Error: SD card init failed!");
    while (1);
  }

  recoverJournal();

  if (!SD.exists(DATA_FILENAME)) {
    File headerFile = SD.open(DATA_FILENAME, FILE_WRITE);
    if (headerFile) {
      headerFile.println("UTC_ISO8601,unix_ts_s,pressure_hPa,temp_C,altitude_m,uv_index_est,uv_lux_est,photo_raw,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps");
      headerFile.close();
    }
  }

  checkLogRollover();

  Serial.println("Initialization complete.");
}

void loop() {
  if (millis() - lastLogMillis >= LOG_INTERVAL_MS) {
    lastLogMillis = millis();
    logSensorRow();
  }
}

void logSensorRow() {
  DateTime now = rtc.now();
  uint32_t unix_ts = now.unixtime();

  // Pressure/temperature/altitude
  float pressure = mpl115A2.getPressure();  // hPa
  float temperature = mpl115A2.getTemperature(); // C
  float altitude = 44330.0 * (1.0 - pow(pressure / 1013.25, 0.1903));

  // Photoresistor UV proxy
  int photo_raw = analogRead(PHOTO_PIN);
  float uv_index_est = (photo_raw / 1023.0) * 11.0;      // scale as approximate UV index
  float uv_lux_est = (photo_raw / 1023.0) * 2000.0;      // arbitrary convert to lux range

  // Inertial data
  sensors_event_t accel, gyro, temp;
  lsm6ds3.getEvent(&accel, &gyro, &temp);

  String row = "";
  row += now.timestamp(); row += ",";
  row += String(unix_ts); row += ",";
  row += String(pressure, 2); row += ",";
  row += String(temperature, 2); row += ",";
  row += String(altitude, 2); row += ",";
  row += String(uv_index_est, 2); row += ",";
  row += String(uv_lux_est, 1); row += ",";
  row += String(photo_raw); row += ",";
  row += String(accel.acceleration.x, 4); row += ",";
  row += String(accel.acceleration.y, 4); row += ",";
  row += String(accel.acceleration.z, 4); row += ",";
  row += String(gyro.gyro.x, 4); row += ",";
  row += String(gyro.gyro.y, 4); row += ",";
  row += String(gyro.gyro.z, 4);

  // Journal write for power-fail recovery
  File journalFile = SD.open(JOURNAL_FILENAME, FILE_WRITE);
  if (!journalFile) {
    Serial.println("Error: Cannot open journal file for write");
    return;
  }
  journalFile.println(row);
  journalFile.flush();
  journalFile.close();

  File dataFile = SD.open(DATA_FILENAME, FILE_WRITE);
  if (dataFile) {
    dataFile.println(row);
    dataFile.flush();
    dataFile.close();

    // Commit success: clear journal
    SD.remove(JOURNAL_FILENAME);

    Serial.println(row);
    Serial.println("Row saved");

    // Roll over main file if too large
    checkLogRollover();
  } else {
    Serial.println("Error: Cannot open data file for write");
  }
}

void recoverJournal() {
  if (!SD.exists(JOURNAL_FILENAME)) {
    return;
  }

  File journalFile = SD.open(JOURNAL_FILENAME, FILE_READ);
  if (!journalFile) {
    Serial.println("Error: Cannot open journal file on recovery");
    return;
  }

  File dataFile = SD.open(DATA_FILENAME, FILE_WRITE);
  if (!dataFile) {
    Serial.println("Error: Cannot open data file for recovery append");
    journalFile.close();
    return;
  }

  while (journalFile.available()) {
    String line = journalFile.readStringUntil('\n');
    if (line.length() == 0) continue;
    dataFile.println(line);
  }

  dataFile.flush();
  dataFile.close();
  journalFile.close();
  SD.remove(JOURNAL_FILENAME);
  Serial.println("Recovered journal to main log");
}

void checkLogRollover() {
  File dataFile = SD.open(DATA_FILENAME, FILE_READ);
  if (!dataFile) {
    return;
  }

  unsigned long fileSize = dataFile.size();
  dataFile.close();

  if (fileSize >= MAX_LOG_FILE_BYTES) {
    rotateLogFile();
  }
}

void rotateLogFile() {
  DateTime now = rtc.now();
  char backupName[32];
  snprintf(backupName, sizeof(backupName), "cubelog_%04u%02u%02u_%02u%02u%02u.csv", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  if (SD.exists(backupName)) {
    SD.remove(backupName);
  }

  if (!SD.rename(DATA_FILENAME, backupName)) {
    Serial.println("Warning: Rollover rename failed");
    return;
  }

  File headerFile = SD.open(DATA_FILENAME, FILE_WRITE);
  if (headerFile) {
    headerFile.println("UTC_ISO8601,unix_ts_s,pressure_hPa,temp_C,altitude_m,uv_index_est,uv_lux_est,photo_raw,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps");
    headerFile.close();
    Serial.print("Log rollover complete. New file: ");
    Serial.println(DATA_FILENAME);
  } else {
    Serial.println("Warning: Could not create new log after rollover");
  }
}
