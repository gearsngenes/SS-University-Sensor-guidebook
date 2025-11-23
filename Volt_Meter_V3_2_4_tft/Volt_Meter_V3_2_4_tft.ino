#include <Adafruit_ADS1X15.h>
/**
 * Gain and bit-multiplier table for the ADS1x15
 * 
 * Gain | Bit-mul 1115 | Bit-mul 1015 | Range (V)
 * -------------------------------------------
 * 2/3  | 0.1875 mV    | 3 mV         | +-6.144
 * 1    | 0.125 mV     | 2 mV         | +-4.096
 * 2    | 0.0625 mV    | 1 mV         | +-2.048
 * 4    | 0.03125 mV   | 0.5 mV      | +-1.024
 * 8    | 0.015625 mV  | 0.25 mV     | +-0.512
 * 16   | 0.0078125 mV | 0.125 mV    | +-0.256 
 * 
 * If 9.0V battery (full) connected to the motor, at max speed, approx +/- 220-250mV is induced 
 * in the coil.
 * 
 * Datasheet of ADS1115: https://www.ti.com/lit/ds/symlink/ads1115.pdf
 * 
 * Based on Adafruit ADS1X15 Library examples:
 * https://github.com/adafruit/Adafruit_ADS1X15/tree/master/examples
 */

Adafruit_ADS1115 ads1115;
//Adafruit_ADS1015 ads1015;

// External voltage scaling factor (for voltage divider, etc.)
float external_voltage_multiplier = 11.0;

// Delay between samples (ms)
int t_delay = 10;

// For keeping track of min and max voltages
float min_voltage = 9999.0;
float max_voltage = -9999.0;

// Forward declarations from TFT support file
void TFT_Setup(void);
void Publish_Data(int emf, int minV, int maxV);

void update_min_max(float voltage) {
  if (voltage < min_voltage) {
    min_voltage = voltage;
  }
  if (voltage > max_voltage) {
    max_voltage = voltage;
  }
}

void setup() {
  Serial.begin(115200);
  /*set up the TFT*/
  pinMode(0, INPUT_PULLUP);
  TFT_Setup();
  Serial.println("TFT Set up Complete.");
  /*set up the ADS*/
  ads1115.setGain(GAIN_TWOTHIRDS);  // +/- 6.144V range, 0.1875 mV per bit
  ads1115.begin();
  Serial.println("ADS1115 Set up Complete.");
}

void loop() {
  // Read differential voltage between A0 and A1
  int16_t diff = ads1115.readADC_Differential_0_1();

  // Calculate voltage in millivolts
  // For GAIN_TWOTHIRDS, 1 bit = 0.1875 mV on ADS1115
  float multiplier = 0.1875F; // mV/bit for ADS1115 at gain 2/3
  float voltage = diff * multiplier * external_voltage_multiplier;

  // Update min and max voltages
  update_min_max(voltage);

  // Publish data to TFT
  Publish_Data((int)voltage, (int)min_voltage, (int)max_voltage);

  // Wait before next sample
  delay(t_delay);
}
