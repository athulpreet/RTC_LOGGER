#include <Arduino.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>

// --------------------- SD Card Pin Definitions ---------------------
#define SD_CS   PA4    // CS
#define SD_SCK  PA5    // SCK
#define SD_MISO PA6    // MISO
#define SD_MOSI PA7    // MOSI

// --------------------- UART1 for GPS & Debug -----------------------
//HardwareSerial Serial1(PA10, PA9); // RX=PA10, TX=PA9

// --------------------- GPS Buffer & Timezone -----------------------
char buffer[128];
int bufferIndex = 0;

// --------------------- Initialize RTC -----------------------------
RTC_DS3231 rtc;
char dateTimeStr[20]; // To store formatted date/time from RTC

// --------------------- GPS Data Struct -----------------------
struct GpsData {
  char lat[11];
  char ns;          // 'N' or 'S'
  char lon[11];
  char ew;          // 'E' or 'W'
  float speed;      // in km/h
  char mode;        // 'A' = Active, 'V' = Void
  bool dataReady;   // true if we got a valid RMC parse
} gpsData;

// --------------------- Keep track of last logged second ---------------------
static int lastLoggedSecond = -1;

// --------------------- Format RTC Date and Time ---------------------
void formatRTCDateTime(DateTime now, char* dateStr, char* timeStr) {
  // Format date as "DDMMYY"
  sprintf(dateStr, "%02d%02d%02d", now.day(), now.month(), now.year() % 100);
  
  // Format time as "HHMMSS"
  sprintf(timeStr, "%02d%02d%02d", now.hour(), now.minute(), now.second());
}

