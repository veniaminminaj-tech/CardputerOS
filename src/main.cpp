// KolibriX: точка входа, рабочий стол, горячие клавиши, строка состояния
#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include "core.h"
#include "boot_guard.h"

CardDisplay tft;
int g_charW = 6, g_lineH = 8, g_cols = 52;
static bool g_sdOk = false;

// ---------- Системные функции ----------
String clockText() {
  char buf[16];
  time_t now = time(nullptr);
  if (now > 1700000000) {                       // время синхронизировано по NTP
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, sizeof(buf), "%H:%M", &t);
  } else {
    snprintf(buf, sizeof(buf), "up %lum", (unsigned long)(millis() / 60000UL));
  }
  return String(buf);
}

int batteryPct() { return M5.Power.getBatteryLevel(); }
bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

// ---------- KolibriX desktop ----------
struct Launcher {
  String name;
  String path;     // путь к .lua (для kind == 3)
  int kind;        // 0 Terminal, 1 Files, 2 Browser, 3 Script
  uint16_t color;
};

static std::vector<Launcher> g_apps;
static int g_sel = 0;        // выбранная иконка
static int g_rowTop = 0;     // первая видимая строка сетки
// Компактная сетка 4x2 для экрана 320x240.
static const int TASKBAR_H = 22;
static const int ICON_COLS = 4;
static const int ICON_W = SCR_W / ICON_COLS;
static const int ICON_H = (SCR_H - STATUS_H - TASKBAR_H) / 2;
static const int ICON_SZ = 28;

static void scanApps() {
  g_apps.clear();
  g_apps.push_back({"Settings", "", 5, 0x8410});
  g_apps.push_back({"Browser", "", 2, 0x1A9F});
  g_apps.push_back({"Shell", "", 0, 0x0340});
  g_apps.push_back({"Telegram", "", 4, 0x2D9F});
  g_apps.push_back({"Device", "", 6, 0x07F0});
  g_apps.push_back({"Files", "", 1, 0x03DF});
  if (!g_sdOk) return;

  File dir = SD.open("/apps");
  if (!dir || !dir.isDirectory()) { dir.close(); return; }
  for (File f = dir.openNextFile(); f && g_apps.size() < 32; f = dir.openNextFile()) {
    String name = fsBaseName(f.name());
    bool isDir = f.isDirectory();
    f.close();
    if (isDir || !name.endsWith(".lua")) continue;
    String path = "/apps/" + name;
    String title = scriptReadName(path);
    if (title.isEmpty()) title = name.substring(0, name.length() - 4);
    g_apps.push_back({title, path, 3, COL_ORANGE});
  }
  dir.close();
}

static void launch(int i) {
  switch (g_apps[i].kind) {
    case 0: openTerminal(); break;
    case 1: openFiles("/"); break;
    case 2: openBrowser(""); break;
    case 4: openTelegram(); break;
    case 5: openSettings(); break;
    case 6: openDevice(); break;
    case 3: openScript(g_apps[i].path); break;
    default: break;
  }
}

// ---------- Рисование ----------
static void drawDesktop() {
  // Темно-бирюзовый рабочий стол.
  tft.fillRect(0, STATUS_H, SCR_W, SCR_H - STATUS_H - TASKBAR_H, COL_DESK);
  for (int y = STATUS_H; y < SCR_H - TASKBAR_H; y += 4) {
    uint16_t shade = (y < 112) ? 0x10A3 : 0x0861;
    tft.drawFastHLine(0, y, SCR_W, shade);
  }
  const int rowsVisible = (SCR_H - STATUS_H - TASKBAR_H) / ICON_H;
  int selRow = g_sel / ICON_COLS;
  if (selRow < g_rowTop) g_rowTop = selRow;
  if (selRow >= g_rowTop + rowsVisible) g_rowTop = selRow - rowsVisible + 1;

  for (int i = 0; i < (int)g_apps.size(); i++) {
    int r = i / ICON_COLS - g_rowTop;
    if (r < 0 || r >= rowsVisible) continue;
    const Launcher& L = g_apps[i];
    int cx = (i % ICON_COLS) * ICON_W;
    int cy = STATUS_H + 7 + r * ICON_H;
    int ix = cx + (ICON_W - ICON_SZ) / 2;
    int iy = cy + 3;

    tft.fillRoundRect(ix, iy, ICON_SZ, ICON_SZ, 5, L.color);
    if (i == g_sel) tft.drawRoundRect(ix - 2, iy - 2, ICON_SZ + 4, ICON_SZ + 4, 6, COL_WHITE);

    char ch = L.name.charAt(0);
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    tft.setTextColor(COL_WHITE, L.color);
    drawText(ix + (ICON_SZ - g_charW) / 2, iy + (ICON_SZ - g_lineH) / 2, String(ch));

    // подпись обрезается по ширине ячейки (при шрифте 6 px это 8 символов)
    int maxc = ICON_W / g_charW;
    String label = ((int)L.name.length() > maxc) ? L.name.substring(0, maxc - 1) + "." : L.name;
    tft.setTextColor(COL_WHITE, COL_DESK);
    drawText(cx + (ICON_W - (int)label.length() * g_charW) / 2, iy + ICON_SZ + 7, label);
  }
}

