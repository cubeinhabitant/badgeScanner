//
// Badge Scanner puzzle
//

#include <Wire.h>
#include "USBHost_t36.h"
#include "SdFat.h"
#include "Waveshare_LCD1602_RGB.h"

#define DEBUG // turn on debug serial output

#ifdef DEBUG
  #define DEBUG_PRINT(x)    Serial.print (x)
  #define DEBUG_PRINTLN(x)  Serial.println (x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 3

// Use built-in SD for SPI modes on Teensy 3.5/3.6.
// Teensy 4.0 use first SPI port.
// SDCARD_SS_PIN is defined for the built-in SD on some boards.
#ifndef SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SS;
#else  // SDCARD_SS_PIN
// Assume built-in SD is used.
const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif  // SDCARD_SS_PIN

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 3

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif  ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS

#if SD_FAT_TYPE == 0
SdFat sd;
File file;
#elif SD_FAT_TYPE == 1
SdFat32 sd;
File32 file;
#elif SD_FAT_TYPE == 2
SdExFat sd;
ExFile file;
#elif SD_FAT_TYPE == 3
SdFs sd;
FsFile file;
#else  // SD_FAT_TYPE
#error Invalid SD_FAT_TYPE
#endif  // SD_FAT_TYPE

//------------------------------------------
// Stor error strings in flash to save RAM.
// Store error strings in flash to save RAM.
#define error(s) sd.errorHalt(&Serial, F(s))
//------------------------------------------

USBHost myusb;
KeyboardController keyboard1(myusb); // magnetic stripe reader
Waveshare_LCD1602_RGB lcd(16,2); // RGB LCD display

const uint8_t BADGE_ID_LENGTH = 4; // number of characters in a badge id
char badge[BADGE_ID_LENGTH + 1]; // a 'read' badge id
uint8_t read = 0; // tracks the number of badge characters read
char line[40]; // used to read lines from config file

//
// Structure for configuration information
//
struct Config {
  char correctBadge[BADGE_ID_LENGTH + 1];
};

Config conf; // Configuration information

// Badge inventory
// Name index must match the badge ID index
const char* badgeNames[] = {"Beth", "Finn", "Cathy", "Heather", "Tom",  "Nate"};
const char* badgeIds[] =   {"1001", "1002", "1003",  "1004",    "1005", "1006"};

// States for reading a badge id from the magnetic stripe reader
const uint8_t STATE_WAITING_FOR_START = 1;
const uint8_t STATE_EXPECT_BADGE_DIGIT = 2;
const uint8_t STATE_EXPECT_END = 3;

uint8_t state = STATE_WAITING_FOR_START; // state for state machine and initialize

bool puzzleSolved = false;
const int showFailedMessagePeriod = 3000; // milliseconds to display the failed message
unsigned long failedMessageShown = 0;

void setup() {
  while (!Serial && millis() < 3000)
    ;  // wait for Arduino Serial Monitor

  Serial.println(F("\nBadge Scanner puzzle\n"));

  strcpy(conf.correctBadge, "1001"); // default correct badge

  // read config from SD card
  if (readConfig()) {
    Serial.println(F("Error reading configuration from SD card. Using defaults."));
  }

  DEBUG_PRINT(F("Correct badge: "));
  DEBUG_PRINTLN(conf.correctBadge);

  myusb.begin();

  keyboard1.attachPress(OnPress);

  lcd.init();
  displayDefaultMessage();
}

void loop() {
  myusb.Task();

  // non-blocking display of "failed" message
  if (failedMessageShown) {
    if (millis() > failedMessageShown + showFailedMessagePeriod) {
      displayDefaultMessage();
      failedMessageShown = 0;
    }
  }
}

int readConfig() {
  // Initialize the SD.
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
    return 1;
  }

  // Open the file
  if (!file.open("config.txt", FILE_READ)) {
    error("open failed");
    return 2;
  }

  while (file.available()) {
    int n = file.fgets(line, sizeof(line));
    if (n <= 0) {
      error("fgets failed");
      continue;
    }
    if (line[n-1] != '\n' && n == (sizeof(line) -1)) {
      error("line too long");
      continue;
    }
    if (!parseLine(line)) {
      error("parseLine failed");
      continue;
    }
  }
  file.close();
  return 0;
}

bool parseLine(char* str) {
  char* name;
  char* value;

  // set strtok to start of line.
  name = strtok(str, "=");
  if (!name) return false;

  // subsequent calls to strtok expect a null pointer
  value = strtok(nullptr, "=");
  if (!value) return false;

  DEBUG_PRINT(F("Parsed name ["));
  DEBUG_PRINT(name);
  DEBUG_PRINT(F("], value ["));
  DEBUG_PRINT(value);
  DEBUG_PRINTLN(F("]"));

  if (!strcmp("badge.correct", name)) {
    strcpy(conf.correctBadge, value);
    Serial.print(F("Configured correct badge: "));
    Serial.println(value);
  }

  return true;
}

void solved(char* badgeId) {
  DEBUG_PRINT(F("Solved! Corrrect badge ["));
  DEBUG_PRINT(conf.correctBadge);
  DEBUG_PRINT(F("] matched read badge ["));
  DEBUG_PRINT(badge);
  DEBUG_PRINTLN(F("]"));

  // cancel any pending clearing of failed message as puzzle is now solved
  failedMessageShown = 0;

  if (puzzleSolved) {
    // reset puzzle
    puzzleSolved = false;
    // update display
    displayDefaultMessage();

    // deactivate relay
    // TODO: deactivate relay
  } else {
    // puzzle newly solved
    puzzleSolved = true;
    // update display
    int badgeIndex = BadgeIndex(badgeId);
    const char* name;
    if (badgeIndex >= 0) {
      name = badgeNames[badgeIndex];
    } else {
      name = "Unknown";
    }
    displayMessage(name, "Logged in", 0, 255, 0);

    // activate relay
    // TODO: activate relay
  }
}

