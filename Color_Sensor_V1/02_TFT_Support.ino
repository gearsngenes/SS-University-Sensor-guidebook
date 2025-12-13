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

// Per-channel trace/text colors, matched to approximate visible color
// based on Adafruit & AS7341 docs (F1..F8 wavelengths). :contentReference[oaicite:1]{index=1}
#define COL_F1        ILI9341_MAGENTA   // F1 ~415nm, Violet
#define COL_F2        0x780F            // F2 ~445nm, Indigo / violet-blue
#define COL_F3        ILI9341_BLUE      // F3 ~480nm, Blue
#define COL_F4        ILI9341_CYAN      // F4 ~515nm, Cyan
#define COL_F5        ILI9341_GREEN     // F5 ~555nm, Green
#define COL_F6        ILI9341_YELLOW    // F6 ~590nm, Yellow
#define COL_F7        0xFD20            // F7 ~630nm, Orange
#define COL_F8        ILI9341_RED       // F8 ~680nm, Red
#define COL_CLEAR     ILI9341_WHITE     // Clear, broadband
#define COL_NIR       0x7BEF            // NIR, light gray

// ===== Vertical scale (raw AS7341 counts) =====
// 0 at bottom, 32000 at top to give more resolution in normal light.
const float COUNT_MIN = 0.0f;
const float COUNT_MAX = 16000.0f;

// Horizontal stepping (pixels per sample)
int t_step = 2;   // smaller => longer sweep (also adjust t_delay in main)

// --- Top legend layout: 2 rows x 5 columns, each cell is ONE number ---
// We'll print F1..F5 on the first row, F6..F8 + Clear + NIR on the second row.
// No labels, just numbers whose color indicates the channel.
const int TOP_ROWS       = 2;
const int TOP_COLS       = 5;
const int CH_COL_W       = 64;     // 320 / 5
const int ROW_CHAR_HEIGHT = 8;     // classic font size=1 height

// UI bands
const int TOP_TEXT_HEIGHT    = TOP_ROWS * ROW_CHAR_HEIGHT; // 16 px total
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

// Previous Y for each of the 10 channels (F1..F8, Clear, NIR)
static int16_t y_prev[10];

// Cached text for each of the 10 channels to minimize redraw
static char prev_val_str[10][8];   // each up to 5 digits + null

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

void drawAxes() {
  // Left Y-axis
  tft.drawLine(0, g_graph_top, 0, g_graph_bottom, COL_AXIS);
  // 0 baseline at the bottom (time axis)
  tft.drawLine(0, g_graph_bottom, g_w - 1, g_graph_bottom, COL_AXIS);
}

void drawYAxisQuarterLabels() {
  // Draw 0, 25%, 50%, 75%, and 100% of full-scale along the left side
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
  int16_t y = g_graph_bottom + 2;  // just below the baseline, inside bottom band

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
  // centered "AS7341 counts" — stays in the bottom band
  tft.fillRect(0, g_h - BOTTOM_TEXT_HEIGHT, g_w, BOTTOM_TEXT_HEIGHT, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  const char* note = "AS7341 spectral counts (0-32000)";
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

// Map channel index [0..9] to its line/text color
static uint16_t colorForIndex(int idx) {
  switch (idx) {
    case 0: return COL_F1;    // F1
    case 1: return COL_F2;    // F2
    case 2: return COL_F3;    // F3
    case 3: return COL_F4;    // F4
    case 4: return COL_F5;    // F5
    case 5: return COL_F6;    // F6
    case 6: return COL_F7;    // F7
    case 7: return COL_F8;    // F8
    case 8: return COL_CLEAR; // Clear
    case 9: return COL_NIR;   // NIR
    default: return COL_TEXT;
  }
}

// Draw/update the top two rows of numbers for all 10 channels.
// Layout:
//
// Row 0 (y=0):   [F1] [F2] [F3] [F4] [F5]
// Row 1 (y=8):   [F6] [F7] [F8] [Clr] [NIR]
//
// Each cell is colored according to its channel, no labels — just numbers.
static void updateTopLegendRow(const float vals[10]) {
  tft.setFont(NULL);     // classic font
  tft.setTextSize(1);

  for (int i = 0; i < 10; i++) {
    char buf[8];
    fmtCounts(buf, sizeof(buf), vals[i]);

    // Only redraw if this value changed since last time
    if (strcmp(buf, prev_val_str[i]) == 0) {
      continue;
    }

    // Determine column and row
    int col = i % TOP_COLS;       // 0..4
    int row = i / TOP_COLS;       // 0 or 1

    int16_t x = col * CH_COL_W;
    int16_t y = row * ROW_CHAR_HEIGHT;

    // Clear the cell region
    tft.fillRect(x, y, CH_COL_W, ROW_CHAR_HEIGHT, COL_BG);

    // Draw number in its channel color
    tft.setTextColor(colorForIndex(i), COL_BG);
    tft.setCursor(x + 2, y);   // small left margin
    tft.print(buf);

    // Cache
    strcpy(prev_val_str[i], buf);
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

  // reset per-channel caches
  for (int i = 0; i < 10; i++) {
    prev_val_str[i][0] = '\0';
    y_prev[i] = g_graph_bottom;
  }
}

// Publish all 10 AS7341 channels to the TFT:
//   f1..f8 = spectral bands, clear_counts = clear channel, nir_counts = NIR.
void Publish_Data(float f1, float f2, float f3, float f4,
                  float f5, float f6, float f7, float f8,
                  float clear_counts, float nir_counts) {
  float vals[10] = { f1, f2, f3, f4, f5, f6, f7, f8, clear_counts, nir_counts };

  // Update top numbers for all channels
  updateTopLegendRow(vals);

  // Map all channels to Y
  int16_t y_curr[10];
  for (int i = 0; i < 10; i++) {
    y_curr[i] = mapToY(vals[i]);
  }

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
    for (int i = 0; i < 10; i++) {
      y_prev[i] = y_curr[i];
      tft.drawPixel(t_curr, y_prev[i], colorForIndex(i));
    }

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

  // Draw connected segments for all channels
  for (int i = 0; i < 10; i++) {
    tft.drawLine(t_prev, y_prev[i], t_curr, y_curr[i], colorForIndex(i));
    y_prev[i] = y_curr[i];
  }
}
