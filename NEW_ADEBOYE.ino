/*
 * Automated Fish Feeding System - Enhanced Navigation Version
 * 
 * PIN CONNECTIONS:
 * ================
 * 
 * LCD (16x2 I2C) - Address 0x27:
 * - SDA → A4 (Uno) / Pin 20 (Mega)
 * - SCL → A5 (Uno) / Pin 21 (Mega)
 * - VCC → 5V
 * - GND → GND
 * 
 * RTC DS3231 Module:
 * - SDA → A4 (Uno) / Pin 20 (Mega)
 * - SCL → A5 (Uno) / Pin 21 (Mega)
 * - VCC → 5V
 * - GND → GND
 * 
 * Servo Motor (Feeding Mechanism):
 * - Signal → Pin 9
 * - VCC → 5V (or external power if needed)
 * - GND → GND
 * 
 * Relay Module (Pump Control):
 * - Signal → Pin 10
 * - VCC → 5V
 * - GND → GND
 * 
 * Keypad (4x5):
 * - Row Pins → 2, 3, 4, 5, 6
 * - Column Pins → 7, 8, A0, A1
 * 
 * NEW FEATURES:
 * - F2: Shows "Are you sure?" confirmation before canceling
 * - L/R arrows: Navigate through input characters
 * - Number keys: Replace character at cursor position
 * - #: Pause/Resume operations
 * - Time zone: Set to West African Time (WAT = UTC+1)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <Keypad.h>
#include <Servo.h>
#include <EEPROM.h>

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RTC Configuration
RTC_DS3231 rtc;

// Servo Configuration
Servo feedServo;
const int SERVO_PIN = 9;
const int SERVO_REST_POS = 0;
const int SERVO_FEED_POS = 90;

// Relay Configuration
const int RELAY_PIN = 10;

// Keypad Configuration (4x5)
const byte ROWS = 5;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'F','G','#','*'},
  {'1','2','3','U'},  // U = Up arrow
  {'4','5','6','D'},  // D = Down arrow
  {'7','8','9','E'},  // E = ESC
  {'L','0','R','N'}   // L = Left, R = Right, N = Enter
};
byte colPins[COLS] = {2, 3, 4, 5};
byte rowPins[ROWS] = {A0, A1, 8, 7, 6};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Menu states
enum MenuState {
  MAIN_DISPLAY,
  SET_PUMP_TIME,
  SET_PUMP_DURATION,
  SET_FEED_TIME,
  SET_FEED_QUANTITY,
  SET_CURRENT_TIME
};

MenuState currentState = MAIN_DISPLAY;

// EEPROM addresses for storing settings
#define EEPROM_INIT_FLAG 0
#define EEPROM_PUMP_HOUR 1
#define EEPROM_PUMP_MINUTE 2
#define EEPROM_PUMP_DURATION 3
#define EEPROM_FEED_HOUR 5
#define EEPROM_FEED_MINUTE 6
#define EEPROM_FEED_QUANTITY 7
#define EEPROM_MAGIC_NUMBER 42

// Pump settings
int pumpHour = 8;
int pumpMinute = 0;
int pumpDuration = 30;
bool pumpActivated = false;
bool pumpPaused = false;
unsigned long pumpStartTime = 0;
unsigned long pumpPausedTime = 0;
unsigned long pumpElapsedBeforePause = 0;

// Feeding settings
int feedHour = 9;
int feedMinute = 0;
int feedQuantity = 3;
bool feedActivated = false;
bool feedPaused = false;
int feedCount = 0;
unsigned long lastFeedTime = 0;

// Input buffer and cursor position
String inputBuffer = "";
int inputStep = 0;
int cursorPos = 0;  // Track cursor position for L/R navigation

// Function prototypes
void loadSettings();
void saveSettings();
void writeIntToEEPROM(int address, int value);
int readIntFromEEPROM(int address);
void cancelOperation();
void togglePause();
void updateInputDisplayWithCursor();

void setup() {
  Serial.begin(9600);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Fish Feeder v2.1");
  delay(2000);
  
  // Load settings from EEPROM
  loadSettings();
  
  // Initialize RTC
  if (!rtc.begin()) {
    lcd.clear();
    lcd.print("RTC Error!");
    while (1);
  }
  
  // Check if RTC lost power and set to West African Time (WAT = UTC+1)
  if (rtc.lostPower()) {
    lcd.clear();
    lcd.print("Setting WAT...");
    DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
    DateTime watTime = DateTime(compileTime.unixtime() + 3600);
    rtc.adjust(watTime);
    delay(1000);
  }
  
  // Initialize Servo
  feedServo.attach(SERVO_PIN);
  feedServo.write(SERVO_REST_POS);
  
  // Initialize Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  
  lcd.clear();
  displayMainScreen();
}

void loop() {
  DateTime now = rtc.now();
  
  // Check for keypad input
  char key = keypad.getKey();
  if (key) {
    handleKeypress(key);
  }
  
  // Update main display
  if (currentState == MAIN_DISPLAY) {
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 1000) {
      displayMainScreen();
      lastUpdate = millis();
    }
  }
  
  // Check pump schedule
  checkPumpSchedule(now);
  
  // Check feeding schedule
  checkFeedSchedule(now);
  
  // Handle active pump (only if not paused)
  if (pumpActivated && !pumpPaused) {
    unsigned long elapsed = pumpElapsedBeforePause + (millis() - pumpStartTime);
    if (elapsed >= (unsigned long)pumpDuration * 1000) {
      stopPump();
    }
  }
  
  // Handle active feeding (only if not paused)
  if (feedActivated && !feedPaused) {
    if (millis() - lastFeedTime >= 2000) {
      dispenseFood();
      lastFeedTime = millis();
      feedCount++;
      
      if (feedCount >= feedQuantity) {
        feedActivated = false;
        feedCount = 0;
        lcd.clear();
        lcd.print("FEEDING COMPLETE");
        delay(2000);
        lcd.clear();
        displayMainScreen();
      }
    }
  }
}

void displayMainScreen() {
  DateTime now = rtc.now();
  
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  if (now.hour() < 10) lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10) lcd.print("0");
  lcd.print(now.second());
  lcd.print(" ");
  
  lcd.setCursor(0, 1);
  lcd.print("F1:Menu ");
  
  if (pumpActivated) {
    if (pumpPaused) {
      lcd.print("PUMP||");
    } else {
      lcd.print("PUMP ON");
    }
  } else if (feedActivated) {
    if (feedPaused) {
      lcd.print("FEED||");
    } else {
      lcd.print("FEED ON");
    }
  } else {
    lcd.print("       ");
  }
}

void handleKeypress(char key) {
  // F2 - Cancel operation (works from anywhere)
  if (key == 'G') {
    cancelOperation();
    return;
  }
  
  // # - Pause/Resume operation (works from main display)
  if (key == '#' && currentState == MAIN_DISPLAY) {
    togglePause();
    return;
  }
  
  if (currentState == MAIN_DISPLAY) {
    if (key == 'F') {
      showMenu();
    }
  } else if (key == 'E') {
    inputBuffer = "";
    inputStep = 0;
    cursorPos = 0;
    currentState = MAIN_DISPLAY;
    lcd.noCursor();
    lcd.clear();
    displayMainScreen();
  } else if (key == 'N') {
    processInput();
  } else if (key >= '0' && key <= '9') {
    // Replace character at cursor position or append
    if (cursorPos < inputBuffer.length()) {
      inputBuffer.setCharAt(cursorPos, key);
      cursorPos++;
      if (cursorPos > inputBuffer.length()) {
        cursorPos = inputBuffer.length();
      }
    } else {
      inputBuffer += key;
      cursorPos = inputBuffer.length();
    }
    updateInputDisplayWithCursor();
  } else if (key == '*') {
    // Replace character at cursor position or append
    if (cursorPos < inputBuffer.length()) {
      inputBuffer.setCharAt(cursorPos, ':');
      cursorPos++;
      if (cursorPos > inputBuffer.length()) {
        cursorPos = inputBuffer.length();
      }
    } else {
      inputBuffer += ":";
      cursorPos = inputBuffer.length();
    }
    updateInputDisplayWithCursor();
  } else if (key == 'L') {
    // Move cursor left
    if (cursorPos > 0) {
      cursorPos--;
      updateInputDisplayWithCursor();
    }
  } else if (key == 'R') {
    // Move cursor right
    if (cursorPos < inputBuffer.length()) {
      cursorPos++;
      updateInputDisplayWithCursor();
    }
  }
}

void cancelOperation() {
  // Always show confirmation before canceling
  lcd.noCursor();
  lcd.clear();
  lcd.print("Are you sure?");
  lcd.setCursor(0, 1);
  lcd.print("N=Yes  E=No");
  
  // Wait for user confirmation
  while (true) {
    char key = keypad.getKey();
    if (key == 'N') {  // Enter key confirms cancellation
      // Check if there's an active operation to cancel
      if (pumpActivated) {
        stopPump();
        lcd.clear();
        lcd.print("PUMP CANCELLED");
        delay(1500);
      }
      
      if (feedActivated) {
        feedActivated = false;
        feedPaused = false;
        feedCount = 0;
        feedServo.write(SERVO_REST_POS);
        lcd.clear();
        lcd.print("FEED CANCELLED");
        delay(1500);
      }
      
      // Return to main menu
      currentState = MAIN_DISPLAY;
      inputBuffer = "";
      cursorPos = 0;
      lcd.clear();
      displayMainScreen();
      break;
    } else if (key == 'E') {  // ESC cancels the cancellation
      lcd.clear();
      
      // Return to appropriate state
      if (currentState == MAIN_DISPLAY) {
        lcd.print("Continuing...");
        delay(1000);
        displayMainScreen();
      } else {
        // Return to input screen
        lcd.print("Resuming input...");
        delay(1000);
        lcd.clear();
        
        // Redisplay the input screen
        switch (currentState) {
          case SET_PUMP_TIME:
            lcd.print("Pump Time (HH*MM)");
            break;
          case SET_PUMP_DURATION:
            lcd.print("Pump Duration");
            break;
          case SET_FEED_TIME:
            lcd.print("Feed Time (HH*MM)");
            break;
          case SET_FEED_QUANTITY:
            lcd.print("Feed Quantity");
            break;
          case SET_CURRENT_TIME:
            lcd.print("Set Time (HH*MM)");
            break;
        }
        updateInputDisplayWithCursor();
      }
      break;
    }
  }
}

void togglePause() {
  if (pumpActivated) {
    pumpPaused = !pumpPaused;
    if (pumpPaused) {
      pumpElapsedBeforePause += (millis() - pumpStartTime);
      digitalWrite(RELAY_PIN, HIGH);
      lcd.clear();
      lcd.print("PUMP PAUSED");
      lcd.setCursor(0, 1);
      lcd.print("# to resume");
      delay(1500);
    } else {
      pumpStartTime = millis();
      digitalWrite(RELAY_PIN, LOW);
      lcd.clear();
      lcd.print("PUMP RESUMED");
      delay(1000);
    }
    displayMainScreen();
  }
  
  if (feedActivated) {
    feedPaused = !feedPaused;
    if (feedPaused) {
      feedServo.write(SERVO_REST_POS);
      lcd.clear();
      lcd.print("FEED PAUSED");
      lcd.setCursor(0, 1);
      lcd.print("# to resume");
      delay(1500);
    } else {
      lastFeedTime = millis();
      lcd.clear();
      lcd.print("FEED RESUMED");
      delay(1000);
    }
    displayMainScreen();
  }
}

void showMenu() {
  lcd.clear();
  lcd.print("1:PumpTime");
  lcd.setCursor(0, 1);
  lcd.print("2:PumpDuration");
  
  while (true) {
    char key = keypad.getKey();
    if (key == 'G') {
      cancelOperation();
      break;
    } else if (key == '1') {
      currentState = SET_PUMP_TIME;
      inputBuffer = "";
      inputStep = 0;
      cursorPos = 0;
      lcd.clear();
      lcd.print("Pump Time (HH*MM)");
      updateInputDisplayWithCursor();
      break;
    } else if (key == '2') {
      currentState = SET_PUMP_DURATION;
      inputBuffer = "";
      inputStep = 0;
      cursorPos = 0;
      lcd.clear();
      lcd.print("Pump Duration");
      updateInputDisplayWithCursor();
      break;
    } else if (key == 'D') {
      showFeedMenu();
      break;
    } else if (key == 'U') {
      break;
    } else if (key == 'E') {
      currentState = MAIN_DISPLAY;
      lcd.clear();
      displayMainScreen();
      break;
    }
  }
}

void showFeedMenu() {
  lcd.clear();
  lcd.print("3:FeedTime");
  lcd.setCursor(0, 1);
  lcd.print("4:FeedQuantity");
  
  while (true) {
    char key = keypad.getKey();
    if (key == 'G') {
      cancelOperation();
      break;
    } else if (key == '3') {
      currentState = SET_FEED_TIME;
      inputBuffer = "";
      inputStep = 0;
      cursorPos = 0;
      lcd.clear();
      lcd.print("Feed Time (HH*MM)");
      updateInputDisplayWithCursor();
      break;
    } else if (key == '4') {
      currentState = SET_FEED_QUANTITY;
      inputBuffer = "";
      inputStep = 0;
      cursorPos = 0;
      lcd.clear();
      lcd.print("Feed Quantity");
      updateInputDisplayWithCursor();
      break;
    } else if (key == 'D') {
      showSettingsMenu();
      break;
    } else if (key == 'U') {
      showMenu();
      break;
    } else if (key == 'E') {
      currentState = MAIN_DISPLAY;
      lcd.clear();
      displayMainScreen();
      break;
    }
  }
}

void showSettingsMenu() {
  lcd.clear();
  lcd.print("5:Set Clock");
  lcd.setCursor(0, 1);
  lcd.print("                ");
  
  while (true) {
    char key = keypad.getKey();
    if (key == 'G') {
      cancelOperation();
      break;
    } else if (key == '5') {
      currentState = SET_CURRENT_TIME;
      inputBuffer = "";
      inputStep = 0;
      cursorPos = 0;
      lcd.clear();
      lcd.print("Set Time (HH*MM)");
      updateInputDisplayWithCursor();
      break;
    } else if (key == 'U') {
      showFeedMenu();
      break;
    } else if (key == 'E') {
      currentState = MAIN_DISPLAY;
      lcd.clear();
      displayMainScreen();
      break;
    }
  }
}

// Update display with cursor indicator
void updateInputDisplayWithCursor() {
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  
  // Show appropriate prompt based on state
  String prompt = "";
  int promptLen = 0;
  
  if (currentState == SET_PUMP_DURATION) {
    prompt = "Sec: ";
    promptLen = 5;
  } else if (currentState == SET_FEED_QUANTITY) {
    prompt = "(1-999): ";
    promptLen = 9;
  } else {
    prompt = "Enter: ";
    promptLen = 7;
  }
  
  lcd.print(prompt);
  lcd.print(inputBuffer);
  
  // Position cursor at current position
  lcd.setCursor(promptLen + cursorPos, 1);
  lcd.cursor();  // Show blinking cursor
}

void processInput() {
  lcd.noCursor();  // Hide cursor during processing
  
  int value;
  int colonPos;
  
  switch (currentState) {
    case SET_PUMP_TIME:
      colonPos = inputBuffer.indexOf(':');
      if (colonPos > 0) {
        pumpHour = inputBuffer.substring(0, colonPos).toInt();
        pumpMinute = inputBuffer.substring(colonPos + 1).toInt();
        
        if (pumpHour >= 0 && pumpHour <= 23 && pumpMinute >= 0 && pumpMinute <= 59) {
          saveSettings();
          lcd.clear();
          lcd.print("Pump Time Set!");
          lcd.setCursor(0, 1);
          if (pumpHour < 10) lcd.print("0");
          lcd.print(pumpHour);
          lcd.print(":");
          if (pumpMinute < 10) lcd.print("0");
          lcd.print(pumpMinute);
          lcd.print(" Dur:");
          lcd.print(pumpDuration);
          lcd.print("s");
          delay(2000);
          currentState = MAIN_DISPLAY;
          inputBuffer = "";
          cursorPos = 0;
          lcd.clear();
          displayMainScreen();
        } else {
          lcd.clear();
          lcd.print("Invalid Time!");
          delay(1500);
          inputBuffer = "";
          cursorPos = 0;
          lcd.clear();
          lcd.print("Pump Time (HH*MM)");
          updateInputDisplayWithCursor();
        }
      } else {
        lcd.clear();
        lcd.print("Use * for time!");
        lcd.setCursor(0, 1);
        lcd.print("Ex: 08*30");
        delay(2000);
        inputBuffer = "";
        cursorPos = 0;
        lcd.clear();
        lcd.print("Pump Time (HH*MM)");
        updateInputDisplayWithCursor();
      }
      break;
      
    case SET_PUMP_DURATION:
      value = inputBuffer.toInt();
      if (value > 0) {
        pumpDuration = value;
        saveSettings();
        lcd.clear();
        lcd.print("Duration Set!");
        lcd.setCursor(0, 1);
        lcd.print(pumpDuration);
        lcd.print(" seconds");
        delay(2000);
        currentState = MAIN_DISPLAY;
        inputBuffer = "";
        cursorPos = 0;
        lcd.clear();
        displayMainScreen();
      } else {
        lcd.clear();
        lcd.print("Invalid! (>0)");
        delay(1500);
        inputBuffer = "";
        cursorPos = 0;
        lcd.clear();
        lcd.print("Pump Duration");
        updateInputDisplayWithCursor();
      }
      break;
      
    case SET_FEED_TIME:
      colonPos = inputBuffer.indexOf(':');
      if (colonPos > 0) {
        feedHour = inputBuffer.substring(0, colonPos).toInt();
        feedMinute = inputBuffer.substring(colonPos + 1).toInt();
        
        if (feedHour >= 0 && feedHour <= 23 && feedMinute >= 0 && feedMinute <= 59) {
          saveSettings();
          lcd.clear();
          lcd.print("Feed Time Set!");
          lcd.setCursor(0, 1);
          if (feedHour < 10) lcd.print("0");
          lcd.print(feedHour);
          lcd.print(":");
          if (feedMinute < 10) lcd.print("0");
          lcd.print(feedMinute);
          lcd.print(" Qty:");
          lcd.print(feedQuantity);
          delay(2000);
          currentState = MAIN_DISPLAY;
          inputBuffer = "";
          cursorPos = 0;
          lcd.clear();
          displayMainScreen();
        } else {
          lcd.clear();
          lcd.print("Invalid Time!");
          delay(1500);
          inputBuffer = "";
          cursorPos = 0;
          lcd.clear();
          lcd.print("Feed Time (HH*MM)");
          updateInputDisplayWithCursor();
        }
      } else {
        lcd.clear();
        lcd.print("Use * for time!");
        lcd.setCursor(0, 1);
        lcd.print("Ex: 09*00");
        delay(2000);
        inputBuffer = "";
        cursorPos = 0;
        lcd.clear();
        lcd.print("Feed Time (HH*MM)");
        updateInputDisplayWithCursor();
      }
      break;
      
    case SET_FEED_QUANTITY:
      value = inputBuffer.toInt();
      if (value >= 1 && value <= 999) {
        feedQuantity = value;
        saveSettings();
        lcd.clear();
        lcd.print("Quantity Set!");
        lcd.setCursor(0, 1);
        lcd.print("Amount: ");
        lcd.print(feedQuantity);
        delay(2000);
        currentState = MAIN_DISPLAY;
        inputBuffer = "";
        cursorPos = 0;
        lcd.clear();
        displayMainScreen();
      } else {
        lcd.clear();
        lcd.print("Invalid! (1-999)");
        delay(1500);
        inputBuffer = "";
        cursorPos = 0;
        lcd.clear();
        lcd.print("Feed Quantity");
        updateInputDisplayWithCursor();
      }
      break;
      
    case SET_CURRENT_TIME:
      colonPos = inputBuffer.indexOf(':');
      if (colonPos > 0) {
        int newHour = inputBuffer.substring(0, colonPos).toInt();
        int newMinute = inputBuffer.substring(colonPos + 1).toInt();
        
        if (newHour >= 0 && newHour <= 23 && newMinute >= 0 && newMinute <= 59) {
          DateTime now = rtc.now();
          rtc.adjust(DateTime(now.year(), now.month(), now.day(), newHour, newMinute, 0));
          
          lcd.clear();
          lcd.print("Clock Set!");
          lcd.setCursor(0, 1);
          if (newHour < 10) lcd.print("0");
          lcd.print(newHour);
          lcd.print(":");
          if (newMinute < 10) lcd.print("0");
          lcd.print(newMinute);
          lcd.print(" WAT");
          delay(2000);
          currentState = MAIN_DISPLAY;
          inputBuffer = "";
          cursorPos = 0;
          lcd.clear();
          displayMainScreen();
        } else {
          lcd.clear();
          lcd.print("Invalid Time!");
          delay(1500);
          inputBuffer = "";
          cursorPos = 0;
          lcd.clear();
          lcd.print("Set Time (HH*MM)");
          updateInputDisplayWithCursor();
        }
      } else {
        lcd.clear();
        lcd.print("Use * for time!");
        lcd.setCursor(0, 1);
        lcd.print("Ex: 12*15");
        delay(2000);
        inputBuffer = "";
        cursorPos = 0;
        lcd.clear();
        lcd.print("Set Time (HH*MM)");
        updateInputDisplayWithCursor();
      }
      break;
  }
}

void checkPumpSchedule(DateTime now) {
  static bool pumpTriggered = false;
  
  if (now.hour() == pumpHour && now.minute() == pumpMinute && !pumpTriggered) {
    startPump();
    pumpTriggered = true;
  }
  
  if (now.minute() != pumpMinute) {
    pumpTriggered = false;
  }
}

void checkFeedSchedule(DateTime now) {
  static bool feedTriggered = false;
  
  if (now.hour() == feedHour && now.minute() == feedMinute && !feedTriggered && !feedActivated) {
    startFeeding();
    feedTriggered = true;
  }
  
  if (now.minute() != feedMinute) {
    feedTriggered = false;
  }
}

void startPump() {
  digitalWrite(RELAY_PIN, LOW);
  pumpActivated = true;
  pumpPaused = false;
  pumpStartTime = millis();
  pumpElapsedBeforePause = 0;
  
  lcd.clear();
  lcd.print("PUMP STARTED");
  lcd.setCursor(0, 1);
  lcd.print("Duration: ");
  lcd.print(pumpDuration);
  lcd.print("s");
  delay(1500);
}

void stopPump() {
  digitalWrite(RELAY_PIN, HIGH);
  pumpActivated = false;
  pumpPaused = false;
  pumpElapsedBeforePause = 0;
  
  lcd.clear();
  lcd.print("PUMP STOPPED");
  delay(2000);
  lcd.clear();
  displayMainScreen();
}

void startFeeding() {
  feedActivated = true;
  feedPaused = false;
  feedCount = 0;
  lastFeedTime = millis();
  
  lcd.clear();
  lcd.print("FEEDING STARTED");
  lcd.setCursor(0, 1);
  lcd.print("Qty: ");
  lcd.print(feedQuantity);
  delay(1500);
}

void dispenseFood() {
  feedServo.write(SERVO_FEED_POS);
  delay(500);
  feedServo.write(SERVO_REST_POS);
  
  if (currentState == MAIN_DISPLAY) {
    lcd.setCursor(0, 1);
    lcd.print("Feed: ");
    lcd.print(feedCount + 1);
    lcd.print("/");
    lcd.print(feedQuantity);
    lcd.print("    ");
  }
}

// EEPROM Functions
void loadSettings() {
  if (EEPROM.read(EEPROM_INIT_FLAG) == EEPROM_MAGIC_NUMBER) {
    pumpHour = EEPROM.read(EEPROM_PUMP_HOUR);
    pumpMinute = EEPROM.read(EEPROM_PUMP_MINUTE);
    pumpDuration = readIntFromEEPROM(EEPROM_PUMP_DURATION);
    feedHour = EEPROM.read(EEPROM_FEED_HOUR);
    feedMinute = EEPROM.read(EEPROM_FEED_MINUTE);
    feedQuantity = readIntFromEEPROM(EEPROM_FEED_QUANTITY);
    
    Serial.println("Settings loaded from EEPROM");
  } else {
    Serial.println("First time setup - using defaults");
    saveSettings();
  }
}

void saveSettings() {
  EEPROM.write(EEPROM_INIT_FLAG, EEPROM_MAGIC_NUMBER);
  EEPROM.write(EEPROM_PUMP_HOUR, pumpHour);
  EEPROM.write(EEPROM_PUMP_MINUTE, pumpMinute);
  writeIntToEEPROM(EEPROM_PUMP_DURATION, pumpDuration);
  EEPROM.write(EEPROM_FEED_HOUR, feedHour);
  EEPROM.write(EEPROM_FEED_MINUTE, feedMinute);
  writeIntToEEPROM(EEPROM_FEED_QUANTITY, feedQuantity);
  
  Serial.println("Settings saved to EEPROM");
}

void writeIntToEEPROM(int address, int value) {
  EEPROM.write(address, highByte(value));
  EEPROM.write(address + 1, lowByte(value));
}

int readIntFromEEPROM(int address) {
  return (EEPROM.read(address) << 8) + EEPROM.read(address + 1);
}