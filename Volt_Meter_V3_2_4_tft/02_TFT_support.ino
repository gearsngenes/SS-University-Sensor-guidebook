#include <Adafruit_GFX.h>      // Core graphics library
#include <Adafruit_ILI9341.h>  // 2.4" TFT FeatherWing (ILI9341)
#include <SPI.h>

// FeatherWing default pins (Feather): CS=D9, DC=D10, RST tied to RESET
#define TFT_CS   9
#define TFT_DC   10
#define TFT_RST  -1  // -1 means use board RESET

// Color aliases (keep legacy names used in sketch)
#define ST77XX_BLACK   ILI9341_BLACK
#define ST77XX_WHITE   ILI9341_WHITE
#define ST77XX_GREEN   ILI9341_GREEN
#define ST77XX_YELLOW  ILI9341_YELLOW

// Bands reserved for labels
const int TOP_TEXT_HEIGHT    = 16;  // top band for max label
const int BOTTOM_TEXT_HEIGHT = 16;  // bottom band for min label

// ==== VERTICAL SCALE ====
// Set your signed full-scale magnitude in millivolts.
// Example: 10000 => ±10V; 300 => ±300 mV
const int EMF_FULL_SCALE_MV = 10000;             // <-- change if you want ±300 mV, set 300
const int EMF_MIN_MV        = -EMF_FULL_SCALE_MV;
const int EMF_MAX_MV        =  EMF_FULL_SCALE_MV;

// TFT object
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Graph state
int t_prev = 0;
int t_curr = 0;
int t_step = 3;     // horizontal step in pixels

int y_prev = 0;
int y_curr = 0;

// Per-sweep state
static bool first_sample = true;   // first-ever sample since boot
static bool sweep_new    = true;   // first sample of a new screen sweep
static int  sweep_min    = 0;      // per-sweep min (mV)
static int  sweep_max    = 0;      // per-sweep max (mV)

// Timing for time-scale labeling
static unsigned long sweep_start_ms   = 0;
static float         last_sweep_secs  = NAN;  // duration of the completed sweep (seconds)

// Cached geometry (computed each draw)
int16_t g_w = 0, g_h = 0;
int16_t g_graph_top = 0, g_graph_bottom = 0, g_graph_height = 0;
int16_t g_mid_y = 0;

// --- helpers (forward) ---
void computeGeometry();
void makeGrid();
void drawMinMaxText(int minV, int maxV);
void drawYAxisHalfLabels();
void drawTimeScaleLabels();

void TFT_Setup(void) {
  tft.begin();
  tft.setRotation(1);          // Landscape: 320x240
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

  delay(200);
}

// Compute cached geometry for graph area and midline
void computeGeometry() {
  g_w = tft.width();
  g_h = tft.height();

  // Keep labels clear by shrinking graph area slightly inside bands
  g_graph_top    = TOP_TEXT_HEIGHT + 2;
  g_graph_bottom = g_h - BOTTOM_TEXT_HEIGHT - 3;
  g_graph_height = g_graph_bottom - g_graph_top + 1;

  // 0 mV midline = exact middle of graph area
  g_mid_y = g_graph_top + g_graph_height / 2;
}

// Draw axes and 0-line (graph area only)
void makeGrid() {
  computeGeometry();

  // Clear graph area
  tft.fillRect(0, g_graph_top, g_w, g_graph_height, ST77XX_BLACK);

  // Y-axis at the left
  tft.drawLine(0, g_graph_top, 0, g_graph_bottom, ST77XX_WHITE);

  // 0 mV axis (midline)
  tft.drawLine(0, g_mid_y, g_w - 1, g_mid_y, ST77XX_WHITE);
}

// Update the top/bottom bands with labeled min/max (with units)
void drawMinMaxText(int minV, int maxV) {
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);

  // Top: max(mV)
  tft.fillRect(0, 0, g_w, TOP_TEXT_HEIGHT, ST77XX_BLACK);
  tft.setCursor(2, 2);
  tft.print("max(mV): ");
  tft.print(maxV);

  // Bottom: min(mV)
  tft.fillRect(0, g_h - BOTTOM_TEXT_HEIGHT, g_w, BOTTOM_TEXT_HEIGHT, ST77XX_BLACK);
  tft.setCursor(2, g_h - BOTTOM_TEXT_HEIGHT + 2);
  tft.print("min(mV): ");
  tft.print(minV);
}

