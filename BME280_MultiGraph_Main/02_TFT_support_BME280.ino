#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Built-in TFT on the ESP32-S2 Reverse TFT Feather
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Plot state (EMF-style)
static int x_prev = 1;
static int x_curr = 1;
static int x_step = 1;

static int y_prev = 0;
static int y_curr = 0;

// For erase/redraw of min/max line (store last displayed)
static float last_min = 0.0f;
static float last_max = 0.0f;
static bool  have_last = false;

// Layout
static const int TEXT_Y = 0;          // min/max line at top
static const int TEXT_X = 5;
static const int TEXT_SIZE = 2;

// Reserve vertical pixels for top text (size 2 is ~16px tall)
static const int TOP_TEXT_H = 16;

// Axis Y: close to bottom (full screen width)
static int AXIS_Y = 0;               // set during TFT_Setup()

// -----------------------------
// Small helpers
// -----------------------------
static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int mapFloatToInt(float v, float inMin, float inMax, int outMin, int outMax) {
  if (inMax - inMin == 0.0f) return outMin;
  float t = (v - inMin) / (inMax - inMin);
  float out = outMin + t * (float)(outMax - outMin);
  return (int)out;
}

// -----------------------------
// EMF-style axes/grid
// -----------------------------
void TFT_Setup(void) {
  // Backlight ON
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // Power for TFT/I2C rail ON
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // IMPORTANT: Reverse TFT uses init(135,240) then rotate
  tft.init(135, 240);
  tft.setRotation(3);

  AXIS_Y = tft.height() - 1; // x-axis at the very bottom

  // Clear and draw axes
  tft.fillScreen(ST77XX_BLACK);
  tft.drawLine(0, 0, 0, tft.height() - 1, ST77XX_WHITE);                 // Y axis
  tft.drawLine(0, AXIS_Y, tft.width() - 1, AXIS_Y, ST77XX_WHITE);         // X axis

  // Reset plot state
  x_prev = 1;
  x_curr = 1;
  y_prev = TOP_TEXT_H;
  have_last = false;

  delay(50);
}

// Clear screen and redraw axes only (NO BOX)
void makeGrid() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawLine(0, 0, 0, tft.height() - 1, ST77XX_WHITE);
  tft.drawLine(0, AXIS_Y, tft.width() - 1, AXIS_Y, ST77XX_WHITE);

  // reset plot cursor
  x_prev = 1;
  x_curr = 1;
  y_prev = TOP_TEXT_H;
}

// Called when switching metrics: clears plot and clears last text so erase works cleanly
void TFT_NewMetricRun() {
  makeGrid();
  have_last = false;
}

// Draw "Min: ...unit   Max: ...unit" on ONE line (top), EMF-style erase/redraw
void TFT_DrawMinMaxLine(float minV, float maxV, const char* unit) {
  tft.setTextSize(TEXT_SIZE);

  // Erase previous by overprinting in BLACK (EMF-style)
  if (have_last) {
    tft.setTextColor(ST77XX_BLACK);
    tft.setCursor(TEXT_X, TEXT_Y);
    tft.print("Min: ");
    tft.print(last_min, 1);
    tft.print(unit);
    tft.print("  Max: ");
    tft.print(last_max, 1);
    tft.print(unit);
  }

  // Store new values
  last_min = minV;
  last_max = maxV;
  have_last = true;

  // Draw new in YELLOW
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(TEXT_X, TEXT_Y);
  tft.print("Min: ");
  tft.print(minV, 1);
  tft.print(unit);
  tft.print("  Max: ");
  tft.print(maxV, 1);
  tft.print(unit);
}

// Plot one datapoint across the full width.
// RETURNS: true if the cursor wrapped and we cleared/redrew the grid.
bool TFT_PlotValue(float val, float vmin, float vmax) {
  // Clamp value to visual range
  val = clampf(val, vmin, vmax);

  // Map to vertical pixels:
  // top limit is TOP_TEXT_H (avoid drawing over min/max line),
  // bottom limit is AXIS_Y (x-axis)
  y_curr = mapFloatToInt(val, vmin, vmax, AXIS_Y, TOP_TEXT_H);

  // Advance x
  x_curr += x_step;

  // Wrap check: use full width
  if (x_curr >= tft.width()) {
    makeGrid();

    // After clear, start at x=1 and plot first point
    x_curr = 1;
    x_prev = 1;
    tft.drawPixel(x_curr, y_curr, ST77XX_GREEN);
    y_prev = y_curr;

    return true; // <-- IMPORTANT: tell main we wrapped
  }

  // Draw line segment
  tft.drawLine(x_prev, y_prev, x_curr, y_curr, ST77XX_GREEN);

  // Update prev
  x_prev = x_curr;
  y_prev = y_curr;

  return false;
}
