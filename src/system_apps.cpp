// Системні застосунки: Settings (налаштування) і Device (інформація про пристрій)
#include <WiFi.h>
#include "core.h"

namespace {

int g_bright = 200;                  // яскравість екрана, 16..255

// Вивід, який нікуди не йде: для виклику команд без терміналу
class NullOut : public Print {
public:
  size_t write(uint8_t) override { return 1; }
};

class SettingsApp : public AppBase {
public:
  String title() const override { return "Settings"; }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    if (k.enter) { activate(); return; }
    switch (navOf(k, true)) {
      case Nav::Up:    if (_sel > 0) _sel--; break;
      case Nav::Down:  if (_sel < kItems - 1) _sel++; break;
      case Nav::Left:  if (_sel == 0) adjustBrightness(-16); break;
      case Nav::Right: if (_sel == 0) adjustBrightness(+16); break;
      default: break;
    }
  }

  void draw(const Box& b, bool focused) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    tft.fillRect(b.x, b.y, b.w, g_lineH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);
    drawText(b.x + 2, b.y, "Settings");

    String labels[kItems];
    labels[0] = "Brightness   < " + String(g_bright) + " >";
    labels[1] = "Wi-Fi         " + (wifiUp() ? WiFi.localIP().toString() : String("offline"));
    labels[2] = "Sign out      (" + sessionUser() + ")";
    labels[3] = "About device";

    for (int i = 0; i < kItems; i++) {
      int y = b.y + (i + 2) * g_lineH;
      bool sel = (i == _sel) && focused;
      uint16_t bg = sel ? COL_WHITE : COL_BLACK;
      uint16_t fg = sel ? COL_BLACK : COL_WHITE;
      tft.fillRect(b.x, y, b.w, g_lineH, bg);
      tft.setTextColor(fg, bg);
      drawText(b.x + 4, y, fitText(labels[i], b.w - 8));
    }

    tft.setTextColor(COL_GRAY, COL_BLACK);
    drawText(b.x + 4, b.y + b.h - g_lineH, "W/S select  A/D change  Enter run");
  }

private:
  static const int kItems = 4;

  void adjustBrightness(int d) {
    int v = g_bright + d;
    if (v < 16) v = 16;
    if (v > 255) v = 255;
    g_bright = v;
    tft.setBrightness((uint8_t)g_bright);
  }

  void activate() {
    if (_sel == 2) {                          // Sign out: та сама логіка, що й команда logout
      NullOut quiet;
      shellExec("logout", quiet);
    } else if (_sel == 3) {
      openDevice();
    }
  }

  int _sel = 0;
};

class DeviceApp : public AppBase {
public:
  String title() const override { return "Device"; }

  void onKey(const KeyEvent&) override {}

  // Оновлюємо показники раз на секунду, а не на кожному циклі
  void tick() override {
    uint32_t s = millis() / 1000;
    if (s != _lastSec) {
      _lastSec = s;
      wmRedraw();
    }
  }

  void draw(const Box& b, bool) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    tft.setTextColor(COL_GREEN, COL_BLACK);

    int y = b.y + 2;
    auto line = [&](const String& s) {
      drawText(b.x + 4, y, fitText(s, b.w - 8));
      y += g_lineH + 2;
    };

    line("Chip:    " + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()));
    line("CPU:     " + String(ESP.getCpuFreqMHz()) + " MHz");
    line("Flash:   " + String(ESP.getFlashChipSize() / 1048576) + " MB");
    line("Heap:    " + String(ESP.getFreeHeap() / 1024) + " KB free of " +
         String(ESP.getHeapSize() / 1024) + " KB");
    line("SDK:     " + String(ESP.getSdkVersion()));
    line("Uptime:  " + String(millis() / 60000) + " min");
    int bat = batteryPct();
    line("Battery: " + (bat >= 0 ? String(bat) + "%" : String("n/a")));
    line("Wi-Fi:   " + (wifiUp() ? WiFi.localIP().toString() : String("offline")));
    line("MAC:     " + WiFi.macAddress());
    line("User:    " + sessionUser());
  }

private:
  uint32_t _lastSec = 0xFFFFFFFFu;
};

}  // namespace

void openSettings() { wmOpen(new SettingsApp()); }
void openDevice() { wmOpen(new DeviceApp()); }