// Draw ±half-scale numeric labels on the Y-axis (no units)
void drawYAxisHalfLabels() {
  const int V_HALF = EMF_FULL_SCALE_MV / 2;  // e.g., 5000 for ±10V, 150 for ±300 mV

  // map value -> screen y in graph area
  auto map_to_y = [&](int mv) -> int16_t {
    // map [EMF_MIN_MV..EMF_MAX_MV] to [g_graph_bottom..g_graph_top]
    long yy = g_graph_top + map(mv, EMF_MIN_MV, EMF_MAX_MV, g_graph_height - 1, 0);
    if (yy < g_graph_top) yy = g_graph_top;
    if (yy > g_graph_bottom) yy = g_graph_bottom;
    return (int16_t)yy;
  };

  int16_t y_pos_half = map_to_y(+V_HALF);
  int16_t y_neg_half = map_to_y(-V_HALF);

  // Small white numbers just to the right of the Y-axis
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  // Adjust cursor so the numbers are centered roughly on the tick height
  int16_t txt_h = 8; // default font height at size=1
  int16_t y_txt = 0;

  // +half
  y_txt = y_pos_half - txt_h / 2;
  if (y_txt < g_graph_top) y_txt = g_graph_top;
  if (y_txt > g_graph_bottom - txt_h) y_txt = g_graph_bottom - txt_h;
  tft.setCursor(4, y_txt);
  tft.print(V_HALF);

  // -half
  y_txt = y_neg_half - txt_h / 2;
  if (y_txt < g_graph_top) y_txt = g_graph_top;
  if (y_txt > g_graph_bottom - txt_h) y_txt = g_graph_bottom - txt_h;
  tft.setCursor(4, y_txt);
  tft.print(-V_HALF);
}

// Draw time scale labels inside the graph area:
// "0" near bottom-left, and last sweep duration (e.g. "3.0s") bottom-right.
void drawTimeScaleLabels() {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  const int margin_x = 2;
  const int margin_y = 2;

  // Text sits just above the bottom of the graph area
  int16_t y = g_graph_bottom - 8 - margin_y;  // 8px font height

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

// === Main drawing function used by the measurement sketch ===
// Note: minV/maxV from the main sketch are ignored for display;
// we track per-sweep min/max here to reset on each new screen.
void Publish_Data(int emf, int /*minV*/, int /*maxV*/) {
  // Clamp to signed range so mapping stays on-screen
  if (emf < EMF_MIN_MV) emf = EMF_MIN_MV;
  if (emf > EMF_MAX_MV) emf = EMF_MAX_MV;

  // Map emf to screen y within graph area (signed scale, 0 at midline)
  int this_y = g_graph_top + map(emf, EMF_MIN_MV, EMF_MAX_MV, g_graph_height - 1, 0);
  if (this_y < g_graph_top) this_y = g_graph_top;
  if (this_y > g_graph_bottom) this_y = g_graph_bottom;

  // Handle first sample or start of a new sweep
  if (first_sample || sweep_new) {
    makeGrid();
    drawYAxisHalfLabels();
    drawTimeScaleLabels();

    t_curr = 0;
    t_prev = 0;

    y_curr = this_y;
    y_prev = this_y;

    // Initialize per-sweep min/max to this sample
    sweep_min = emf;
    sweep_max = emf;
    drawMinMaxText(sweep_min, sweep_max);

    // Start timing for this sweep
    sweep_start_ms = millis();

    // Draw initial point
    tft.drawPixel(t_curr, y_curr, ST77XX_GREEN);

    first_sample = false;
    sweep_new    = false;
    return;
  }

  // Advance time (x) coordinate
  t_prev = t_curr;
  t_curr += t_step;

  // If we reached the right edge, end this sweep:
  // - compute duration,
  // - mark to start fresh next call (which will redraw labels accordingly)
  if (t_curr >= g_w) {
    unsigned long now_ms = millis();
    if (now_ms >= sweep_start_ms) {
      last_sweep_secs = (now_ms - sweep_start_ms) / 1000.0f;
    } else {
      last_sweep_secs = NAN; // unexpected clock wrap
    }
    sweep_new = true;
    return;  // do not draw a connecting line across the wrap
  }

  // Normal case: draw line segment from previous point to current
  y_prev = y_curr;
  y_curr = this_y;
  tft.drawLine(t_prev, y_prev, t_curr, y_curr, ST77XX_GREEN);

  // Update per-sweep min/max with this sample and refresh labels
  if (emf < sweep_min) sweep_min = emf;
  if (emf > sweep_max) sweep_max = emf;
  drawMinMaxText(sweep_min, sweep_max);
}
