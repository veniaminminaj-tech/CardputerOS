// Files: файловый менеджер SD-карты с просмотром текстовых файлов
#include <algorithm>
#include <SD.h>
#include "core.h"

namespace {

struct Ent {
  String name;
  bool dir;
};

class FilesApp : public AppBase {
public:
  explicit FilesApp(const String& dir) : _dir(dir) { reload(); }

  String title() const override { return "Files"; }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    Nav nv = navOf(k, true);

    if (_viewing) {                               // режим просмотра файла
      if (k.back || nv == Nav::Left) { _viewing = false; return; }
      if (nv == Nav::Up && _tscroll > 0) _tscroll--;
      if (nv == Nav::Down && _tscroll + 1 < (int)_text.size()) _tscroll++;
      return;
    }
    // режим списка
    if (k.back || nv == Nav::Left) { goUp(); return; }
    if (nv == Nav::Up && _sel > 0) _sel--;
    if (nv == Nav::Down && _sel + 1 < (int)_ents.size()) _sel++;
    if (k.enter && !_ents.empty()) openEntry(_sel);
  }

  void draw(const Box& b, bool focused) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    int rows = b.h / g_lineH - 1;
    if (rows < 1) return;

    tft.fillRect(b.x, b.y, b.w, g_lineH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);

    if (_viewing) {
      drawText(b.x + 2, b.y, fitText(_viewName + "  [Del] back", b.w - 4));
      tft.setTextColor(COL_WHITE, COL_BLACK);
      for (int i = 0; i < rows; i++) {
        int idx = _tscroll + i;
        if (idx >= (int)_text.size()) break;
        drawText(b.x + 2, b.y + (i + 1) * g_lineH, _text[idx]);
      }
      return;
    }

    drawText(b.x + 2, b.y, fitText(_dir, b.w - 4));
    if (_ents.empty()) {
      tft.setTextColor(COL_GRAY, COL_BLACK);
      drawText(b.x + 2, b.y + g_lineH, "(empty or no SD card)");
      return;
    }
    if (_sel < _top) _top = _sel;
    if (_sel >= _top + rows) _top = _sel - rows + 1;

    for (int i = 0; i < rows; i++) {
      int idx = _top + i;
      if (idx >= (int)_ents.size()) break;
      bool sel = (idx == _sel);
      uint16_t bg = (sel && focused) ? COL_WHITE : COL_BLACK;
      uint16_t fg = (sel && focused) ? COL_BLACK : (_ents[idx].dir ? COL_CYAN : COL_WHITE);
      int y = b.y + (i + 1) * g_lineH;
      tft.fillRect(b.x, y, b.w, g_lineH, bg);
      tft.setTextColor(fg, bg);
      String label = _ents[idx].dir ? _ents[idx].name + "/" : _ents[idx].name;
      drawText(b.x + 2, y, fitText(label, b.w - 4));
    }
  }

private:
  void reload() {
    _ents.clear();
    File d = canRead(_dir) ? SD.open(_dir) : File();
    if (d && d.isDirectory()) {
      for (File f = d.openNextFile(); f && _ents.size() < 200; f = d.openNextFile()) {
        Ent e;
        e.name = fsBaseName(f.name());
        e.dir = f.isDirectory();
        f.close();
        _ents.push_back(e);
      }
    }
    d.close();
    // сначала папки, потом файлы
    std::stable_partition(_ents.begin(), _ents.end(), [](const Ent& e) { return e.dir; });
    _sel = 0;
    _top = 0;
  }

  void goUp() {
    if (_dir == "/") return;
    int i = _dir.lastIndexOf('/');
    _dir = (i <= 0) ? String("/") : _dir.substring(0, i);
    reload();
  }

  void openEntry(int i) {
    String path = fsJoin(_dir, _ents[i].name);
    if (_ents[i].dir) {
      _dir = path;
      reload();
      return;
    }
    loadText(path);
  }

  void loadText(const String& path) {
    _text.clear();
    _tscroll = 0;
    _viewName = fsBaseName(path);
    _viewing = true;
    if (!canRead(path)) { _text.push_back("Permission denied"); return; }
    File f = SD.open(path, FILE_READ);
    if (!f) { _text.push_back("cannot open file"); return; }

    String cur;
    size_t bytes = 0;
    while (f.available() && bytes < 3000 && _text.size() < 300) {
      char c = (char)f.read();
      bytes++;
      if (c == '\r') continue;
      if (c == '\n') { _text.push_back(cur); cur = ""; continue; }
      if (c == '\t') c = ' ';
      if ((int)cur.length() >= g_cols) { _text.push_back(cur); cur = ""; }
      cur += c;
    }
    if (cur.length()) _text.push_back(cur);
    f.close();
  }

  String _dir;
  std::vector<Ent> _ents;
  int _sel = 0, _top = 0;
  bool _viewing = false;
  std::vector<String> _text;
  int _tscroll = 0;
  String _viewName;
};

}  // namespace

void openFiles(const String& dir) { wmOpen(new FilesApp(dir)); }
