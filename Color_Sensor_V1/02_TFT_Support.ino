#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <math.h>

// FeatherWing 2.4" ILI9341 pins (default CS=9, DC=10 on Feather headers)
#define TFT_CS   9
#define TFT_DC   10
#define TFT_RST  -1    // tied to board reset

// Colors
#define COL_BG        ILI9341_BLACK
#define COL_AXIS      ILI9341_WHITE
#define COL_R         ILI9341_RED
#define COL_G         ILI9341_GREEN
#define COL_B         ILI9341_BLUE
#define COL_CLR       ILI9341_YELLOW
#define COL_TEXT      ILI9341_WHITE

// ===== Vertical scale (AS7341 raw counts) =====
// These are dimensionless ADC counts from 0..65535 (16-bit).
const float COUNT_MIN = 0.0f;
const float COUNT_MAX = 65535.0f;

// Horizontal stepping (pixels per sample)
int t_step = 2;   // smaller => longer sweep (also adjust t_delay in main)

// ------- Top legend layout (single-size, four columns) -------
const int TS_TOP = 16;          // text size for top row
const int COL_W  = 80;
const int COL0   = 0;
const int COL1   = 80;
const int COL2   = 160;
const int COL3   = 240;

// Label anchors (left edge for each segment)
const int LAB_X_R   = COL0 + 4;
const int LAB_X_G   = COL1 + 4;
const int LAB_X_B   = COL2 + 4;
const int LAB_X_CLR = COL3 + 4;

// UI bands
const int TOP_TEXT_HEIGHT    = 16;
const int BOTTOM_TEXT_HEIGHT = 16;   // bottom band (units + time labels)

// TFT
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Graph geometry
int16_t g_w = 0, g_h = 0;
int16_t g_graph_top = 0, g_graph_bottom = 0, g_graph_height = 0;
int16_t g_mid_y = 0;   // 0-count axis (bottom of graph)

// Sweep/time state
int t_prev = 0, t_curr = 0;
bool first_sample = true, sweep_new = true;
static unsigned long sweep_start_ms  = 0;
static float         last_sweep_secs = NAN;  // duration of previous sweep

// Per-series previous Y (for connected segments)
int16_t y_prev_r = 0, y_prev_g = 0, y_prev_b = 0, y_prev_c = 0;

// Legend caches (to avoid unnecessary redraws)
char prev_segR[16] = "";
char prev_segG[16] = "";
char prev_segB[16] = "";
char prev_segC[16] = "";

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
  // For this sketch, "0" counts sits at the bottom of the graph
  g_mid_y        = g_graph_bottom;
}

int16_t mapToY(float val_counts) {
  float v = clampf(val_counts, COUNT_MIN, COUNT_MAX);
  float frac = (v - COUNT_MIN) / (COUNT_MAX - COUNT_MIN);  // 0..1
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
  // 0-count baseline at the bottom
  tft.drawLine(0, g_mid_y, g_w - 1, g_mid_y, COL_AXIS);
}

void drawYAxisLabels() {
  // Show three labels: 0, mid, and max counts
  float vals[3] = { COUNT_MIN, (COUNT_MIN + COUNT_MAX) * 0.5f, COUNT_MAX };

  for (int i = 0; i < 3; i++) {
    float v = vals[i];
    int16_t y = mapToY(v);
    int16_t y_txt = y - 4;               // ~center for size=1 text (≈8px high)
    if (y_txt < g_graph_top) y_txt = g_graph_top;
    if (y_txt > g_graph_bottom - 8) y_txt = g_graph_bottom - 8;

    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT, COL_BG);  // classic font supports bg erase
    char buf[14];
    unsigned long u = (unsigned long)(v + 0.5f);
    snprintf(buf, sizeof(buf), "%lu", u);
    tft.setCursor(4, y_txt);
    tft.print(buf);
  }
}

void drawTimeScaleLabels() {
  // "0" on left, "≈Xs" on right, along the bottom axis
  const int margin_x = 2;
  int16_t y = g_mid_y + 2;  // just below the baseline

  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);

  // Left "0"
  tft.setCursor(margin_x + 8, y);
  tft.print("0");

  // Right measured seconds (if available)
  tft.fillRect(g_w - 70, y, 70, 10, COL_BG);
  if (!isnan(last_sweep_secs)) {
    char buf[16];
    snprintf(buf, sizeof(buf), "~%.1fs", last_sweep_secs);
    int16_t text_w = (int16_t)(strlen(buf) * 6); // 6 px/char at size=1
    tft.setCursor(g_w - text_w - margin_x, y);
    tft.print(buf);
  }
}

