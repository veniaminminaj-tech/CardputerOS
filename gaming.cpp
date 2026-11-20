// gaming.cpp - standalone Cardputer ADV mini-game app source.
// Build as a separate PlatformIO application with the same board/framework,
// partitions_cardputer.csv, include/ directory and LovyanGFX/M5Cardputer deps.
#include <Arduino.h>
#include <M5Cardputer.h>
#include "config.h"
#include "boot_guard.h"

CardDisplay tft;

namespace {
int playerX = 160;
int targetX = 80;
int targetY = 70;
int score = 0;
uint32_t lastMove = 0;
bool running = true;

void drawGame() {
  tft.fillScreen(COL_BLACK);
  tft.setTextColor(COL_GREEN, COL_BLACK);
  tft.drawString("CARDPUTER ARCADE", 8, 6);
  tft.setTextColor(COL_WHITE, COL_BLACK);
  tft.drawString(("SCORE: " + String(score)).c_str(), 8, 24);
  tft.drawRect(4, 42, 312, 190, COL_GRAY);
  tft.fillCircle(targetX, targetY, 5, COL_YELLOW);
  tft.fillRoundRect(playerX - 9, 207, 18, 12, 3, COL_CYAN);
  tft.setTextColor(COL_GRAY, COL_BLACK);
  tft.drawString("A/D or </> move | ENTER restart", 8, 224);
}

void newTarget() {
  targetX = 14 + (esp_random() % 292);
  targetY = 55 + (esp_random() % 135);
}
}

void setup() {
  bootReturnToLauncher();
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  tft.init();
  tft.setRotation(1);
  tft.setBrightness(200);
  newTarget();
  drawGame();
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto keys = M5Cardputer.Keyboard.keysState();
    for (char c : keys.word) {
      if (c == 'a' || c == 'A' || c == ',') playerX -= 12;
      if (c == 'd' || c == 'D' || c == '/') playerX += 12;
    }
    if (keys.enter) { score = 0; newTarget(); }
    playerX = constrain(playerX, 14, 306);
  }

  if (millis() - lastMove > 35) {
    lastMove = millis();
    if (abs(playerX - targetX) < 11 && targetY > 190) {
      ++score;
      newTarget();
    }
    // The target slowly falls toward the player's paddle.
    static int fall = 0;
    if (++fall >= 4) {
      fall = 0;
      targetY += 2;
      if (targetY > 225) newTarget();
      drawGame();
    }
  }
  delay(5);
}
