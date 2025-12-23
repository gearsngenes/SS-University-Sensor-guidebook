#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// Buttons: A->D0, B->D1, C->D2
#define BUTTON_A 0  // active LOW (pull-up)
#define BUTTON_B 1  // active HIGH (pull-down)
#define BUTTON_C 2  // active HIGH (pull-down)

static const uint32_t DEBOUNCE_MS   = 50;
static const uint32_t LOOP_DELAY_MS = 5;

Adafruit_BME280 bme;

// Metric selection
enum Metric : uint8_t { METRIC_TEMP, METRIC_HUMID, METRIC_PRESS };
static Metric currentMetric = METRIC_TEMP;

// Fixed axis ranges (as agreed)
static const float TEMP_MIN  = 0.0f;
static const float TEMP_MAX  = 50.0f;
static const char  TEMP_UNIT[] = "C";

static const float HUM_MIN   = 0.0f;
static const float HUM_MAX   = 100.0f;
static const char  HUM_UNIT[] = "%";

static const float PRES_MIN  = 900.0f;    // hPa
static const float PRES_MAX  = 1100.0f;   // hPa
static const char  PRES_UNIT[] = "hPa";

// Run min/max (reset on screen change OR on wrap, per your request)
static float runMin = 0.0f;
static float runMax = 0.0f;

// Button press->release debouncer
struct ButtonPR {
  uint8_t pin;
  bool activeHigh;
  bool stablePressed = false;
  bool sawPress = false;
  uint32_t lastChangeMs = 0;
};

static ButtonPR btnA{BUTTON_A, false};
static ButtonPR btnB{BUTTON_B, true};
static ButtonPR btnC{BUTTON_C, true};

static bool readLogicalPressed(const ButtonPR& b) {
  bool raw = digitalRead(b.pin);
  return b.activeHigh ? raw : !raw;
}

static bool updatePressRelease(ButtonPR& b, uint32_t nowMs) {
  bool pressedNow = readLogicalPressed(b);

  if (pressedNow != b.stablePressed) {
    if ((nowMs - b.lastChangeMs) >= DEBOUNCE_MS) {
      b.stablePressed = pressedNow;
      b.lastChangeMs = nowMs;

      if (b.stablePressed) {
        b.sawPress = true;
      } else {
        if (b.sawPress) {
          b.sawPress = false;
          return true;
        }
      }
    }
  }
  return false;
}

// Metric config
static void getMetricConfig(Metric m, float &vmin, float &vmax, const char* &unit) {
  switch (m) {
    case METRIC_TEMP:  vmin = TEMP_MIN;  vmax = TEMP_MAX;  unit = TEMP_UNIT; break;
    case METRIC_HUMID: vmin = HUM_MIN;   vmax = HUM_MAX;   unit = HUM_UNIT;  break;
    default:           vmin = PRES_MIN;  vmax = PRES_MAX;  unit = PRES_UNIT; break;
  }
}

// 1) collect
static float collectSensorValue(Metric m) {
  switch (m) {
    case METRIC_TEMP:
      return bme.readTemperature(); // °C
    case METRIC_HUMID:
      return bme.readHumidity();    // %RH
    default: {
      // Adafruit BME280 returns Pa; convert to hPa
      float pa = bme.readPressure();
      return pa / 100.0f;
    }
  }
}

// Reset min/max baseline for a new run window
static void resetMinMaxForNewWindow(float firstValue) {
  runMin = firstValue;
  runMax = firstValue;
}

// 2) compute
static void computeMinMax(float v) {
  if (v < runMin) runMin = v;
  if (v > runMax) runMax = v;
}

// Reset on screen change
static void startNewRun(Metric m) {
  currentMetric = m;
  runMin =  1e9f;
  runMax = -1e9f;

  TFT_NewMetricRun();
}

// 3) communicate
static void publish(float v, float vmin, float vmax, const char* unit) {
  // Plot; if it wrapped, reset min/max for the new screen-width window
  bool wrapped = TFT_PlotValue(v, vmin, vmax);
  if (wrapped) {
    // New window starts at the first plotted value after wrap
    resetMinMaxForNewWindow(v);
  }

  // Update min/max line every loop (EMF-style)
  TFT_DrawMinMaxLine(runMin, runMax, unit);
}

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLDOWN);
  pinMode(BUTTON_C, INPUT_PULLDOWN);

  Wire.begin();

  // TFT
  TFT_Setup();
  Serial.println("TFT setup complete.");

  // BME280
  bool ok = bme.begin();
  if (!ok) ok = bme.begin(0x76);
  if (!ok) ok = bme.begin(0x77);

  if (!ok) {
    Serial.println("BME280 not found.");
    while (true) delay(100);
  }
  Serial.println("BME280 setup complete.");

  // Default screen = Temperature
  startNewRun(METRIC_TEMP);
}

void loop() {
  // Buttons (press->release switches screens)
  uint32_t nowMs = millis();
  if (updatePressRelease(btnA, nowMs)) startNewRun(METRIC_TEMP);
  if (updatePressRelease(btnB, nowMs)) startNewRun(METRIC_HUMID);
  if (updatePressRelease(btnC, nowMs)) startNewRun(METRIC_PRESS);

  // Collect -> Compute -> Communicate
  float vmin, vmax; const char* unit;
  getMetricConfig(currentMetric, vmin, vmax, unit);

  float v = collectSensorValue(currentMetric); // 1) collect

  // If this is a new metric run (runMin is still huge), seed min/max from first value.
  // This keeps the display sane immediately.
  if (runMin > 1e8f && runMax < -1e8f) {
    resetMinMaxForNewWindow(v);
  } else {
    computeMinMax(v);                          // 2) compute
  }

  publish(v, vmin, vmax, unit);                // 3) communicate

  delay(LOOP_DELAY_MS);
}
