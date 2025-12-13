#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <string.h>
#include <math.h>

// FeatherWing 2.4" ILI9341 pins (default CS=9, DC=10 on Feather headers)
#define TFT_CS   9
#define TFT_DC   10
#define TFT_RST  -1    // tied to board reset

// Colors
#define COL_BG        ILI9341_BLACK
#define COL_AXIS      ILI9341_WHITE
#define COL_GRID      ILI9341_DARKGREY
#define COL_TEXT      ILI9341_WHITE

// Channel colors (bars & numbers)
// Approximate to actual spectral bands of AS7341.
#define COL_F1        ILI9341_MAGENTA   // F1 ~415nm, violet
#define COL_F2        0x780F            // F2 ~445nm, indigo/violet-blue
#define COL_F3        ILI9341_BLUE      // F3 ~480nm, blue
#define COL_F4        ILI9341_CYAN      // F4 ~515nm, cyan
#define COL_F5        ILI9341_GREEN     // F5 ~555nm, green
#define COL_F6        ILI9341_YELLOW    // F6 ~590nm, yellow
#define COL_F7        0xFD20            // F7 ~630nm, orange
#define COL_F8        ILI9341_RED       // F8 ~680nm, red
#define COL_CLEAR     ILI9341_WHITE     // Clear
#define COL_NIR       0x7BEF            // NIR, light gray

// ===== Vertical scale (raw AS7341 counts) =====
const float COUNT_MIN = 0.0f;
const float COUNT_MAX = 32000.0f;   // top of histogram

// Histogram config
const int NUM_CHANNELS = 10;         // F1..F8, Clear, NIR

// TFT + layout
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

int16_t g_w = 0, g_h = 0;
int16_t g_graph_top = 0, g_graph_bottom = 0, g_graph_height = 0;

// UI bands
const int TOP_MARGIN_HEIGHT    = 4;   // small padding at top
const int BOTTOM_TEXT_HEIGHT   = 16;  // bottom band (note + maybe labels)

// Cached previous values to avoid unnecessary redraws of numbers
static char prev_val_str[NUM_CHANNELS][8];

// ---------- helpers ----------
static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static uint16_t colorForIndex(int idx) {
  switch (idx) {
    case 0: return COL_F1;
    case 1: return COL_F2;
    case 2: return COL_F3;
    case 3: return COL_F4;
    case 4: return COL_F5;
    case 5: return COL_F6;
    case 6: return COL_F7;
    case 7: return COL_F8;
    case 8: return COL_CLEAR;
    case 9: return COL_NIR;
    default: return COL_TEXT;
  }
}

void computeGeometry() {
  g_w = tft.width();
  g_h = tft.height();
  g_graph_top    = TOP_MARGIN_HEIGHT;
  g_graph_bottom = g_h - BOTTOM_TEXT_HEIGHT - 1;
  g_graph_height = g_graph_bottom - g_graph_top + 1;
}

// Map [COUNT_MIN..COUNT_MAX] to [g_graph_bottom..g_graph_top]
int16_t mapToY(float val) {
  float v = clampf(val, COUNT_MIN, COUNT_MAX);
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

void drawYAxisQuarterLabels() {
  // 0, 25%, 50%, 75%, 100% of full-scale on left
  float vals[5] = {
    COUNT_MIN,
    COUNT_MIN + 0.25f * (COUNT_MAX - COUNT_MIN),
    COUNT_MIN + 0.50f * (COUNT_MAX - COUNT_MIN),
    COUNT_MIN + 0.75f * (COUNT_MAX - COUNT_MIN),
    COUNT_MAX
  };

  for (int i = 0; i < 5; i++) {
    float v = vals[i];
    int16_t y = mapToY(v);
    int16_t y_txt = y - 4;
    if (y_txt < g_graph_top) y_txt = g_graph_top;
    if (y_txt > g_graph_bottom - 8) y_txt = g_graph_bottom - 8;

    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT, COL_BG);
    char buf[12];
    unsigned long u = (unsigned long)(v + 0.5f);
    snprintf(buf, sizeof(buf), "%lu", u);
    tft.setCursor(2, y_txt);
    tft.print(buf);

    // Horizontal grid line (for 25/50/75%)
    if (i > 0 && i < 4) {
      for (int x = 1; x < g_w - 1; x += 4) {
        tft.drawPixel(x, y, COL_GRID);
      }
    }
  }
}

