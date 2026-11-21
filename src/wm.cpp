// Менеджер окон: стек окон (последнее = верхнее), фокус, закрытие, Alt+Tab
#include <algorithm>
#include "core.h"

namespace {

struct Win {
  AppBase* app;
  Box r;
};

std::vector<Win> g_win;          // последний элемент — активное окно
bool g_showDesktop = true;
bool g_dirty = true;
int g_cascade = 0;
const size_t MAX_WINDOWS = 6;

// Удаляет окна, которые приложение пометило как закрытые
void reap() {
  bool changed = false;
  for (size_t i = 0; i < g_win.size();) {
    if (g_win[i].app->closeRequested) {
      delete g_win[i].app;
      g_win.erase(g_win.begin() + i);
      changed = true;
    } else {
      i++;
    }
  }
  if (changed) {
    if (g_win.empty()) g_showDesktop = true;
    wmRedraw();
  }
}

}  // namespace

void wmRedraw() { g_dirty = true; }
bool wmTakeDirty() { bool d = g_dirty; g_dirty = false; return d; }
bool wmShowDesktop() { return g_showDesktop; }
bool wmHasWindows() { return !g_win.empty(); }

bool wmOpen(AppBase* app) {
  if (g_win.size() >= MAX_WINDOWS) {
    delete app;
    return false;
  }
  int off = (g_cascade++ % 5) * 12;          // каскад: окна не совпадают полностью
  Win w;
  w.app = app;
  w.r = Box{off, STATUS_H + off, SCR_W - off, SCR_H - STATUS_H - off};
  g_win.push_back(w);
  g_showDesktop = false;
  wmRedraw();
  return true;
}

void wmCloseTop() {
  if (g_win.empty()) return;
  g_win.back().app->closeRequested = true;
  reap();
}

void wmCycle() {
  if (g_showDesktop || g_win.size() < 2) {
    if (!g_win.empty()) g_showDesktop = false;
  } else {
    // последнее окно уходит вниз стека, предыдущее становится активным
    std::rotate(g_win.begin(), g_win.end() - 1, g_win.end());
  }
  wmRedraw();
}

void wmToggleDesktop() {
  if (!g_showDesktop) g_showDesktop = true;
  else if (!g_win.empty()) g_showDesktop = false;
  wmRedraw();
}

void wmTopKey(const KeyEvent& k) {
  if (g_win.empty()) return;
  g_win.back().app->onKey(k);
  reap();
}

void wmTick() {
  for (size_t i = 0; i < g_win.size(); i++) g_win[i].app->tick();
  reap();
}

void wmCloseAll() {
  for (auto& w : g_win) delete w.app;
  g_win.clear();
  g_showDesktop = true;
  g_dirty = true;
}

void wmDrawAll() {
  tft.fillRect(0, STATUS_H, SCR_W, SCR_H - STATUS_H, 0x1028);
  for (size_t i = 0; i < g_win.size(); i++) {
    const Win& w = g_win[i];
    bool top = (i + 1 == g_win.size());
    uint16_t frame = top ? 0x3D9F : 0x2940;
    uint16_t edge = top ? 0xBFFF : COL_GRAY;

    // Rounded Windows 11-inspired window card and blue title bar.
    tft.fillRoundRect(w.r.x, w.r.y, w.r.w, w.r.h, 7, 0x1028);
    tft.fillRoundRect(w.r.x, w.r.y, w.r.w, TITLE_H + 4, 7, frame);
    tft.fillRect(w.r.x, w.r.y + TITLE_H - 2, w.r.w, 4, frame);
    tft.setTextColor(COL_WHITE, frame);
    drawText(w.r.x + 5, w.r.y + 2, w.app->title());

    // Small close control at the right side of the title bar.
    tft.fillRoundRect(w.r.x + w.r.w - 13, w.r.y + 2, 9, 8, 3, 0xD965);
    Box body{w.r.x, w.r.y + TITLE_H + 2, w.r.w, w.r.h - TITLE_H - 2};
    tft.setClipRect(body.x, body.y, body.w, body.h);
    w.app->draw(body, top);
    tft.clearClipRect();
    tft.drawRoundRect(w.r.x, w.r.y, w.r.w, w.r.h, 7, edge);
  }
}