static void drawTaskbar() {
  const int y = SCR_H - TASKBAR_H;
  tft.fillRect(0, y, SCR_W, TASKBAR_H, 0x4208);
  tft.drawFastHLine(0, y, SCR_W, COL_WHITE);
  tft.fillRoundRect(4, y + 3, 66, 16, 3, 0xC618);
  tft.drawRoundRect(4, y + 3, 66, 16, 3, COL_WHITE);
  tft.setTextColor(COL_BLACK, 0xC618);
  drawText(10, y + 7, "K  Start");
  tft.fillRoundRect(76, y + 3, 84, 16, 3, 0x2104);
  tft.setTextColor(COL_WHITE, 0x2104);
  drawText(82, y + 7, "KolibriX");
  tft.setTextColor(COL_WHITE, 0x4208);
  drawText(170, y + 7, g_sdOk ? "SD: ready" : "SD: missing");
}

static void drawStatus() {
  tft.fillRect(0, 0, SCR_W, STATUS_H, COL_DGRAY);
  tft.setTextColor(COL_WHITE, COL_DGRAY);
  drawText(4, 3, "KolibriX  " + (sessionLoggedIn() ? sessionUser() : String("locked")));
  String right = wifiUp() ? "WiFi  " : "----  ";
  int bat = batteryPct();
  if (bat >= 0) right += "BAT " + String(bat) + "%  ";
  right += clockText();
  drawText(SCR_W - (int)right.length() * g_charW - 4, 3, right);
}

static void redrawAll() {
  if (wmShowDesktop()) { drawDesktop(); drawTaskbar(); }
  else wmDrawAll();
  drawStatus();
}

// ---------- Ввод ----------
// Клавиши рабочего стола (когда стол показан)
static void desktopKey(const KeyEvent& k) {
  if (k.ctrl && lowerc(k.ch) == 'r') {
    scanApps();                                   // Ctrl+R — перечитать /apps
  } else if (k.enter && !g_apps.empty()) {
    launch(g_sel);
    return;
  } else {
    switch (navOf(k, true)) {
      case Nav::Up:    g_sel -= ICON_COLS; break;
      case Nav::Down:  g_sel += ICON_COLS; break;
      case Nav::Left:  g_sel -= 1; break;
      case Nav::Right: g_sel += 1; break;
      default: break;
    }
  }
  int n = (int)g_apps.size();
  if (g_sel < 0) g_sel = 0;
  if (g_sel >= n) g_sel = (n > 0) ? n - 1 : 0;
}

static void dispatch(const KeyEvent& k) {
  wmRedraw();
  if (!sessionLoggedIn()) { wmTopKey(k); return; }   // до входа доступно только окно Login
  // ---- глобальные горячие клавиши ----
  if (k.opt && k.ch == 0 && !k.enter && !k.back && !k.tab) { wmToggleDesktop(); return; }
  if (k.alt && k.tab) { wmCycle(); return; }                                  // Alt+Tab
  if (k.ctrl && k.alt && lowerc(k.ch) == 't') { openTerminal(); return; }    // Ctrl+Alt+T
  if (k.ctrl && lowerc(k.ch) == 'w') { wmCloseTop(); return; }               // Ctrl+W
  // ---- обычный ввод ----
  if (wmShowDesktop()) { desktopKey(k); return; }
  wmTopKey(k);
}

static void pollKeyboard() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  auto st = M5Cardputer.Keyboard.keysState();

  KeyEvent base;
  base.fn = st.fn;
  base.shift = st.shift;
  base.ctrl = st.ctrl;
  base.alt = st.alt;
  base.opt = st.opt;

  bool any = false;
  if (st.enter) { KeyEvent k = base; k.enter = true; dispatch(k); any = true; }
  if (st.del)   { KeyEvent k = base; k.back = true;  dispatch(k); any = true; }
  if (st.tab)   { KeyEvent k = base; k.tab = true;   dispatch(k); any = true; }
  for (char c : st.word) {
    KeyEvent k = base;
    k.ch = c;
    dispatch(k);
    any = true;
  }
  if (!any && st.opt) dispatch(base);          // одиночное нажатие Opt
}

// ---------- Arduino ----------
void setup() {
  bootReturnToLauncher();                  // наступний старт знову покаже Launcher
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);                // встроенный экран, клавиатура, питание

  tft.init();
  tft.setRotation(1);                          // 320x240, альбомная ориентация
  tft.setBrightness(200);
  tft.setTextSize(1);
  g_charW = tft.textWidth("0");
  g_lineH = tft.fontHeight();
  g_cols = (SCR_W - 4) / g_charW;
  tft.fillScreen(COL_BLACK);

  SPI.begin(CARD_SD_SCK, CARD_SD_MISO, CARD_SD_MOSI, CARD_SD_CS);
  g_sdOk = SD.begin(CARD_SD_CS, SPI, 25000000);
  if (!g_sdOk) Serial.println("SD: not found");

  WiFi.mode(WIFI_STA);
  userDbInit();
  tgInit();
  shellInit();
  scanApps();
  openLogin();
  wmRedraw();
}

void loop() {
  M5Cardputer.update();
  if (shellTakeLogout()) {                 // logout: закрыть окна и показать экран входа
    wmCloseAll();
    sessionSetLoggedIn(false);
    openLogin();
  }
  pollKeyboard();
  wmTick();
  tgTick();

  static uint32_t lastSec = 0xFFFFFFFFu;
  uint32_t sec = millis() / 1000;
  if (sec != lastSec) {                        // часы — раз в секунду
    lastSec = sec;
    drawStatus();
  }
  if (wmTakeDirty()) redrawAll();
  delay(5);
}
