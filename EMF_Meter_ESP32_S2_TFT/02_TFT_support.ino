#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

// Use dedicated hardware SPI pins
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);


int t_prev = 0;
int t_curr = 0;
int t_step = 3;


int y_prev = 0;
int y_curr = 0;
int min_y = 0;
int max_y = 0;


void TFT_Setup(void) {
  // turn on backlite
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // turn on the TFT / I2C power supply
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // initialize TFT
  tft.init(135, 240); // Init ST7789 240x135
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.drawLine(0, 0, 0, tft.height() - 1, ST77XX_WHITE);
  tft.drawLine(0, tft.height() / 2, tft.width() - 1, tft.height() / 2, ST77XX_WHITE);

  delay(1000);
}



void makeGrid() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawLine(0, 0, 0, tft.height() - 1, ST77XX_WHITE);
  tft.drawLine(0, tft.height() / 2, tft.width() - 1, tft.height() / 2, ST77XX_WHITE);
}
void plotData_chan1(int emf, int minV, int maxV) {
  // create current data point
  y_curr = map(emf, -1 * max_volt_mag, max_volt_mag, 135, 0);
  t_curr = t_curr + t_step;
  //reset the x-point if it goes past the screen
  if (t_curr > tft.width()) {
    makeGrid();
    tft.drawPixel(0, y_curr, ST77XX_GREEN);
    t_curr = 0;
  }
  else { // else draw a straight line
    tft.drawLine(t_prev, y_prev, t_curr, y_curr, ST77XX_GREEN);
  }
  // set previous point equal to the current point
  t_prev = t_curr;
  y_prev = y_curr;
  tft.setTextSize(2);
  /*erase the old min-max readings*/
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(5, 0);
  tft.println("max: " + (String)max_y + "mV");
  tft.setCursor(5, 120);
  tft.println("min: " + (String)min_y + "mV");
  /*update the min_y and max_y values*/
  min_y = minV;
  max_y = maxV;
  /*write the new min-max readings*/
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(5, 0);
  tft.println("max: " + (String)max_y + "mV");
  tft.setCursor(5, 120);
  tft.println("min: " + (String)min_y + "mV");
}
