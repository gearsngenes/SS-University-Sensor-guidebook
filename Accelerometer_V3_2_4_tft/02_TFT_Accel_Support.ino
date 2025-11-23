#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

// FeatherWing 2.4" ILI9341 pins (default CS=9, DC=10 on Feather headers)
#define TFT_CS   9
#define TFT_DC   10
#define TFT_RST  -1    // tied to board reset

// Colors
#define COL_BG        ILI9341_BLACK
#define COL_AXIS      ILI9341_WHITE
#define COL_X         ILI9341_RED
#define COL_Y         ILI9341_GREEN
#define COL_Z         ILI9341_CYAN
#define COL_T         ILI9341_YELLOW
#define COL_TEXT      ILI9341_WHITE

// ===== Vertical scale (m/s^2) =====
const float ACC_FULL_SCALE_MS2 = 20.0f;   // ±40 m/s^2 (~±4g)
const float ACC_MIN_MS2        = -ACC_FULL_SCALE_MS2;
const float ACC_MAX_MS2        =  ACC_FULL_SCALE_MS2;

// Horizontal stepping (pixels per sample)
int t_step = 2;   // smaller => longer sweep (also adjust t_delay in main)

// ------- Top legend layout (single-size, four columns) -------
const int TS_TOP = 16;          // <— change to 1 if you need smaller
const int COL_W  = 80;
const int COL0   = 0;
const int COL1   = 80;
const int COL2   = 160;
const int COL3   = 240;

// Label anchors (left edge for each segment)
const int LAB_X_X = COL0 + 4;
const int LAB_X_Y = COL1 + 4;
const int LAB_X_Z = COL2 + 4;
const int LAB_X_T = COL3 + 4;

// UI bands (TOP depends on TS_TOP)
const int TOP_TEXT_HEIGHT    = 16;
const int BOTTOM_TEXT_HEIGHT = 16;   // bottom band (units + time labels)

// TFT
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Graph geometry
int16_t g_w=0, g_h=0, g_graph_top=0, g_graph_bottom=0, g_graph_height=0, g_mid_y=0;

// Sweep/time state
int t_prev = 0, t_curr = 0;
bool first_sample = true, sweep_new = true;
static unsigned long sweep_start_ms  = 0;
static float         last_sweep_secs = NAN;  // duration of previous sweep

// Per-series previous Y (for connected segments)
int16_t y_prev_x=0, y_prev_y=0, y_prev_z=0, y_prev_t=0;

// Legend caches (to avoid unnecessary redraws)
char prev_segX[16] = "", prev_segY[16] = "", prev_segZ[16] = "", prev_segT[16] = "";

// ---------- helpers ----------
static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }

void computeGeometry() {
  g_w = tft.width();
  g_h = tft.height();
  g_graph_top    = TOP_TEXT_HEIGHT + 2;
  g_graph_bottom = g_h - BOTTOM_TEXT_HEIGHT - 3;
  g_graph_height = g_graph_bottom - g_graph_top + 1;
  g_mid_y        = g_graph_top + g_graph_height / 2;  // 0 m/s^2 axis
}

int16_t mapToY(float val_ms2) {
  float v = clampf(val_ms2, ACC_MIN_MS2, ACC_MAX_MS2);
  float frac = (v - ACC_MIN_MS2) / (ACC_MAX_MS2 - ACC_MIN_MS2);  // 0..1
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
  // 0 m/s^2 midline
  tft.drawLine(0, g_mid_y, g_w - 1, g_mid_y, COL_AXIS);
}

void drawYAxisHalfLabels() {
  const float HALF = ACC_FULL_SCALE_MS2 * 0.5f;

  auto put = [&](float v){
    int16_t y = mapToY(v);
    int16_t y_txt = y - 4;               // ~center for size=1 text (≈8px high)
    if (y_txt < g_graph_top) y_txt = g_graph_top;
    if (y_txt > g_graph_bottom - 8) y_txt = g_graph_bottom - 8;

    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT, COL_BG);  // classic font supports bg erase
    char buf[10];
    snprintf(buf, sizeof(buf), "%d", (int)roundf(v));
    tft.setCursor(4, y_txt);
    tft.print(buf);
  };

  put(+HALF);
  put(-HALF);
}

void drawTimeScaleLabels() {
  const int margin_x = 2;
  int16_t y = g_mid_y + 2;  // just below the axis

  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);

  // Left "0"
  tft.setCursor(margin_x + 8, y);
  tft.print("0");

  // Right measured seconds (if available)
  tft.fillRect(g_w - 70, y, 70, 10, COL_BG);
  if (!isnan(last_sweep_secs) && last_sweep_secs > 0.0f) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fs", last_sweep_secs);
    int16_t text_w = (int16_t)(strlen(buf) * 6); // 6px/char at size=1
    tft.setCursor(g_w - text_w - margin_x, y);
    tft.print(buf);
  }
}