// --------------------- Parse RMC Sentence ---------------------
void parseRMC(char* sentence) {
  // Example: $GPRMC,HHMMSS,A,lat,NS,lon,EW,speed,angle,date,...
  // or       $GNRMC,HHMMSS,A,lat,NS,lon,EW,speed,angle,date,...
  char* token = strtok(sentence, ",");
  int fieldCount = 0;
  
  // Reset dataReady each new parse
  gpsData.dataReady = false;

  while (token != NULL) {
    switch(fieldCount) {
      case 2: // Status (A=Active, V=Void)
        if (strlen(token) > 0) {
          gpsData.mode = token[0];
        }
        break;
      case 3: // Latitude
        if (strlen(token) > 0) {
          strncpy(gpsData.lat, token, 10);
          gpsData.lat[10] = '\0';
        }
        break;
      case 4: // N/S
        if (strlen(token) > 0) {
          gpsData.ns = token[0];
        }
        break;
      case 5: // Longitude
        if (strlen(token) > 0) {
          strncpy(gpsData.lon, token, 10);
          gpsData.lon[10] = '\0';
        }
        break;
      case 6: // E/W
        if (strlen(token) > 0) {
          gpsData.ew = token[0];
        }
        break;
      case 7: // Speed in knots
        if (strlen(token) > 0) {
          gpsData.speed = atof(token) * 1.852; // knots -> km/h
        } else {
          gpsData.speed = 0.0;
        }
        break;
      case 9: // We don't need GPS date anymore since we'll use RTC
        gpsData.dataReady = true;
        break;
    }
    token = strtok(NULL, ",");
    fieldCount++;
  }

  // If we got valid GPS data and the fix is Active
  if (gpsData.dataReady && gpsData.mode == 'A') {
    // Get current date and time from RTC
    DateTime now = rtc.now();
    char rtcDate[7]; // DDMMYY
    char rtcTime[7]; // HHMMSS
    
    // Format RTC date and time
    formatRTCDateTime(now, rtcDate, rtcTime);
    
    // Print the GPS data with RTC timestamp to Serial1 for debugging
    Serial1.print(rtcDate);
    Serial1.print(",");
    Serial1.print(rtcTime);
    Serial1.print(",");
    Serial1.print(gpsData.lon);
    Serial1.print(gpsData.ew);
    Serial1.print(",");
    Serial1.print(gpsData.lat);
    Serial1.print(gpsData.ns);
    Serial1.print(",");
    Serial1.println(gpsData.speed, 1);

    // --------------------- EXACT 5s LOGGING (RTC Time Based) ---------------------
    // Log at 0, 5, 10, 15... seconds
    int currentSec = now.second();
    
    // If it's exactly a multiple of 5 seconds
    if ((currentSec % 5) == 0) {
      // And we haven't logged this particular second yet
      if (currentSec != lastLoggedSecond) {
        lastLoggedSecond = currentSec;

        // Log to SD
        File dataFile = SD.open("gps_log.txt", FILE_WRITE);
        if (dataFile) {
          dataFile.print(rtcDate);
          dataFile.print(",");
          dataFile.print(rtcTime);
          dataFile.print(",");
          dataFile.print(gpsData.lon);
          dataFile.print(gpsData.ew);
          dataFile.print(",");
          dataFile.print(gpsData.lat);
          dataFile.print(gpsData.ns);
          dataFile.print(",");
          dataFile.println(gpsData.speed, 1);
          dataFile.close();

          // Also print to Serial1 for debug
          Serial1.println("Data logged to SD (gps_log.txt). Logged line:");
          Serial1.print(rtcDate);  Serial1.print(",");
          Serial1.print(rtcTime);  Serial1.print(",");
          Serial1.print(gpsData.lon);   Serial1.print(gpsData.ew);
          Serial1.print(",");
          Serial1.print(gpsData.lat);   Serial1.print(gpsData.ns);
          Serial1.print(",");
          Serial1.println(gpsData.speed, 1);
        } else {
          Serial1.println("Error opening gps_log.txt for writing!");
        }
      }
    }

  } else {
    // If fix is Void
    if (gpsData.mode == 'V') {
      Serial1.println("GPS signal lost");
      
      // Get current date and time from RTC for logging
      DateTime now = rtc.now();
      char rtcDate[7]; // DDMMYY
      char rtcTime[7]; // HHMMSS
      
      // Format RTC date and time
      formatRTCDateTime(now, rtcDate, rtcTime);
      
      // Log even when GPS signal is lost
      int currentSec = now.second();
      
      // If it's exactly a multiple of 5 seconds
      if ((currentSec % 5) == 0) {
        // And we haven't logged this particular second yet
        if (currentSec != lastLoggedSecond) {
          lastLoggedSecond = currentSec;

          // Log to SD with "SL" for all GPS fields
          File dataFile = SD.open("gps_log.txt", FILE_WRITE);
          if (dataFile) {
            dataFile.print(rtcDate);
            dataFile.print(",");
            dataFile.print(rtcTime);
            dataFile.print(",");
            dataFile.print("SL"); // Longitude
            dataFile.print(",");
            dataFile.print("SL"); // Latitude
            dataFile.print(",");
            dataFile.println("SL"); // Speed
            dataFile.close();

            // Also print to Serial1 for debug
            Serial1.println("Data logged to SD (gps_log.txt) with Signal Lost markers:");
            Serial1.print(rtcDate);  Serial1.print(",");
            Serial1.print(rtcTime);  Serial1.print(",");
            Serial1.print("SL,");   // Longitude
            Serial1.print("SL,");   // Latitude
            Serial1.println("SL");  // Speed
          } else {
            Serial1.println("Error opening gps_log.txt for writing!");
          }
        }
      }
    }
  }
}

