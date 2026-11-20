// Launcher: стартове меню. Вибір застосунку з SD (/boot/*.bin), прошивка в OTA-слот і запуск.
#include <M5Cardputer.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "config.h"

CardDisplay tft;

namespace {

const int kCount = 3;
const char* kLabel[kCount] = {"KolibriX", "Gaming", "Update both from SD"};
const char* kFile[2] = {"/boot/kolibrix.bin", "/boot/gaming.bin"};
const char* kReady[2] = {"ready0", "ready1"};
const esp_partition_subtype_t kSlot[2] = {ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                          ESP_PARTITION_SUBTYPE_APP_OTA_1};
const uint32_t kAutoMs = 5000;

Preferences prefs;
int sel = 0;
bool sdOk = false;
bool autoCancelled = false;
uint32_t bootStart = 0;
String status;
bool dirty = true;

void drawLine(int x, int y, const String& s, uint16_t fg, uint16_t bg) {
  tft.setTextColor(fg, bg);
  tft.drawString(s.c_str(), x, y);
}

void drawProgress(int pct) {
  tft.fillRect(10, 200, 300, 10, COL_DGRAY);
  tft.fillRect(10, 200, 3 * pct, 10, COL_GREEN);   // 300 px при 100%
}

void drawMenu() {
  tft.fillScreen(COL_BLACK);
  drawLine(10, 8, "KolibriX launcher", COL_GREEN, COL_BLACK);
  for (int i = 0; i < kCount; i++) {
    int y = 40 + i * 22;
    bool on = (i == sel);
    uint16_t bg = on ? COL_WHITE : COL_BLACK;
    uint16_t fg = on ? COL_BLACK : COL_WHITE;
    tft.fillRect(8, y - 3, 304, 18, bg);
    drawLine(14, y, kLabel[i], fg, bg);
  }
  if (!autoCancelled && sel < 2) {
    uint32_t el = millis() - bootStart;
    int left = (el >= kAutoMs) ? 0 : (int)((kAutoMs - el) / 1000) + 1;
    drawLine(10, 150, "Auto-start in " + String(left) + " s, press a key to choose",
             COL_YELLOW, COL_BLACK);
  }
  if (!status.isEmpty()) drawLine(10, 176, status, COL_ORANGE, COL_BLACK);
  drawLine(10, 220, "W/S select   Enter run", COL_GRAY, COL_BLACK);
  dirty = false;
}

const esp_partition_t* slotPart(int i) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, kSlot[i], nullptr);
}

// Копіює .bin з SD у OTA-слот і перевіряє образ. Повертає "" при успіху або текст помилки.
String flashFromSd(int i) {
  tft.fillScreen(COL_BLACK);
  drawLine(10, 20, "Flashing " + String(kLabel[i]) + " ...", COL_GREEN, COL_BLACK);

  const esp_partition_t* part = slotPart(i);
  if (!part) return "OTA slot missing, check partition table";

  File f = SD.open(kFile[i], FILE_READ);
  if (!f) return String("not found on SD: ") + kFile[i];
  size_t size = f.size();
  if (size == 0 || size > part->size) {
    f.close();
    return "image is empty or too big";
  }

  esp_ota_handle_t h = 0;
  if (esp_ota_begin(part, size, &h) != ESP_OK) {
    f.close();
    return "ota begin failed";
  }

  uint8_t buf[4096];
  size_t done = 0;
  drawProgress(0);
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    if (esp_ota_write(h, buf, n) != ESP_OK) {
      esp_ota_abort(h);
      f.close();
      return "write failed";
    }
    done += (size_t)n;
    drawProgress((int)(done * 100 / size));
  }
  f.close();

  if (done != size) {
    esp_ota_abort(h);
    return "read incomplete";
  }
  if (esp_ota_end(h) != ESP_OK) return "image invalid (not an ESP32-S3 app?)";
  return String();
}

void bootSlot(int i) {
  const esp_partition_t* p = slotPart(i);
  if (p) esp_ota_set_boot_partition(p);
  prefs.putUChar("last", (uint8_t)i);
  delay(200);
  ESP.restart();
}

void runApp(int i) {
  if (!sdOk) {
    status = "SD card not found";
    dirty = true;
    return;
  }
  if (!prefs.getBool(kReady[i], false)) {
    String err = flashFromSd(i);
    if (!err.isEmpty()) {
      status = "error: " + err;
      dirty = true;
      return;
    }
    prefs.putBool(kReady[i], true);
  }
  bootSlot(i);
}

void updateAll() {
  if (!sdOk) {
    status = "SD card not found";
    dirty = true;
    return;
  }
  for (int i = 0; i < 2; i++) {
    String err = flashFromSd(i);
    if (!err.isEmpty()) {
      status = String(kLabel[i]) + ": " + err;
      dirty = true;
      return;
    }
    prefs.putBool(kReady[i], true);
  }
  status = "both updated, choose an app";
  dirty = true;
}

void activate() {
  if (sel == 2) updateAll();
  else runApp(sel);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  tft.init();
  tft.setRotation(1);
  tft.setBrightness(200);
  tft.fillScreen(COL_BLACK);

  SPI.begin(CARD_SD_SCK, CARD_SD_MISO, CARD_SD_MOSI, CARD_SD_CS);
  sdOk = SD.begin(CARD_SD_CS, SPI, 25000000);
  if (!sdOk) status = "SD card not found, insert it and reset";

  prefs.begin("launcher", false);
  sel = prefs.getUChar("last", 0);
  if (sel > 1) sel = 0;

  bootStart = millis();
  drawMenu();
}

void loop() {
  M5Cardputer.update();

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    autoCancelled = true;                       // будь-яка клавіша зупиняє автозапуск
    for (char c : st.word) {
      if ((c == 'w' || c == ';') && sel > 0) { sel--; dirty = true; }
      else if ((c == 's' || c == '.') && sel < kCount - 1) { sel++; dirty = true; }
    }
    if (st.enter) activate();
  }

  if (!autoCancelled && sel < 2 && millis() - bootStart >= kAutoMs) {
    autoCancelled = true;
    runApp(sel);
  }

  static uint32_t lastSec = 0xFFFFFFFFu;
  if (!autoCancelled && sel < 2 && millis() / 1000 != lastSec) {
    lastSec = millis() / 1000;                  // оновлюємо лічильник раз на секунду
    dirty = true;
  }

  if (dirty) drawMenu();
  delay(10);
}
