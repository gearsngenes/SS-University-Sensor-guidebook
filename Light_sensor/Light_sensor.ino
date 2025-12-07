#include <Wire.h>
#include <Adafruit_LTR329_LTR303.h>

// ===== LTR-303 (I2C) =====
Adafruit_LTR303 ltr303;

// Sampling delay (ms) — pairs with t_step in 02_TFT_Support.ino for sweep span
int t_delay = 5;

// Display hooks (implemented in 02_TFT_Support.ino)
//   void TFT_Setup(void);
//   void Publish_Data(int value, int unused1, int unused2);
void TFT_Setup(void);
void Publish_Data(int value, int unused1, int unused2);

// Simple min/max trackers in "visible counts" (raw)
uint32_t vis_min = 0xFFFFFFFF;
uint32_t vis_max = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  Serial.println("LTR-303 Light Sensor + TFT graph");
  Serial.println("Initializing I2C and sensor...");

  Wire.begin();

  if (!ltr303.begin()) {
    Serial.println("ERROR: LTR-303 not found at 0x29. Check wiring/STEMMA QT cable.");
    while (1) {
      delay(10);
    }
  }

  // Configure the ALS (Ambient Light Sensor)
  // Gain options: LTR3XX_GAIN_1, _2, _4, _8, _48, _96  (1x..96x)
  // Integration time: LTR3XX_INTEGTIME_50.._400 ms
  // Measurement rate: LTR3XX_MEASRATE_50.._2000 ms
  ltr303.setGain(LTR3XX_GAIN_1);                     // 1x gain, wide range
  ltr303.setIntegrationTime(LTR3XX_INTEGTIME_100);   // 100 ms integration
  ltr303.setMeasurementRate(LTR3XX_MEASRATE_200);    // new measurement every 200 ms
  ltr303.enable(true);                               // turn on ALS engine

  // Init TFT graph
  TFT_Setup();
  Serial.println("Setup complete.");
}

void loop() {
  uint16_t ch0, ch1;

  // Wait until new data is ready
  if (!ltr303.newDataAvailable()) {
    delay(t_delay);
    return;
  }

  // Read both channels: ch0 = visible+IR, ch1 = IR (raw 16-bit counts)
  if (!ltr303.readBothChannels(ch0, ch1)) {
    // Data overrun or error
    Serial.println("LTR303 read error / invalid data");
    delay(t_delay);
    return;
  }

  // Derive approximate human-visible component as raw counts
  uint32_t visible_counts = 0;
  if (ch0 > ch1) {
    visible_counts = (uint32_t)(ch0 - ch1);
  } else {
    visible_counts = 0; // clamp if IR >= total
  }

  // ---- Serial output (good for Serial Plotter) ----
  // We keep it to one primary series "VisibleCount" so the Serial Plotter
  // clearly shows a single line.
  Serial.print("VisibleCount:");
  Serial.println(visible_counts);

  // ---- Track min/max (in visible counts) ----
  if (visible_counts < vis_min) vis_min = visible_counts;
  if (visible_counts > vis_max) vis_max = visible_counts;

  // ---- Push a single value into the TFT graph ----
  // The TFT support file treats this value as a non-negative "light level".
  int value_for_tft = (int)visible_counts;
  Publish_Data(value_for_tft, 0, 0);

  delay(t_delay);
}
