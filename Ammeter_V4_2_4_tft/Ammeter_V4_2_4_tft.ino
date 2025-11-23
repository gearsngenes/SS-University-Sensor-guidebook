#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 ina219;

// Sampling delay (ms)
int t_delay = 20;          // keep as in your original

// For Serial/logging (lifetime min/max). Display uses per-sweep min/max.
float min_current =  1e9;
float max_current = -1e9;

void TFT_Setup(void);
void Publish_Data(float current_mA, float min_current_mA, float max_current_mA, float bus_mV);

// Track lifetime min/max for Serial (display uses per-sweep)
void update_min_max(float current_mA) {
  if (current_mA < min_current) min_current = current_mA;
  if (current_mA > max_current) max_current = current_mA;
}

void setup() {
  Serial.begin(115200);
  // while (!Serial) { delay(1); }

  // INA219 init
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (1) { delay(10); }
  }
  // Optional: pick a calibration suitable for your range:
  // ina219.setCalibration_32V_2A(); // common default

  // TFT init
  TFT_Setup();
  Serial.println("Ammeter TFT setup complete.");
}

void loop() {
  // Read current in mA and bus voltage in mV
  float current_mA  = ina219.getCurrent_mA();
  float busvoltageV = ina219.getBusVoltage_V();
  float bus_mV      = busvoltageV * 1000.0f;

  update_min_max(current_mA);

  // Debug (optional)
  // Serial.print("I(mA): "); Serial.print(current_mA);
  // Serial.print("  MIN: "); Serial.print(min_current);
  // Serial.print("  MAX: "); Serial.print(max_current);
  // Serial.print("  Vbus(mV): "); Serial.println(bus_mV);

  // Publish to TFT (display internally tracks per-sweep min/max)
  Publish_Data(current_mA, min_current, max_current, bus_mV);

  delay(t_delay);
}
