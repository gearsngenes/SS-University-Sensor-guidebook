#include <SPI.h>
#include <Wire.h>
/**
    Code for ESP32-S2-TFT-Reverse
*/
#include <Arduino.h>
// If you decide to really use the fuel gauge later, you can re-add
// the MAX1704X object and initialization.
// #include "Adafruit_MAX1704X.h"

#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans12pt7b.h>

/*
   For boards like the Adafruit Feather ESP32-S2 Reverse TFT, the TFT_* pins
   are provided by the board support package:

     TFT_CS, TFT_DC, TFT_RST, TFT_BACKLITE, TFT_I2C_POWER

   We only define fallback values here for *generic* boards where those
   macros are not already defined.
*/
#ifndef TFT_CS
  #define TFT_CS   9
#endif
#ifndef TFT_DC
  #define TFT_DC   10
#endif
#ifndef TFT_RST
  #define TFT_RST  -1    // tied to board reset
#endif

Adafruit_ST7789 display = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(240, 135);

// variable for when omega updates (set from IR_Support.ino)
boolean updateOmg = false;


/*
   Code for turntable components
*/
#include <Adafruit_MotorShield.h>
// Create the motor shield object with the default I2C address
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
// Select which 'port' M1, M2, M3 or M4. In this case, M1
Adafruit_DCMotor *myMotor = AFMS.getMotor(1);
int m_speed = 100;
uint8_t idx = 0;
String dirs[] = {"Counter Clock", "Clockwise", "STOP"};



void setup() {
  Serial.begin(115200);

  // Turn on backlight
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // Turn on the TFT / on-board I2C power rail
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // Initialize I2C (Motor Shield + any on-board I2C sensors)
  Wire.begin();

  /**
     Motor Shield setup
  */
  Serial.println("Adafruit Motorshield v2 - DC Motor test!");
  if (!AFMS.begin()) {         // create with the default frequency 1.6KHz
    Serial.println("Could not find Motor Shield. Check wiring.");

    // Give visual feedback on the TFT if the shield is missing
    display.init(135, 240);
    display.setRotation(3);
    canvas.setFont(&FreeSans12pt7b);
    canvas.setTextColor(ST77XX_RED);
    canvas.fillScreen(ST77XX_BLACK);
    canvas.setCursor(10, 60);
    canvas.println("No MotorShield!");
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);

    while (1); // halt here so you notice the problem
  }
  Serial.println("Motor Shield found.");

  // Set the speed to start, from 0 (off) to 255 (max speed)
  myMotor->setSpeed(m_speed);
  // Set initial direction forward
  myMotor->run(FORWARD);

  /**
     IR Sensor Setup
  */
  IRSetup();

  /**
     TFT setup
  *
     We initialize the TFT *after* powering its rail, but regardless of the
     Motor Shield’s status (see above for the failure path).
  */
  display.init(135, 240);           // Init ST7789 240x135
  display.setRotation(3);
  canvas.setFont(&FreeSans12pt7b);
  canvas.setTextColor(ST77XX_WHITE);

  // Configure the three front buttons:
  //   0: Button A (active LOW, pullup)
  //   1: Button B (active HIGH, pulldown)
  //   2: Button C (active HIGH, pulldown)
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLDOWN);
  pinMode(2, INPUT_PULLDOWN);

  // Splash screen
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setCursor(20, 20);
  canvas.print("GnG Cirkinemeter\nTurn-Table");
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
  delay(1500);

  // Clear for runtime display
  canvas.fillScreen(ST77XX_BLACK);
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);

  // Make sure backlight stays on
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
}

/**
   Convenience motor-control functions:
     - CCW: Counter-clockwise (FORWARD)
     - CW: Clockwise (BACKWARD)
     - force_stop: RELEASE (coast/stop)
*/
void CCW() {
  myMotor->run(FORWARD);
}

void CW() {
  myMotor->run(BACKWARD);
}

void force_stop() {
  myMotor->run(RELEASE);
}

void loop() {
  canvas.setCursor(20, 20);

  /**
     If any of buttons A, B, or C are pressed, then
     clear the display and modify the values as
     indicated in their specific sections.
  */

  // Button A (pin 0, active LOW) — increase speed
  if (!digitalRead(0)) { // if A is pressed
    /**
       If A is pressed, increase speed
       no matter what. Cap out at 255.
    */
    m_speed += 1;
    if (m_speed > 255) {
      m_speed = 255;
    }
    myMotor->setSpeed(m_speed);
    canvas.fillScreen(ST77XX_BLACK);
    delay(5);
  }
  // Button B (pin 1, active HIGH) — cycle direction
  else if (digitalRead(1)) {
    /**
       If B is pressed, then cycle through
       -> CCW -> CW -> Stop ->
    */
    idx += 1;
    idx %= 3;
    if (idx == 0) myMotor->run(FORWARD);
    else if (idx == 1) myMotor->run(BACKWARD);
    else myMotor->run(RELEASE);
    canvas.fillScreen(ST77XX_BLACK);
    delay(300);
  }
  // Button C (pin 2, active HIGH) — decrease speed
  else if (digitalRead(2)) {
    /**
       If Button C pressed, then decrease
       the speed of the motor, floor at 0.
    */
    m_speed -= 1;
    if (m_speed < 0) {
      m_speed = 0;
    }
    myMotor->setSpeed(m_speed);
    canvas.fillScreen(ST77XX_BLACK);
    delay(5);
  }

  /**
     After controlling speed and direction:
       - check for IR sensors
       - if enough revolutions occur
         * get RPS/Omega
  */
  IRProcessCrossing();
  printOmegaPeriodically();
  if (updateOmg) {
    /**
       If new omega data needs to be
       displayed, then refresh the
       TFT canvas.
    */
    canvas.fillScreen(ST77XX_BLACK);
    updateOmg = false;
  }

  /**
     Display the speed and direction
     on the TFT.
  */
  canvas.setCursor(10, 40);
  if (idx == 2) {
    canvas.setTextColor(ST77XX_RED);
  }
  canvas.println(dirs[idx]);

  canvas.setCursor(10, 70);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.println("PWM: " + (String)m_speed);

  canvas.setCursor(10, 110);
  canvas.setTextColor(ST77XX_YELLOW);
  canvas.println(getStringOmgRPS());

  // Reset back to default text color
  canvas.setTextColor(ST77XX_WHITE);

  delay(10);
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);

  // Make absolutely sure backlight stays on
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
}
