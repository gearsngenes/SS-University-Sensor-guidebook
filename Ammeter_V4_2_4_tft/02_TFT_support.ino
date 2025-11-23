#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

// FeatherWing 2.4" ILI9341 pins
#define TFT_CS   9
#define TFT_DC   10
#define TFT_RST  -1  // tied to Feather RESET

// Color aliases
#define ST77XX_BLACK   ILI9341_BLACK
#define ST77XX_WHITE   ILI9341_WHITE
#define ST77XX_GREEN   ILI9341_GREEN
#define ST77XX_YELLOW  ILI9341_YELLOW

// Label bands
const int TOP_TEXT_HEIGHT    = 16;   // top band for max(mA)
const int BOTTOM_TEXT_HEIGHT = 16;   // bottom band for min(mA)

// ==== CURRENT VERTICAL SCALE ====
// Signed full-scale centered at 0 mA (change if needed)
const int CURR_FULL_SCALE_MA = 50;                // e.g., ±100 mA
const int CURR_MIN_MA        = -CURR_FULL_SCALE_MA;
const int CURR_MAX_MA        =  CURR_FULL_SCALE_MA;

// TFT
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Graph state
int t_prev = 0;
int t_curr = 0;
int t_step = 3;     // horizontal pixels per sample

int y_prev = 0;
int y_curr = 0;

// Per-sweep state (display-only)
static bool first_sample = true;
static bool sweep_new    = true;
static float sweep_min   = 0.0f;   // mA
static float sweep_max   = 0.0f;   // mA

// Timing for time-scale labels
static unsigned long sweep_start_ms  = 0;
static float         last_sweep_secs = NAN;

// Cached geometry
int16_t g_w = 0, g_h = 0;
int16_t g_graph_top = 0, g_graph_bottom = 0, g_graph_height = 0;
int16_t g_mid_y = 0;

// ---- helpers ----
void computeGeometry();
void makeGrid();
void drawMinMaxText(float min_mA, float max_mA);
void drawYAxisHalfLabels();
void drawTimeScaleLabels();

void TFT_Setup(void) {
  tft.begin();
  tft.setRotation(1); // landscape
  tft.fillScreen(ST77XX_BLACK);

  first_sample = true;
  sweep_new    = true;
  t_prev       = 0;
  t_curr       = 0;

  computeGeometry();
  makeGrid();
  drawMinMaxText(0, 0);
  drawYAxisHalfLabels();
  drawTimeScaleLabels();

  delay(150);
}

void computeGeometry() {
  g_w = tft.width();
  g_h = tft.height();

  g_graph_top    = TOP_TEXT_HEIGHT + 2;
  g_graph_bottom = g_h - BOTTOM_TEXT_HEIGHT - 3;
  g_graph_height = g_graph_bottom - g_graph_top + 1;

  g_mid_y = g_graph_top + g_graph_height / 2;   // 0 mA axis
}

void makeGrid() {
  computeGeometry();

  // Clear graph area
  tft.fillRect(0, g_graph_top, g_w, g_graph_height, ST77XX_BLACK);

  // Y-axis
  tft.drawLine(0, g_graph_top, 0, g_graph_bottom, ST77XX_WHITE);

  // 0 mA axis (midline)
  tft.drawLine(0, g_mid_y, g_w - 1, g_mid_y, ST77XX_WHITE);
}

void drawMinMaxText(float min_mA, float max_mA) {
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);

  // Top: max(mA)
  tft.fillRect(0, 0, g_w, TOP_TEXT_HEIGHT, ST77XX_BLACK);
  tft.setCursor(2, 2);
  tft.print("max(mA): ");
  tft.print((int)round(max_mA));

  // Bottom: min(mA)
  tft.fillRect(0, g_h - BOTTOM_TEXT_HEIGHT, g_w, BOTTOM_TEXT_HEIGHT, ST77XX_BLACK);
  tft.setCursor(2, g_h - BOTTOM_TEXT_HEIGHT + 2);
  tft.print("min(mA): ");
  tft.print((int)round(min_mA));
}

