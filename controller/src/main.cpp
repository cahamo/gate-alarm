// Gate alarm source code.
// See https://cahamo.delphidabbler.com/projects/gate-alarm/
//
// Copyright (c) 2024, Peter Johnson (gravatar.com/delphidabbler)
// MIT License: https://cahamo.mit-license.org/

#include <Arduino.h>
#include <ezButton.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

#define DEBUG
#include "debug.h"

#define LCD_WIDTH   16
#define LCD_HEIGHT   2

#define SECONDS_PER_MINUTE    60
#define MILLIS_PER_SECOND     1000
#define MILLIS_PER_MINUTE     ((unsigned long) MILLIS_PER_SECOND * SECONDS_PER_MINUTE)

#define DEBOUNCE_DELAY        50

#define MAGNET_SWITCH_PIN     2
#define ALARM_LED_PIN         11
#define ALARM_BUZZER_PIN      10
#define HEARTBEAT_LED_PIN     12

#define SUSPEND_OFF           0
#define SUSPEND_INFINITE    (-1)

boolean alarmSounding = false;
boolean gateOpen = false;
unsigned long suspendStartTime = 0;
long totalSuspendTime = SUSPEND_OFF;

// create ezButton object for magnetic reed switch & parallel test button switch
ezButton btnMagnet(MAGNET_SWITCH_PIN, INPUT);

#define DIGIT_ENTRY_BASE 10

const byte KEYPAD_ROWS = 4;
const byte KEYPAD_COLS = 3;

