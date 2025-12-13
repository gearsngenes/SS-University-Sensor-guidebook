#include <Wire.h>
#include <Adafruit_AS7341.h>

// AS7341 spectral sensor
Adafruit_AS7341 as7341;

// Refresh every 5 seconds
int t_delay = 5000;

// Display hooks (implemented in 02_TFT_Spectral_Support.ino)
void TFT_Setup(void);
void Publish_Data(float f1, float f2, float f3, float f4,
                  float f5, float f6, float f7, float f8,
                  float clear_counts, float nir_counts);

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
  uint16_t f2  = as7341.getChannel(AS7341_CHANNEL_445nm_F2);  // indigo/blue
  uint16_t f3  = as7341.getChannel(AS7341_CHANNEL_480nm_F3);  // blue
  uint16_t f4  = as7341.getChannel(AS7341_CHANNEL_515nm_F4);  // cyan
  uint16_t f5  = as7341.getChannel(AS7341_CHANNEL_555nm_F5);  // green
  uint16_t f6  = as7341.getChannel(AS7341_CHANNEL_590nm_F6);  // yellow
  uint16_t f7  = as7341.getChannel(AS7341_CHANNEL_630nm_F7);  // orange/red
  uint16_t f8  = as7341.getChannel(AS7341_CHANNEL_680nm_F8);  // deep red
  uint16_t clr = as7341.getChannel(AS7341_CHANNEL_CLEAR);     // broadband "clear"
  uint16_t nir = as7341.getChannel(AS7341_CHANNEL_NIR);       // near IR

  // Serial output for debugging / Serial Plotter
  Serial.print("F1_415nm_Violet:");  Serial.print(f1);
  Serial.print(" F2_445nm_Indigo:"); Serial.print(f2);
  Serial.print(" F3_480nm_Blue:");   Serial.print(f3);
  Serial.print(" F4_515nm_Cyan:");   Serial.print(f4);
  Serial.print(" F5_555nm_Green:");  Serial.print(f5);
  Serial.print(" F6_590nm_Yellow:"); Serial.print(f6);
  Serial.print(" F7_630nm_Orange:"); Serial.print(f7);
  Serial.print(" F8_680nm_Red:");    Serial.print(f8);
  Serial.print(" Clear_Broad:");     Serial.print(clr);
  Serial.print(" NIR_910nm:");       Serial.print(nir);
  Serial.println();

  // Histogram update: all 10 channels
  Publish_Data((float)f1, (float)f2, (float)f3, (float)f4,
               (float)f5, (float)f6, (float)f7, (float)f8,
               (float)clr, (float)nir);

  delay(t_delay);
}
