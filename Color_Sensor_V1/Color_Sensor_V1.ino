#include <Wire.h>
#include <Adafruit_AS7341.h>

// AS7341 spectral sensor
Adafruit_AS7341 as7341;

// Sampling delay (ms) — tune with t_step in TFT file for sweep span
int t_delay = 50;

// Display hooks (implemented in 02_TFT_Spectral_Support.ino)
void TFT_Setup(void);
void Publish_Data(float r_counts, float g_counts, float b_counts, float clear_counts);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);  // wait for Serial Monitor / Plotter
  }

  if (!as7341.begin()) {
    Serial.println("Could not find AS7341, check wiring!");
    while (1) {
      delay(10);
    }
  }

  // Integration time, step and gain (from Adafruit example)
  as7341.setATIME(100);
  as7341.setASTEP(999);
  as7341.setGain(AS7341_GAIN_256X);

  TFT_Setup();
}

void loop() {
  // Read all spectral channels at once
  if (!as7341.readAllChannels()) {
    Serial.println("Error reading all channels!");
    delay(t_delay);
    return;
  }

  // Raw 16-bit channel counts (dimensionless ADC counts)
  uint16_t f1  = as7341.getChannel(AS7341_CHANNEL_415nm_F1);  // violet
  uint16_t f2  = as7341.getChannel(AS7341_CHANNEL_445nm_F2);  // blue
  uint16_t f3  = as7341.getChannel(AS7341_CHANNEL_480nm_F3);  // light blue
  uint16_t f4  = as7341.getChannel(AS7341_CHANNEL_515nm_F4);  // green
  uint16_t f5  = as7341.getChannel(AS7341_CHANNEL_555nm_F5);  // yellow-green
  uint16_t f6  = as7341.getChannel(AS7341_CHANNEL_590nm_F6);  // yellow
  uint16_t f7  = as7341.getChannel(AS7341_CHANNEL_630nm_F7);  // orange/red
  uint16_t f8  = as7341.getChannel(AS7341_CHANNEL_680nm_F8);  // deep red
  uint16_t clr = as7341.getChannel(AS7341_CHANNEL_CLEAR);     // broadband "clear"
  uint16_t nir = as7341.getChannel(AS7341_CHANNEL_NIR);       // near IR

  // Map spectral bands to approximate R, G, B "color" channels.
  // These are still raw counts (dimensionless, proportional to intensity).
  float blue_counts  = (float)f3;   // ~480 nm
  float green_counts = (float)f5;   // ~555 nm
  float red_counts   = (float)f7;   // ~630 nm
  float clear_counts = (float)clr;  // broadband "clear" photodiode

  // -------- Serial output for Arduino Serial Plotter --------
  // Each "name:value" pair becomes a separate series in the Serial Plotter.
  // Labels include wavelength and an approximate color.
  Serial.print("F1_415nm_Violet:");  Serial.print(f1);
  Serial.print(" F2_445nm_Blue:");   Serial.print(f2);
  Serial.print(" F3_480nm_LtBlue:"); Serial.print(f3);
  Serial.print(" F4_515nm_Green:");  Serial.print(f4);
  Serial.print(" F5_555nm_YelGn:");  Serial.print(f5);
  Serial.print(" F6_590nm_Yellow:"); Serial.print(f6);
  Serial.print(" F7_630nm_Orange:"); Serial.print(f7);
  Serial.print(" F8_680nm_Red:");    Serial.print(f8);
  Serial.print(" Clear_Broad:");     Serial.print(clr);
  Serial.print(" NIR_910nm:");       Serial.print(nir);

  // Convenience R/G/B series in addition to the raw F1..F8 channels
  Serial.print(" R_630nm:"); Serial.print(red_counts);
  Serial.print(" G_555nm:"); Serial.print(green_counts);
  Serial.print(" B_480nm:"); Serial.print(blue_counts);
  Serial.println();

  // -------- Send data to TFT graph (R, G, B + Clear) --------
  Publish_Data(red_counts, green_counts, blue_counts, clear_counts);

  delay(t_delay);
}