// Keypad has keys 0..9, star & hash
// 0..9 are used to eter suspend time, entering 0 cancels suspension
// # key enters suspension time, if entered. If no time entered infinite suspension is acivated.
// * key resets gate alarm, for use when gate has been closed. Cancels any alarm or suspension.
char keyPadKeys[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

// Pins used to read rows and columns from membrane keypad
byte rowPins[KEYPAD_ROWS] = {3, 4, 5, 6};
byte colPins[KEYPAD_COLS] = {7, 8, 9};

#define HASH_KEY (-1)
#define STAR_KEY (-2)
#define INVALID_KEY (-9)

int keypadValue(char key) {
  if (key >= '0' && key <= '9') return key - '0';
  if (key == '#') return HASH_KEY;
  if (key == '*') return STAR_KEY;
  return INVALID_KEY;
}

Keypad keypad = Keypad(makeKeymap(keyPadKeys), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);

long suspendTimeAccumulator = 0;
boolean isUpdatingSuspendTime = false;

LiquidCrystal_I2C lcd(0x27, LCD_WIDTH, LCD_HEIGHT);

unsigned long lastDisplayUpdate = 0;

// Time between display refreshes in ms
#define DISPLAY_UPDATE_DELTA      250

// Time alarm buzzer sounds & is silent in ms
#define ALARM_BUZZER_ON_TIME      1500
#define ALARM_BUZZER_OFF_TIME     1000
#define ALARM_BUZZER_CYCLE_TIME   (ALARM_BUZZER_ON_TIME + ALARM_BUZZER_OFF_TIME)

// Time alarm LED illuminates & is off in ms
#define ALARM_LED_ON_TIME         250
#define ALARM_LED_OFF_TIME        250
#define ALARM_LED_CYCLE_TIME      (ALARM_LED_ON_TIME + ALARM_LED_OFF_TIME)

// Time heartbeat LED illuminates & is off in ms
#define HEARTBEAT_LED_ON_TIME     100
#define HEARTBEAT_LED_OFF_TIME    8000
#define HEARTBEAT_LED_CYCLE_TIME  (HEARTBEAT_LED_ON_TIME + HEARTBEAT_LED_OFF_TIME)

// Time LED backlight stays on in ms
#define LCD_BACKLIGHT_TIMEOUT     10000

// Variables used to determine pulsing of alarm buzzer & LEDs
unsigned long alarmBuzzerPulseStartTime = 0;
unsigned long alarmLEDPulseStartTime = 0;
unsigned long heartbeatLEDPulseStartTime = millis();
unsigned long lcdBacklightTimeoutStartTime = 0;
// bool isLcdBacklightOn = true;

void switchLCDBacklightOn() {
  lcdBacklightTimeoutStartTime = millis();
  lcd.backlight();
}

void switchLCDBacklightOff() {
  lcd.noBacklight();
  lcdBacklightTimeoutStartTime = 0;
}

void setup() {

  // Enable serial port iff DEBUG is defined
  DBGbegin(9600);

  // Setup LCD
  lcd.init();
  lcd.clear();
  lcd.backlight();

  // Display splash screen
  lcd.setCursor(0, 0);
  lcd.print(F("** Gate Alarm **"));
  lcd.setCursor(0, 1);
  lcd.print(F("**   Welcome  **"));
  switchLCDBacklightOn();
  delay(2000);

  // Set up debounce time for magnet switch & parallel test button
  btnMagnet.setDebounceTime(DEBOUNCE_DELAY);

  // Set up alarm pins & ensure all off
  pinMode(ALARM_LED_PIN, OUTPUT);
  pinMode(ALARM_BUZZER_PIN, OUTPUT);
  pinMode(HEARTBEAT_LED_PIN, OUTPUT);
}

boolean isSuspended() {
  return totalSuspendTime != SUSPEND_OFF;
}

boolean isInfiniteSuspension() {
  return totalSuspendTime == SUSPEND_INFINITE;
}

void writeLinesOnLCD(String line1, String line2) {
  static String oldLine1 = "";
  static String oldLine2 = "";
  if ( (line1 != oldLine1) || (line2 != oldLine2)) {
    switchLCDBacklightOn();
    oldLine1 = line1;
    oldLine2 = line2;
    lcd.clear();
    int leftLine1 = ((LCD_WIDTH) - line1.length()) / 2;
    lcd.setCursor(leftLine1, 0);
    lcd.print(line1);
    int leftLine2 = ((LCD_WIDTH) - line2.length()) / 2;
    lcd.setCursor(leftLine2, 1);
    lcd.print(line2);
  }
}

void updateDisplay() {
  if (isUpdatingSuspendTime) {
    writeLinesOnLCD(F("Enter delay:"), String(suspendTimeAccumulator));
  }
  else if (isSuspended()) {
    if (isInfiniteSuspension()) {
      writeLinesOnLCD(F("Alarm"), F("Suspended"));
    }
    else {
      long millisRemaining = totalSuspendTime - millis() + suspendStartTime;
      unsigned int minsRemaining = millisRemaining / MILLIS_PER_MINUTE;
      unsigned int secsRemaining = (millisRemaining - minsRemaining * MILLIS_PER_MINUTE) / MILLIS_PER_SECOND;
      secsRemaining %= SECONDS_PER_MINUTE;
      String secsStr = String(secsRemaining);
      if (secsStr.length() == 1) {
        secsStr = "0" + secsStr;
      }
      writeLinesOnLCD(F("Alarm paused for"), String(minsRemaining) + F(":") + String(secsStr));
    }
  }
  else {
    if (gateOpen) {
      writeLinesOnLCD(F("** GATE **"), F("** OPEN **"));
    }
    else {
      writeLinesOnLCD(F("OK"), "");
    }
  }
}

void hideAlarmLED() {
  alarmLEDPulseStartTime = 0;
  digitalWrite(ALARM_LED_PIN, LOW);
}

void showAlarmLED() {
  digitalWrite(ALARM_LED_PIN, HIGH);
  alarmLEDPulseStartTime = millis();
}

void silenceAlarm() {
  if (alarmSounding) {
    alarmSounding = false;
    alarmBuzzerPulseStartTime = 0;
    digitalWrite(ALARM_BUZZER_PIN, LOW);
    DBGprintln(F("*** Alarm silenced"));
  }
}

void activateAlarm() {
  if (gateOpen) {
    if (!alarmSounding) {
      DBGprintln(F("*** ALARM ACTIVATED"));
      digitalWrite(ALARM_BUZZER_PIN, HIGH);
      alarmBuzzerPulseStartTime = millis();
      alarmSounding = true;
    }
  }
}

void cancelSuspension() {
  totalSuspendTime = SUSPEND_OFF;
  suspendStartTime = 0;
}

void openGate() {
  if (!gateOpen) {
    DBGprintln(F("*** Gate open"));
    gateOpen = true;
    showAlarmLED();
    if (! isSuspended() ) {
      activateAlarm();
    }
  }
}

void reset() {
  DBGprintln(F("*** Reset"));
  gateOpen = false;
  cancelSuspension();
  silenceAlarm();
  hideAlarmLED();
}

void processKeypadDigit(int digit) {
  DBGprint(F("Processing keypad DIGIT: "));
  DBGprintln(digit);
  if (isUpdatingSuspendTime) {
    suspendTimeAccumulator = suspendTimeAccumulator * DIGIT_ENTRY_BASE + digit;
    DBGprint(F("  Editing suspend time. Updated value = "));
    DBGprintln(suspendTimeAccumulator);
  }
  else {
    suspendTimeAccumulator = digit;
    isUpdatingSuspendTime = true;
    DBGprint(F("  Starting to edit suspend time. Starting value = "));
    DBGprintln(suspendTimeAccumulator);
  }
}

void processKeypadHash() {
  DBGprintln(F("Processing keypad HASH key"));
  if (isUpdatingSuspendTime) {
    if (suspendTimeAccumulator != 0) {
      totalSuspendTime = suspendTimeAccumulator * MILLIS_PER_MINUTE;
      DBGprint(F("  Entered suspend time of "));
      DBGprintln(totalSuspendTime);
    }
    else {
      totalSuspendTime = SUSPEND_OFF;
      DBGprintln(F("  Entered zero value for suspend time => turned suspension off"));
    }
    suspendTimeAccumulator = 0;
    isUpdatingSuspendTime = false;
  }
  else {
    // Hash button pressed on its own pauses alarm indefinately
    totalSuspendTime = SUSPEND_INFINITE;
    switchLCDBacklightOn(); // re-activates backlight if off and # key pressed twice in a row
    DBGprintln(F("  Pressed HASH key without entering value: entered infinite supension"));
  }

  DBGprint(F("  Result: "));
  if (isSuspended()) {
    DBGprint(F("Suspended, time in ms = "));
    DBGprintln(totalSuspendTime);
    if (alarmSounding) {
      silenceAlarm();
    }
    if (! isInfiniteSuspension() ) {
      suspendStartTime = millis();
    }
    else {
      suspendStartTime = 0;
    }
  }
  else {
    DBGprint(F("Not suspended, "));
    if (gateOpen) {
      DBGprintln(F("gate IS open (reactivating alarm)"));
      activateAlarm();
    }
    else {
      DBGprintln(F("gate NOT open (doing nothing)"));
    }
  }
}

void processKeypadStar() {
  DBGprintln(F("Processing keypad STAR key: resetting gate alarm"));
  reset();
}

void loop() {

  // MUST call the .loop() method for every ezButton each time round the loop
  btnMagnet.loop();

  // Gate is deemed to be open if either it really is or if test buton is pressed
  if (btnMagnet.isPressed()) {
    openGate();
  }

  // Check if a key has been pressed on keypad: act on it if so
  char keyPadKey = keypad.getKey();
  if (keyPadKey) {
    int keyVal = keypadValue(keyPadKey);
    if (keyVal >= 0 && keyVal <= 9) {
      processKeypadDigit(keyVal);
    }
    else if (keyVal == HASH_KEY) {
      processKeypadHash();
    }
    else if (keyVal == STAR_KEY) {
      processKeypadStar();
    }

  }

  // Check if any suspension has timed out
  if (isSuspended() && ! isInfiniteSuspension()) {
    if (millis() - suspendStartTime > (unsigned long) totalSuspendTime) {
      DBGprintln(F("*** Suspension timeout"));
      cancelSuspension();
      activateAlarm();
    }
  }

  // Display is updated every DISPLAY_UPDATE_DELTA ms
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_DELTA) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  // Check if alarm is sounding and time take action if so
  if (alarmSounding) {
    if (millis() - alarmBuzzerPulseStartTime > ALARM_BUZZER_CYCLE_TIME) {
      alarmBuzzerPulseStartTime = millis();
    }
    else {
      if (millis() - alarmBuzzerPulseStartTime < ALARM_BUZZER_ON_TIME) {
        digitalWrite(ALARM_BUZZER_PIN, HIGH);
      }
      else {
        digitalWrite(ALARM_BUZZER_PIN, LOW);
      }
    }
  }

  // Check if gate is open: Alarm LED is lit regardless of whether suspended or not
  if (gateOpen) {
    if (millis() - alarmLEDPulseStartTime > ALARM_LED_CYCLE_TIME) {
      alarmLEDPulseStartTime = millis();
    }
    else {
      if (millis() - alarmLEDPulseStartTime < ALARM_LED_ON_TIME) {
        digitalWrite(ALARM_LED_PIN, HIGH);
      }
      else {
        digitalWrite(ALARM_LED_PIN, LOW);
      }
    }
  }

  // There's a heartbeat pulse every few seconds when a LED is flashed briefly
  // unless the alarm is suspended in which case the LED is always lit
  if (!isSuspended()) {
    if (millis() - heartbeatLEDPulseStartTime > HEARTBEAT_LED_CYCLE_TIME) {
      heartbeatLEDPulseStartTime = millis();
    }
    else {
      if (millis() - heartbeatLEDPulseStartTime < HEARTBEAT_LED_ON_TIME) {
        digitalWrite(HEARTBEAT_LED_PIN, HIGH);
      }
      else {
        digitalWrite(HEARTBEAT_LED_PIN, LOW);
      }
    }
  } else {
    digitalWrite(HEARTBEAT_LED_PIN, HIGH);
  }

  // LCD backlight is normally switched off after it has been on for more than a few seconds
  // EXCEPT:
  //    * when gate is open
  //    * when alarm paused for a fixed amount of time (but not when suspended indefinately)
  //    * when user is entering a suspension time
  if (
    (millis() - lcdBacklightTimeoutStartTime >= LCD_BACKLIGHT_TIMEOUT)
    && !gateOpen
    && (!isSuspended() || isInfiniteSuspension())
    && !isUpdatingSuspendTime
  ) {
    switchLCDBacklightOff();
  }

}