// --------------------- Setup (Initialize GPS + SD + RTC) ---------------------
void setup() {
  // Start Serial1 at 9600 for both GPS and debug prints
  Serial1.begin(9600);
  delay(1000);

  Serial1.println("\nGPS Logger with RTC Timestamp");

  // Initialize I2C for RTC
  Wire.begin();
  
  // Initialize RTC module
  if (!rtc.begin()) {
    Serial1.println("Couldn't find DS3231 RTC");
    while (1);
  }
  
  // Check if RTC lost power and needs to be set
  if (rtc.lostPower()) {
    Serial1.println("RTC lost power, setting to compile time");
    // Set RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  Serial1.println("DS3231 RTC initialized");

  // Configure SPI pins for SD card
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH); // Deselect SD card

  // Initialize SPI with conservative settings
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV128); // Very slow speed
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);

  Serial1.println("\nPower-up sequence:");
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Send dummy clock cycles
  for(int i = 0; i < 10; i++) {
    SPI.transfer(0xFF);
    Serial1.print(".");
  }
  Serial1.println(" Done");

  delay(100);

  Serial1.println("\nInitializing SD card...");
  Serial1.println("Using level-shifted configuration:");
  Serial1.println("CS   -> PA4  (with level shifter)");
  Serial1.println("SCK  -> PA5  (with level shifter)");
  Serial1.println("MISO -> PA6  (with level shifter)");
  Serial1.println("MOSI -> PA7  (with level shifter)");
  Serial1.println("VCC  -> 5V   (to module's VCC)");
  Serial1.println("GND  -> GND");

  // Attempt to initialize SD
  if (!SD.begin(SD_CS)) {
    Serial1.println("\nSD card initialization failed!");
    Serial1.println("\nTroubleshooting for level-shifted module:");
    Serial1.println("1. Power supply:");
    Serial1.println("   - Connect module's VCC to 5V (not 3.3V)");
    Serial1.println("   - Ensure good ground connection");
    Serial1.println("2. Check voltage levels:");
    Serial1.println("   - Module VCC should read ~5V");
    Serial1.println("   - Card VCC should read ~3.3V (from LDO)");
    Serial1.println("3. Level shifter connections:");
    Serial1.println("   - All signals should be properly level shifted");
    Serial1.println("   - No additional pullup resistors needed");
    Serial1.println("4. Try these steps:");
    Serial1.println("   - Power cycle the entire system");
    Serial1.println("   - Inspect the level shifter ICs");
    Serial1.println("   - Check for any burnt components");
    while(1); // Halt
  }

  Serial1.println("SD card initialization successful!");

  // Optional: Test file operations
  File dataFile = SD.open("test.txt", FILE_WRITE);
  if (dataFile) {
    Serial1.println("\nWriting to test.txt...");
    dataFile.println("Testing SD card with level shifter");
    dataFile.println("Module is working properly!");
    dataFile.close();
    Serial1.println("Write successful!");

    // Read back the file
    dataFile = SD.open("test.txt");
    if (dataFile) {
      Serial1.println("\nReading file contents:");
      while (dataFile.available()) {
        Serial1.write(dataFile.read());
      }
      dataFile.close();
      Serial1.println("\nRead successful!");
    }
  } else {
    Serial1.println("Error opening test file!");
  }

  // Clear GPS data struct
  memset(&gpsData, 0, sizeof(gpsData));
  gpsData.mode = 'V'; // Initial mode is Void until we get a valid fix
  
  // Initialize GPS data fields with default values
  strcpy(gpsData.lat, "0000.0000");
  gpsData.ns = 'N';
  strcpy(gpsData.lon, "00000.0000");
  gpsData.ew = 'E';
  gpsData.speed = 0.0;
  gpsData.dataReady = false;
}

// --------------------- Main Loop: Read & Parse GPS, Log with RTC Time ---------------------
void loop() {
  // Continuously read from Serial1 to get NMEA sentences
  while (Serial1.available() > 0) {
    char c = Serial1.read();

    if (c == '$') { // Start of NMEA
      bufferIndex = 0;
    }
    else if (c == '\n' || c == '\r') { // End of NMEA
      if (bufferIndex > 0) {
        buffer[bufferIndex] = '\0';
        // Check if it's RMC
        if (strstr(buffer, "GNRMC") || strstr(buffer, "GPRMC")) {
          parseRMC(buffer);
        }
      }
      bufferIndex = 0;
    }
    else if (bufferIndex < (int)sizeof(buffer) - 1) {
      buffer[bufferIndex++] = c;
    }
  }

  // Display current RTC time every second for debug purposes
  static unsigned long lastDisplayTime = 0;
  if (millis() - lastDisplayTime >= 1000) {
    lastDisplayTime = millis();
    
    // Get current date and time from RTC
    DateTime now = rtc.now();
    
    // Format and display date and time
    sprintf(dateTimeStr, "%04d/%02d/%02d %02d:%02d:%02d", 
            now.year(), now.month(), now.day(), 
            now.hour(), now.minute(), now.second());
    
    Serial1.print("RTC: ");
    Serial1.println(dateTimeStr);
  }
}