void drawBottomNote() {
  // centered "AS7341 counts" — stays in the bottom band
  tft.fillRect(0, g_h - BOTTOM_TEXT_HEIGHT, g_w, BOTTOM_TEXT_HEIGHT, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  const char* note = "AS7341 spectral counts";
  int16_t wpx = (int16_t)(strlen(note) * 6); // 6 px/char at size=1
  int16_t x = (g_w - wpx) / 2;
  int16_t y = g_h - BOTTOM_TEXT_HEIGHT + 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(note);
}

// Format counts as an integer string (no decimals)
static void fmtCounts(char* out, size_t n, float v) {
  if (v < 0.0f) v = 0.0f;
  unsigned long u = (unsigned long)(v + 0.5f);
  snprintf(out, n, "%lu", u);
}

// Build four labeled segments first, then print them together on one row
static void updateTopLegendRow(float r_counts, float g_counts, float b_counts, float clr_counts) {
  char vr[12], vg[12], vb[12], vc[12];
  fmtCounts(vr, sizeof(vr), r_counts);
  fmtCounts(vg, sizeof(vg), g_counts);
  fmtCounts(vb, sizeof(vb), b_counts);
  fmtCounts(vc, sizeof(vc), clr_counts);

  // Make full segment strings once (e.g., "R:1234")
  char segR[16], segG[16], segB[16], segC[16];
  snprintf(segR, sizeof(segR), "R:%s", vr);
  snprintf(segG, sizeof(segG), "G:%s", vg);
  snprintf(segB, sizeof(segB), "B:%s", vb);
  snprintf(segC, sizeof(segC), "Clr:%s", vc);

  // Only repaint segments that changed (saves SPI)
  tft.setFont(NULL);                // classic font (bg erase supported)
  tft.setTextSize(1);

  if (strcmp(prev_segR, segR) != 0) {
    tft.fillRect(COL0, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_R, COL_BG);
    tft.setCursor(LAB_X_R, 0);
    tft.print(segR);
    strcpy(prev_segR, segR);
  }
  if (strcmp(prev_segG, segG) != 0) {
    tft.fillRect(COL1, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_G, COL_BG);
    tft.setCursor(LAB_X_G, 0);
    tft.print(segG);
    strcpy(prev_segG, segG);
  }
  if (strcmp(prev_segB, segB) != 0) {
    tft.fillRect(COL2, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_B, COL_BG);
    tft.setCursor(LAB_X_B, 0);
    tft.print(segB);
    strcpy(prev_segB, segB);
  }
  if (strcmp(prev_segC, segC) != 0) {
    tft.fillRect(COL3, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_CLR, COL_BG);
    tft.setCursor(LAB_X_CLR, 0);
    tft.print(segC);
    strcpy(prev_segC, segC);
  }
}

void drawStaticUI() {
  computeGeometry();
  clearGraphArea();
  drawAxes();
  drawYAxisLabels();
  drawTimeScaleLabels();   // uses prior sweep seconds if available
  // Top row will be drawn by updateTopLegendRow() as values arrive
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
  prev_segR[0] = prev_segG[0] = prev_segB[0] = prev_segC[0] = '\0';
}

void Publish_Data(float r_counts, float g_counts, float b_counts, float clr_counts) {
  // Update instantaneous top legend (labels + values in one pass)
  updateTopLegendRow(r_counts, g_counts, b_counts, clr_counts);

  // Map all four series to Y
  int16_t yr = mapToY(r_counts);
  int16_t yg = mapToY(g_counts);
  int16_t yb = mapToY(b_counts);
  int16_t yc = mapToY(clr_counts);

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

    // Rebuild static UI (axes, y labels, time labels below axis)
    drawStaticUI();

    t_prev = t_curr = 0;
    first_sample = false;
    sweep_new    = false;

    y_prev_r = yr;
    y_prev_g = yg;
    y_prev_b = yb;
    y_prev_c = yc;
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

  // Draw connected segments
  tft.drawLine(t_prev, y_prev_r, t_curr, yr, COL_R);
  tft.drawLine(t_prev, y_prev_g, t_curr, yg, COL_G);
  tft.drawLine(t_prev, y_prev_b, t_curr, yb, COL_B);
  tft.drawLine(t_prev, y_prev_c, t_curr, yc, COL_CLR);

  // Update prevs
  y_prev_r = yr;
  y_prev_g = yg;
  y_prev_b = yb;
  y_prev_c = yc;
}
