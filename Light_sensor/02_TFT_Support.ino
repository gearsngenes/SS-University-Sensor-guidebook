#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// FeatherWing 2.4" ILI9341 pins (default CS=9, DC=10 on Feather headers)
#define TFT_CS   9
#define TFT_DC   10
#define TFT_RST  -1    // tied to board reset

// Colors
#define COL_BG        ILI9341_BLACK
#define COL_AXIS      ILI9341_WHITE
#define COL_TRACE     ILI9341_GREEN
#define COL_GRID      ILI9341_DARKGREY
#define COL_TEXT      ILI9341_WHITE

// ===== Vertical scale (lux or arbitrary non-negative light units) =====
// Treat the incoming value as "lux" for plotting.  You can adjust this
// full-scale constant to match the typical range of your sensor.
const float LUX_MIN = 0.0f;
const float LUX_MAX = 2000.0f;   // full-scale top of graph (tune as needed)

// Horizontal stepping (pixels per sample)
int t_step = 2;   // smaller => longer sweep (also adjust t_delay in main)

// ------- Top legend layout (single-size, three columns) -------
const int TS_TOP = 2;          // text size for top row
const int COL_W  = 106;
const int COL0   = 0;
const int COL1   = 106;
const int COL2   = 212;

const int LAB_X_CUR  = COL0 + 4;
const int LAB_X_MIN  = COL1 + 4;
const int LAB_X_MAX  = COL2 + 4;

// UI bands
const int TOP_TEXT_HEIGHT    = 16;
const int BOTTOM_TEXT_HEIGHT = 16;   // bottom band (units + time labels)

// TFT
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// Graph geometry
int16_t g_w = 0, g_h = 0;
int16_t g_graph_top = 0, g_graph_bottom = 0, g_graph_height = 0;

// Sweep/time state
int t_prev = 0, t_curr = 0;
bool first_sample = true, sweep_new = true;
static unsigned long sweep_start_ms  = 0;
static float         last_sweep_secs = NAN;  // duration of previous sweep

// Previous Y for line drawing
int16_t y_prev = 0;

// Global running min/max (over entire session, not just one sweep)
float lux_min_seen = NAN;
float lux_max_seen = NAN;

// Legend caches (to avoid unnecessary redraws)
char prev_segCur[20] = "";
char prev_segMin[20] = "";
char prev_segMax[20] = "";

// ---------- helpers ----------
static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void computeGeometry() {
  g_w = tft.width();
  g_h = tft.height();
  g_graph_top    = TOP_TEXT_HEIGHT + 2;
  g_graph_bottom = g_h - BOTTOM_TEXT_HEIGHT - 3;
  g_graph_height = g_graph_bottom - g_graph_top + 1;
}

// Map [LUX_MIN..LUX_MAX] to [g_graph_bottom..g_graph_top]
int16_t mapToY(float lux_val) {
  float v = clampf(lux_val, LUX_MIN, LUX_MAX);
  float frac = (v - LUX_MIN) / (LUX_MAX - LUX_MIN);  // 0..1
  float y    = (float)g_graph_top + (1.0f - frac) * (float)(g_graph_height - 1);
  int16_t yi = (int16_t)(y + 0.5f);
  if (yi < g_graph_top) yi = g_graph_top;
  if (yi > g_graph_bottom) yi = g_graph_bottom;
  return yi;
}

void clearGraphArea() {
  tft.fillRect(0, g_graph_top, g_w, g_graph_height, COL_BG);
}

void drawAxes() {
  // Left Y-axis
  tft.drawLine(0, g_graph_top, 0, g_graph_bottom, COL_AXIS);
  // 0-lux baseline at the bottom of the graph area (time axis)
  tft.drawLine(0, g_graph_bottom, g_w - 1, g_graph_bottom, COL_AXIS);
}

void drawYAxisQuarterLabels() {
  // Draw 0, 25%, 50%, 75%, and 100% of full-scale along the left side
  float vals[5] = {
    LUX_MIN,
    LUX_MIN + 0.25f * (LUX_MAX - LUX_MIN),
    LUX_MIN + 0.50f * (LUX_MAX - LUX_MIN),
    LUX_MIN + 0.75f * (LUX_MAX - LUX_MIN),
    LUX_MAX
  };

  for (int i = 0; i < 5; i++) {
    float v = vals[i];
    int16_t y = mapToY(v);
    int16_t y_txt = y - 4;               // ~center for size=1 text (≈8px high)
    if (y_txt < g_graph_top) y_txt = g_graph_top;
    if (y_txt > g_graph_bottom - 8) y_txt = g_graph_bottom - 8;

    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT, COL_BG);  // classic font supports bg erase
    char buf[12];
    unsigned long u = (unsigned long)(v + 0.5f);
    snprintf(buf, sizeof(buf), "%lu", u);
    tft.setCursor(4, y_txt);
    tft.print(buf);

    // Draw a light horizontal grid line for non-zero quarters
    if (i > 0 && i < 4) {
      for (int x = 1; x < g_w - 1; x += 4) {
        tft.drawPixel(x, y, COL_GRID);
      }
    }
  }
}