void drawYAxisHalfLabels() {
  const int HALF = CURR_FULL_SCALE_MA / 2;  // e.g., ±20 for ±40 mA

  auto map_to_y = [&](int mA) -> int16_t {
    long yy = g_graph_top + map(mA, CURR_MIN_MA, CURR_MAX_MA, g_graph_height - 1, 0);
    if (yy < g_graph_top) yy = g_graph_top;
    if (yy > g_graph_bottom) yy = g_graph_bottom;
    return (int16_t)yy;
  };

  int16_t y_pos_half = map_to_y(+HALF);
  int16_t y_neg_half = map_to_y(-HALF);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  const int16_t txt_h = 8;
  int16_t y_txt;

  // +half
  y_txt = y_pos_half - txt_h / 2;
  if (y_txt < g_graph_top) y_txt = g_graph_top;
  if (y_txt > g_graph_bottom - txt_h) y_txt = g_graph_bottom - txt_h;
  tft.setCursor(4, y_txt);
  tft.print(HALF);

  // -half
  y_txt = y_neg_half - txt_h / 2;
  if (y_txt < g_graph_top) y_txt = g_graph_top;
  if (y_txt > g_graph_bottom - txt_h) y_txt = g_graph_bottom - txt_h;
  tft.setCursor(4, y_txt);
  tft.print(-HALF);
}

void drawTimeScaleLabels() {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  const int margin_x = 2;
  const int margin_y = 2;
  int16_t y = g_graph_bottom - 8 - margin_y; // 8px font height

  // Left: "0"
  tft.setCursor(margin_x + 8, g_h/2+2);  // a little right of Y-axis
  tft.print("0");

  // Right: measured secs from previous sweep (if available)
  tft.fillRect(g_w - 60, y, 60, 10, ST77XX_BLACK); // clear old area
  if (!isnan(last_sweep_secs) && last_sweep_secs > 0.0f) {
    char buf[16];
    // print with one decimal place, e.g., "3.0s"
    snprintf(buf, sizeof(buf), "%.1fs", last_sweep_secs);
    int16_t text_w = strlen(buf) * 6; // 6px per char at size=1
    tft.setCursor(g_w - text_w - margin_x, g_h/2+2);
    tft.print(buf);
  }
}

// Display entry point from ammeter sketch
// (ignores passed min/max for rendering; uses per-sweep values)
void Publish_Data(float current_mA, float /*min_current_mA*/, float /*max_current_mA*/, float /*bus_mV*/) {
  // Clamp to signed range
  if (current_mA < CURR_MIN_MA) current_mA = CURR_MIN_MA;
  if (current_mA > CURR_MAX_MA) current_mA = CURR_MAX_MA;

  // Map current to screen y
  int this_y = g_graph_top + map((int)round(current_mA), CURR_MIN_MA, CURR_MAX_MA, g_graph_height - 1, 0);
  if (this_y < g_graph_top) this_y = g_graph_top;
  if (this_y > g_graph_bottom) this_y = g_graph_bottom;

  // Start of new sweep or first-ever sample
  if (first_sample || sweep_new) {
    makeGrid();
    drawYAxisHalfLabels();
    drawTimeScaleLabels();

    t_curr = 0;
    t_prev = 0;

    y_curr = this_y;
    y_prev = this_y;

    sweep_min = current_mA;
    sweep_max = current_mA;
    drawMinMaxText(sweep_min, sweep_max);

    sweep_start_ms = millis();

    tft.drawPixel(t_curr, y_curr, ST77XX_GREEN);

    first_sample = false;
    sweep_new    = false;
    return;
  }

  // Advance X
  t_prev = t_curr;
  t_curr += t_step;

  // Wrap -> finish sweep, measure duration, start fresh next call
  if (t_curr >= g_w) {
    unsigned long now_ms = millis();
    if (now_ms >= sweep_start_ms) {
      last_sweep_secs = (now_ms - sweep_start_ms) / 1000.0f;
    } else {
      last_sweep_secs = NAN;
    }
    sweep_new = true;
    return;
  }

  // Draw connected segment
  y_prev = y_curr;
  y_curr = this_y;
  tft.drawLine(t_prev, y_prev, t_curr, y_curr, ST77XX_GREEN);

  // Update per-sweep min/max and labels
  if (current_mA < sweep_min) sweep_min = current_mA;
  if (current_mA > sweep_max) sweep_max = current_mA;
  drawMinMaxText(sweep_min, sweep_max);
}
