#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LIS3DH.h>
#include <math.h>

// ===== LIS3DH (I2C) =====
Adafruit_LIS3DH lis;

// Sampling delay (ms) — tune with t_step in TFT file for sweep span
int t_delay = 5;

// Display hooks (implemented in 02_TFT_Accel_Support.ino)
void TFT_Setup(void);
void Publish_Data(float ax_ms2, float ay_ms2, float az_ms2, float atotal_ms2);

void setup() {
  Serial.begin(115200);
  // Do not block on while(!Serial)

  // Bring up TFT first so you see black screen even if sensor init fails
  TFT_Setup();

  // Power the I2C rail on ESP32-S2 Feather if the pin is defined
  #ifdef PIN_I2C_POWER
    pinMode(PIN_I2C_POWER, OUTPUT);
    digitalWrite(PIN_I2C_POWER, HIGH);
    delay(10); // let rail stabilize
  #endif

  // Initialize LIS3DH at 0x18, fallback to 0x19
  bool ok = lis.begin(0x18);
  if (!ok) ok = lis.begin(0x19);
  if (!ok) {
    // If you want, print an on-screen error here; for now, just halt
    while (1) { delay(10); }
  }

  // Range & data rate (±4g gives nice headroom; ODR 100 Hz is plenty)
  lis.setRange(LIS3DH_RANGE_4_G);
  lis.setDataRate(LIS3DH_DATARATE_100_HZ);
}

void loop() {
  sensors_event_t event;
  lis.getEvent(&event);            // acceleration in m/s^2
  float ax = event.acceleration.x;
  float ay = event.acceleration.y;
  float az = event.acceleration.z;
  float at = sqrtf(ax*ax + ay*ay + az*az);

  Publish_Data(ax, ay, az, at);
  delay(t_delay);
}