void drawBottomNote() {
  // centered "acc(m/s/s)" — stays in the bottom band
  tft.fillRect(0, g_h - BOTTOM_TEXT_HEIGHT, g_w, BOTTOM_TEXT_HEIGHT, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  const char* note = "acc(m/s/s)";
  int16_t wpx = (int16_t)(strlen(note) * 6); // 6 px/char at size=1
  int16_t x = (g_w - wpx) / 2;
  int16_t y = g_h - BOTTOM_TEXT_HEIGHT + 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(note);
}

// format instantaneous value with 2 decimals, no leading '+', and no leading '0' for |v|<1
static void fmtVal(char* out, size_t n, float v) {
  bool neg = (v < 0.0f);
  float a = fabsf(v);
  a = roundf(a * 100.0f) / 100.0f;   // 2 decimals

  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%.1f", a + 1e-6f);

  // remove leading '0' if less than 1.00 (so "0.12" -> ".12")
  const char* start = tmp;
  if (a < 1.0f && tmp[0] == '0') start = tmp + 1;

  if (neg) snprintf(out, n, "-%s", start);
  else     snprintf(out, n, "%s",  start);
}

// Build four labeled segments first, then print them together on one row
static void updateTopLegendRow(float ax, float ay, float az, float at) {
  char vx[12], vy[12], vz[12], vt[12];
  fmtVal(vx, sizeof(vx), ax);
  fmtVal(vy, sizeof(vy), ay);
  fmtVal(vz, sizeof(vz), az);
  fmtVal(vt, sizeof(vt), at);

  // Make full segment strings once (e.g., "x:-.34")
  char segX[16], segY[16], segZ[16], segT[16];
  snprintf(segX, sizeof(segX), "x:%s", vx);
  snprintf(segY, sizeof(segY), "y:%s", vy);
  snprintf(segZ, sizeof(segZ), "z:%s", vz);
  snprintf(segT, sizeof(segT), "T:%s", vt);

  // Only repaint segments that changed (saves SPI)
  tft.setFont(NULL);                // classic font (bg erase supported)
  tft.setTextSize(2);

  if (strcmp(prev_segX, segX) != 0) {
    // Clear only this column's zone to avoid artifacts
    tft.fillRect(COL0, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_X, COL_BG);
    tft.setCursor(LAB_X_X, 0);
    tft.print(segX);
    strcpy(prev_segX, segX);
  }
  if (strcmp(prev_segY, segY) != 0) {
    tft.fillRect(COL1, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_Y, COL_BG);
    tft.setCursor(LAB_X_Y, 0);
    tft.print(segY);
    strcpy(prev_segY, segY);
  }
  if (strcmp(prev_segZ, segZ) != 0) {
    tft.fillRect(COL2, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_Z, COL_BG);
    tft.setCursor(LAB_X_Z, 0);
    tft.print(segZ);
    strcpy(prev_segZ, segZ);
  }
  if (strcmp(prev_segT, segT) != 0) {
    tft.fillRect(COL3, 0, COL_W, TOP_TEXT_HEIGHT, COL_BG);
    tft.setTextColor(COL_T, COL_BG);
    tft.setCursor(LAB_X_T, 0);
    tft.print(segT);
    strcpy(prev_segT, segT);
  }
}

void drawStaticUI() {
  computeGeometry();
  clearGraphArea();
  drawAxes();
  drawYAxisHalfLabels();
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
  prev_segX[0] = prev_segY[0] = prev_segZ[0] = prev_segT[0] = '\0';
}

void Publish_Data(float ax_ms2, float ay_ms2, float az_ms2, float atotal_ms2) {
  // Update instantaneous top legend (labels + values in one pass)
  updateTopLegendRow(ax_ms2, ay_ms2, az_ms2, atotal_ms2);

  // Map all four series to Y
  int16_t yx = mapToY(ax_ms2);
  int16_t yy = mapToY(ay_ms2);
  int16_t yz = mapToY(az_ms2);
  int16_t yt = mapToY(atotal_ms2);

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
    y_prev_x = yx; y_prev_y = yy; y_prev_z = yz; y_prev_t = yt;

    // initial pixels
    tft.drawPixel(t_curr, y_prev_x, COL_X);
    tft.drawPixel(t_curr, y_prev_y, COL_Y);
    tft.drawPixel(t_curr, y_prev_z, COL_Z);
    tft.drawPixel(t_curr, y_prev_t, COL_T);

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

  // Draw connected segments
  tft.drawLine(t_prev, y_prev_x, t_curr, yx, COL_X);
  tft.drawLine(t_prev, y_prev_y, t_curr, yy, COL_Y);
  tft.drawLine(t_prev, y_prev_z, t_curr, yz, COL_Z);
  tft.drawLine(t_prev, y_prev_t, t_curr, yt, COL_T);

  // Update prevs
  y_prev_x = yx; y_prev_y = yy; y_prev_z = yz; y_prev_t = yt;
}