void drawAxes() {
  // Left Y-axis
  tft.drawLine(0, g_graph_top, 0, g_graph_bottom, COL_AXIS);
  // Bottom X-axis
  tft.drawLine(0, g_graph_bottom, g_w - 1, g_graph_bottom, COL_AXIS);
}

void drawBottomNote() {
  tft.fillRect(0, g_h - BOTTOM_TEXT_HEIGHT, g_w, BOTTOM_TEXT_HEIGHT, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  const char* note = "AS7341 spectrum histogram (0-32000)";
  int16_t wpx = (int16_t)(strlen(note) * 6); // 6 px/char at size=1
  int16_t x = (g_w - wpx) / 2;
  int16_t y = g_h - BOTTOM_TEXT_HEIGHT + 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(note);
}

// Format counts as an integer string
static void fmtCounts(char* out, size_t n, float v) {
  if (v < 0.0f) v = 0.0f;
  unsigned long u = (unsigned long)(v + 0.5f);
  snprintf(out, n, "%lu", u);
}

// Draw the histogram bars and numbers
static void drawHistogram(const float vals[]) {
  clearGraphArea();
  drawAxes();
  drawYAxisQuarterLabels();

  // Bottom ticks for each bar + bars themselves
  int16_t bar_full_w = g_w / NUM_CHANNELS;
  if (bar_full_w < 4) bar_full_w = 4; // safeguard

  for (int i = 0; i < NUM_CHANNELS; i++) {
    uint16_t col = colorForIndex(i);

    // Bar horizontal bounds
    int16_t x0 = i * bar_full_w;
    int16_t x1 = (i == NUM_CHANNELS - 1) ? (g_w - 1) : ((i + 1) * bar_full_w - 1);
    int16_t bar_w = x1 - x0 + 1;
    int16_t bar_margin = bar_w / 8;   // small side margin
    if (bar_margin < 1) bar_margin = 1;

    int16_t bx0 = x0 + bar_margin;
    int16_t bx1 = x1 - bar_margin;
    if (bx1 <= bx0) bx1 = bx0 + 1;

    // Bar vertical bounds
    int16_t y_bottom = g_graph_bottom;
    int16_t y_top    = mapToY(vals[i]);

    // Fill the bar
    tft.fillRect(bx0, y_top, bx1 - bx0 + 1, y_bottom - y_top, col);

    // X-axis tick at bar center
    int16_t cx = (bx0 + bx1) / 2;
    for (int k = 0; k < 4; k++) {
      tft.drawPixel(cx, g_graph_bottom - k, COL_AXIS);
    }

    // Number on top of the bar
    char buf[8];
    fmtCounts(buf, sizeof(buf), vals[i]);

    // Cache (not strictly necessary now, but harmless)
    strcpy(prev_val_str[i], buf);

    tft.setTextSize(1);
    tft.setTextColor(col, COL_BG);

    // Position text slightly above bar top if room, else inside bar
    int16_t text_y = y_top - 10;
    if (text_y < g_graph_top) text_y = g_graph_top;

    // A bit of horizontal centering
    int16_t text_w = strlen(buf) * 6;
    int16_t text_x = cx - text_w / 2;
    if (text_x < bx0) text_x = bx0;
    if (text_x + text_w > bx1) text_x = bx1 - text_w + 1;

    tft.setCursor(text_x, text_y);
    tft.print(buf);
  }

  drawBottomNote();
}

// ---------- public API ----------
void TFT_Setup(void) {
  tft.begin();
  tft.setRotation(1);     // landscape 320x240
  tft.fillScreen(COL_BG);

  computeGeometry();

  // Clear caches
  for (int i = 0; i < NUM_CHANNELS; i++) {
    prev_val_str[i][0] = '\0';
  }

  // Initial frame (blank histogram area + axes + labels)
  clearGraphArea();
  drawAxes();
  drawYAxisQuarterLabels();
  drawBottomNote();
}

// Publish all 10 AS7341 channels to the TFT:
//   f1..f8 = spectral bands, clear_counts = clear channel, nir_counts = NIR.
void Publish_Data(float f1, float f2, float f3, float f4,
                  float f5, float f6, float f7, float f8,
                  float clear_counts, float nir_counts) {
  float vals[NUM_CHANNELS] = {
    f1, f2, f3, f4, f5, f6, f7, f8, clear_counts, nir_counts
  };

  drawHistogram(vals);
}
