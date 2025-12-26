/************************************************************
  ESP32-S2 Reverse TFT Feather — Button-Controlled Blink
  ------------------------------------------------------
  LOOP has 3 clear phases:

    1) COLLECT:
       - Count how many "steps" each button is held.
       - One step = 50 ms of holding.
       - D0 held => incSteps++
       - D2 held => decSteps++
       - DO NOT change blinkDelayMs yet.

    2) COMPUTE:
       - Update blinkDelayMs by (incSteps - decSteps) * 10 ms
       - Clamp blinkDelayMs between 10 and 3000 ms
       - Track when a 10-blink batch will complete

    3) COMMUNICATE:
       - Run ONE blink (ON then OFF)
       - Every 10 completed blinks:
           compute average frequency (Hz) and print to TFT

  Definitions:
    - "One blink" = LED ON for blinkDelayMs, then LED OFF for blinkDelayMs

  Buttons on this board:
    - D0 (pin 0) is active-LOW (pressed reads LOW) -> uses INPUT_PULLUP
    - D2 (pin 2) is active-HIGH (pressed reads HIGH) -> uses INPUT_PULLDOWN
************************************************************/

// ==========================
// 1) LIBRARIES
// ==========================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ==========================
// 2) PINS & TFT OBJECT
// ==========================
const int PIN_LED    = LED_BUILTIN;
const int PIN_BTN_D0 = 0;   // pressed = LOW
const int PIN_BTN_D2 = 2;   // pressed = HIGH

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ==========================
// 3) CONSTANT SETTINGS
// ==========================
const int DEFAULT_DELAY_MS = 500;
const int MIN_DELAY_MS     = 10;
const int MAX_DELAY_MS     = 3000;
const int STEP_MS          = 10;

// While a button is held, we count "steps" every 50ms
const int HOLD_SAMPLE_MS   = 50;

// ==========================
// 4) STATE VARIABLES
// ==========================

// Blink timing (the thing students will change)
int blinkDelayMs = DEFAULT_DELAY_MS;

// Ten-blink batch tracking
int blinksInBatch = 0;          // counts completed blinks (0..10)
uint32_t batchStartMs = 0;      // time when the 10-blink batch started (right before blink #1)

// ==========================
// 5) TFT HELPERS (simple, no fancy graphics)
// ==========================
void initTFT_UI() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);

  tft.setCursor(10, 10);
  tft.print("Freq. (Hz):");
}

void printFrequencyHz(float hz) {
  // Clear the top region so old digits don't remain
  tft.fillRect(0, 0, tft.width(), 35, ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Freq. (Hz): ");
  tft.print(hz, 2);
}

// ==========================
// 6) DISPLAY PHASE: one blink
// ==========================
void doOneBlink(int delayMs) {
  digitalWrite(PIN_LED, HIGH);
  delay(delayMs);

  digitalWrite(PIN_LED, LOW);
  delay(delayMs);
}

// ==========================
// 7) SETUP
// ==========================
void setup() {
  Serial.begin(115200);

  // LED
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // Buttons with correct electrical defaults for this board
  pinMode(PIN_BTN_D0, INPUT_PULLUP);    // D0 pressed => LOW
  pinMode(PIN_BTN_D2, INPUT_PULLDOWN);  // D2 pressed => HIGH

  // TFT power/backlight pins (board-variant friendly)
  #ifdef TFT_I2C_POWER
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);
  #endif

  #ifdef TFT_BACKLIGHT
    pinMode(TFT_BACKLIGHT, OUTPUT);
    digitalWrite(TFT_BACKLIGHT, HIGH);
  #endif

  // TFT init
  pinMode(TFT_BACKLITE, OUTPUT);        // NOTE: it's BACKLITE (not BACKLIGHT)
  digitalWrite(TFT_BACKLITE, HIGH);     // backlight ON

  pinMode(TFT_I2C_POWER, OUTPUT);       // enable TFT / I2C power rail
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);
  tft.init(135, 240);
  tft.setRotation(3);
  initTFT_UI();

  // Start state
  blinkDelayMs  = DEFAULT_DELAY_MS;
  blinksInBatch = 0;
  batchStartMs  = 0;
}

// ==========================
// 8) LOOP: COLLECT -> COMPUTE -> DISPLAY
// ==========================
void loop() {

  // ======================================================
  // ======================= COLLECT =======================
  // ======================================================
  // We count "hold steps" while either button is pressed.
  // Each loop of this while() represents 50 ms of holding.

  int incSteps = 0;  // how many 50ms steps D0 was held
  int decSteps = 0;  // how many 50ms steps D2 was held

  bool d0Pressed = (digitalRead(PIN_BTN_D0) == LOW);   // active-LOW
  bool d2Pressed = (digitalRead(PIN_BTN_D2) == HIGH);  // active-HIGH

  while (d0Pressed || d2Pressed) {
    if (d0Pressed) incSteps++;
    if (d2Pressed) decSteps++;

    delay(HOLD_SAMPLE_MS);

    // re-check buttons
    d0Pressed = (digitalRead(PIN_BTN_D0) == LOW);
    d2Pressed = (digitalRead(PIN_BTN_D2) == HIGH);
  }

  // ======================================================
  // ======================= COMPUTE =======================
  // ======================================================

  // 1) Apply net delay change AFTER collecting steps
  int netSteps = incSteps - decSteps; // positive => slower, negative => faster
  blinkDelayMs += netSteps * STEP_MS;

  // Clamp into safe range
  blinkDelayMs = constrain(blinkDelayMs, MIN_DELAY_MS, MAX_DELAY_MS);

  // 2) Decide whether we are ABOUT to start a new 10-blink batch
  // We start timing right before blink #1 (so button-hold time doesn't affect frequency).
  if (blinksInBatch == 0) {
    batchStartMs = millis();
  }

  // 3) Decide whether THIS upcoming blink will finish the batch of 10
  bool willFinishBatch = (blinksInBatch == 9);

  // ======================================================
  // ===================== COMMUNICATE =====================
  // ======================================================

  // 1) Perform one full blink using the CURRENT delay
  doOneBlink(blinkDelayMs);

  // 2) Count it as completed
  blinksInBatch++;

  // 3) If we completed 10 blinks, compute Hz and print to TFT
  if (willFinishBatch) {
    uint32_t elapsedMs = millis() - batchStartMs;

    // Average frequency over 10 blinks:
    // Hz = (10 blinks) / (elapsed seconds)
    float elapsedSeconds = elapsedMs / 1000.0f;
    if (elapsedSeconds > 0.0f) {
      float hz = 10.0f / elapsedSeconds;
      printFrequencyHz(hz);
    }

    // Reset for the next batch
    blinksInBatch = 0;
  }
}