void failed(char* badgeId) {
  DEBUG_PRINT(F("FAIL! Corrrect badge ["));
  DEBUG_PRINT(conf.correctBadge);
  DEBUG_PRINT(F("] matched read badge ["));
  DEBUG_PRINT(badge);
  DEBUG_PRINTLN(F("]"));

  if (!puzzleSolved) {
    // update display
    int badgeIndex = BadgeIndex(badgeId);
    const char* name;
    if (badgeIndex >= 0) {
      name = badgeNames[badgeIndex];
    } else {
      name = "Unknown";
    }
    displayMessage(name, "Not scheduled", 255, 0, 0);
    failedMessageShown = millis();
  }
}

/**
 * Compare a badge value to an expected badge value.
 * Returns 'true' if the badge values match, 'false' otherwise.
 */
bool BadgeMatch(char* expected_badge, char* compare_badge) {
  if (strcmp(expected_badge, compare_badge) == 0) {
    return true;
  }
  return false;
}

int BadgeIndex(char* badgeId) {
  int len = sizeof(badgeIds)/sizeof(badgeIds[0]);
  for (int i = 0; i < len; i++) {
    if (strcmp(badgeIds[i], badgeId) == 0) {
      return i;
    }
  }

  return -1;
}

void OnPress(int key)
{
  DEBUG_PRINT(F("key '"));
  switch (key) {
    case KEYD_UP       : DEBUG_PRINT(F("UP")); break;
    case KEYD_DOWN    : DEBUG_PRINT(F("DN")); break;
    case KEYD_LEFT     : DEBUG_PRINT(F("LEFT")); break;
    case KEYD_RIGHT   : DEBUG_PRINT(F("RIGHT")); break;
    case KEYD_INSERT   : DEBUG_PRINT(F("Ins")); break;
    case KEYD_DELETE   : DEBUG_PRINT(F("Del")); break;
    case KEYD_PAGE_UP  : DEBUG_PRINT(F("PUP")); break;
    case KEYD_PAGE_DOWN: DEBUG_PRINT(F("PDN")); break;
    case KEYD_HOME     : DEBUG_PRINT(F("HOME")); break;
    case KEYD_END      : DEBUG_PRINT(F("END")); break;
    case KEYD_F1       : DEBUG_PRINT(F("F1")); break;
    case KEYD_F2       : DEBUG_PRINT(F("F2")); break;
    case KEYD_F3       : DEBUG_PRINT(F("F3")); break;
    case KEYD_F4       : DEBUG_PRINT(F("F4")); break;
    case KEYD_F5       : DEBUG_PRINT(F("F5")); break;
    case KEYD_F6       : DEBUG_PRINT(F("F6")); break;
    case KEYD_F7       : DEBUG_PRINT(F("F7")); break;
    case KEYD_F8       : DEBUG_PRINT(F("F8")); break;
    case KEYD_F9       : DEBUG_PRINT(F("F9")); break;
    case KEYD_F10      : DEBUG_PRINT(F("F10")); break;
    case KEYD_F11      : DEBUG_PRINT(F("F11")); break;
    case KEYD_F12      : DEBUG_PRINT(F("F12")); break;
    default: DEBUG_PRINT((char)key); break;
  }
  DEBUG_PRINTLN(F("'"));

  switch (key) {
    case '%':
      if (state == STATE_WAITING_FOR_START) {
        // clear badge
        memset(badge, '\0', sizeof(badge));
        read = 0;
        // change state to expect receiving the badge id
        DEBUG_PRINTLN(F("State change to STATE_EXPECT_BADGE_DIGIT"));
        state = STATE_EXPECT_BADGE_DIGIT;
      }
      break;

    case '?':
      // end character
      if (state == STATE_EXPECT_END) {
        if (BadgeMatch(conf.correctBadge, badge)) {
          solved(badge);
        } else {
          failed(badge);
        }
      }
      // clear badge
      memset(badge, '\0', sizeof(badge));
      read = 0;
      // change state to look for start of new badge id
      DEBUG_PRINTLN(F("State change to STATE_WAITING_FOR_START"));
      state = STATE_WAITING_FOR_START;
      break;

    default:
      if (state == STATE_EXPECT_BADGE_DIGIT) {
        if (read < BADGE_ID_LENGTH) {
          badge[read++] = key;
        }

        if (read >= BADGE_ID_LENGTH) {
          // read enough, expect the end indication
          DEBUG_PRINTLN(F("State change to STATE_EXPECT_END"));
          state = STATE_EXPECT_END;
        }
      }
      break;
  }
}

void displayMessage(const char* line1, const char* line2, int r, int g, int b) {
  lcd.clear();
  lcd.setRGB(r, g, b);
  if (line1) {
    lcd.setCursor(0, 0);
    lcd.send_string(line1);
  }
  if (line2) {
    lcd.setCursor(0, 1);
    lcd.send_string(line2);
  }
}

void displayDefaultMessage() {
  displayMessage("Swipe ID", "to log in", 255, 185, 0);
}