void drawTimeScaleLabels() {
  // "0" near bottom-left, and last sweep duration (e.g. "~3.0s") bottom-right.
  const int margin_x = 2;
  int16_t y = g_graph_bottom + 2;  // just below the 0-lux baseline, inside bottom band

  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);

  // Left "0"
  tft.setCursor(margin_x + 8, y);
  tft.print("0");

  // Right measured seconds (if available)
  tft.fillRect(g_w - 70, y, 70, 10, COL_BG);
  if (!isnan(last_sweep_secs) && last_sweep_secs > 0.0f) {
    char buf[16];
    snprintf(buf, sizeof(buf), "~%.1fs", last_sweep_secs);
    int16_t text_w = (int16_t)(strlen(buf) * 6); // 6 px/char at size=1
    tft.setCursor(g_w - text_w - margin_x, y);
    tft.print(buf);
  }
}

void drawBottomNote() {
  // centered "Light (lux)" — stays in the bottom band
  tft.fillRect(0, g_h - BOTTOM_TEXT_HEIGHT, g_w, BOTTOM_TEXT_HEIGHT, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  const char* note = "Light (lux or counts)";
  int16_t wpx = (int16_t)(strlen(note) * 6); // 6 px/char at size=1
  int16_t x = (g_w - wpx) / 2;
  int16_t y = g_h - BOTTOM_TEXT_HEIGHT + 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(note);
}

// Format value as integer lux (no decimals)
static void fmtLux(char* out, size_t n, float v) {
  if (v < 0.0f) v = 0.0f;
  unsigned long u = (unsigned long)(v + 0.5f);
  snprintf(out, n, "%lu", u);
}

// Build legend strings once per call, then only redraw changed segments
static void updateTopLegendRow(float lux_now) {
  // Update global min/max
  if (isnan(lux_min_seen) || lux_now < lux_min_seen) lux_min_seen = lux_now;
  if (isnan(lux_max_seen) || lux_now > lux_max_seen) lux_max_seen = lux_now;

  char vCur[12], vMin[12], vMax[12];
  fmtLux(vCur, sizeof(vCur), lux_now);
  fmtLux(vMin, sizeof(vMin), isnan(lux_min_seen) ? lux_now : lux_min_seen);
  fmtLux(vMax, sizeof(vMax), isnan(lux_max_seen) ? lux_now : lux_max_seen);

  // Full segment strings
  char segCur[20], segMin[20], segMax[20];
  snprintf(segCur, sizeof(segCur), "Lux:%s", vCur);
  snprintf(segMin, sizeof(segMin), "Min:%s", vMin);
  snprintf(segMax, sizeof(segMax), "Max:%s", vMax);

  tft.setFont(NULL);                // classic font (bg erase supported)
  tft.setTextSize(TS_TOP);

  if (strcmp(prev_segCur, segCur) != 0) {
    tft.fillRect(COL0, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_TRACE, COL_BG);
    tft.setCursor(LAB_X_CUR, 0);
    tft.print(segCur);
    strcpy(prev_segCur, segCur);
  }
  if (strcmp(prev_segMin, segMin) != 0) {
    tft.fillRect(COL1, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(LAB_X_MIN, 0);
    tft.print(segMin);
    strcpy(prev_segMin, segMin);
  }
  if (strcmp(prev_segMax, segMax) != 0) {
    tft.fillRect(COL2, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(LAB_X_MAX, 0);
    tft.print(segMax);
    strcpy(prev_segMax, segMax);
  }
}

void drawStaticUI() {
  computeGeometry();
  clearGraphArea();
  drawAxes();
  drawYAxisQuarterLabels();
  drawTimeScaleLabels();   // uses prior sweep seconds if available
  drawBottomNote();
}

// ---------- public API ----------
void TFT_Setup(void) {
  tft.begin();
  tft.setRotation(1);     // landscape 320x240
  tft.fillScreen(COL_BG);

  first_sample = true;
  sweep_new    = true;
  t_prev = t_curr = 0;
  last_sweep_secs = NAN;

  sweep_start_ms = millis();
  drawStaticUI();

  // reset legend caches
  prev_segCur[0] = prev_segMin[0] = prev_segMax[0] = '\0';
  lux_min_seen = NAN;
  lux_max_seen = NAN;
}

// For compatibility with the existing sketches, we keep the same signature:
//   Publish_Data(int value, int /*unused*/, int /*unused*/)
// The first parameter should be the light level you want to plot (lux or
// any other non-negative brightness units).
void Publish_Data(int lux_value, int /*unused1*/, int /*unused2*/) {
  float lux = (lux_value < 0) ? 0.0f : (float)lux_value;

  // Update instantaneous top legend
  updateTopLegendRow(lux);

  // Map to Y
  int16_t y = mapToY(lux);

  if (first_sample || sweep_new) {
    // If we just wrapped, compute & store the elapsed time for the last sweep
    if (!first_sample) {
      unsigned long now_ms = millis();
      if (now_ms >= sweep_start_ms) {
        last_sweep_secs = (now_ms - sweep_start_ms) / 1000.0f;
      } else {
        last_sweep_secs = NAN;
      }
    }
    sweep_start_ms = millis();

    // Rebuild static UI (axes, Y labels, time labels, bottom note)
    drawStaticUI();

    t_prev = t_curr = 0;
    y_prev = y;

    // Initial pixel
    tft.drawPixel(t_curr, y_prev, COL_TRACE);

    first_sample = false;
    sweep_new    = false;
    return;
  }

  // Advance X
  t_prev = t_curr;
  t_curr += t_step;

  // Wrap -> start a new sweep next call (no cross-screen line)
  if (t_curr >= g_w) {
    sweep_new = true;
    return;
  }

  // Draw connected segment
  tft.drawLine(t_prev, y_prev, t_curr, y, COL_TRACE);

  // Update prev
  y_prev = y;
}
