// Media Player: MJPEG video frames and WAV audio from microSD.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <stdlib.h>
#include "core.h"

namespace {
constexpr size_t MAX_JPEG_BYTES = 96 * 1024;
constexpr size_t MAX_WAV_BYTES = 384 * 1024;
constexpr uint32_t FRAME_INTERVAL_MS = 100; // target up to 10 fps; source may be slower

bool endsWithIgnoreCase(const String& s, const char* ext) {
  String a = s; a.toLowerCase();
  String b = ext; b.toLowerCase();
  return a.endsWith(b);
}
bool isVideo(const String& n) { return endsWithIgnoreCase(n, ".mjpg") || endsWithIgnoreCase(n, ".mjpeg") || endsWithIgnoreCase(n, ".avi"); }
bool isAudio(const String& n) { return endsWithIgnoreCase(n, ".wav"); }

class MediaPlayerApp : public AppBase {
public:
  MediaPlayerApp() { reload(); }
  ~MediaPlayerApp() override { stopVideo(); stopAudio(); }

  String title() const override { return "Media Player"; }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    Nav nv = navOf(k, true);
    if (k.back || nv == Nav::Left) {
      if (_video) stopVideo();
      else if (_audio) stopAudio();
      else if (_dir != "/") goUp();
      return;
    }
    if (_video) {
      if (k.enter || lowerc(k.ch) == 'p') _paused = !_paused;
      if (lowerc(k.ch) == 's') stopVideo();
      return;
    }
    if (_audio) {
      if (k.enter || lowerc(k.ch) == 'p') {
        if (M5Cardputer.Speaker.isPlaying()) M5Cardputer.Speaker.stop();
        else if (_wavData && _wavLen) M5Cardputer.Speaker.playWav(_wavData, _wavLen, 1, 0, true);
      }
      if (lowerc(k.ch) == 's') stopAudio();
      return;
    }
    if (nv == Nav::Up && _sel > 0) _sel--;
    if (nv == Nav::Down && _sel + 1 < (int)_items.size()) _sel++;
    if (k.enter && !_items.empty()) openSelected();
  }

  void tick() override {
    if (!_video || _paused || !_file) return;
    uint32_t now = millis();
    if (now - _lastFrame < FRAME_INTERVAL_MS) return;
    _lastFrame = now;
    if (!drawNextFrame()) {
      stopVideo();
      _message = "Video ended / invalid MJPEG";
    }
    wmRedraw();
  }

  void draw(const Box& b, bool focused) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    int rowH = g_lineH;
    tft.fillRect(b.x, b.y, b.w, rowH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);

    if (_video) {
      if (_jpgData && _jpgLen) {
        tft.drawJpg(_jpgData, _jpgLen, b.x, b.y + rowH, b.w, b.h - rowH);
      } else {
        tft.setTextColor(COL_GRAY, COL_BLACK);
        drawText(b.x + 3, b.y + rowH * 2, "Waiting for first frame...");
      }
      tft.fillRect(b.x, b.y, b.w, rowH, COL_DGRAY);
      tft.setTextColor(COL_WHITE, COL_DGRAY);
      drawText(b.x + 3, b.y, _paused ? "MJPEG PAUSED [Enter]" : "MJPEG PLAYING [Enter]");
      return;
    }
    if (_audio) {
      drawText(b.x + 3, b.y, "WAV AUDIO [Enter] pause/resume");
      tft.setTextColor(COL_WHITE, COL_BLACK);
      drawText(b.x + 3, b.y + rowH * 2, fitText(_current, b.w - 6));
      tft.setTextColor(COL_GRAY, COL_BLACK);
      drawText(b.x + 3, b.y + rowH * 4, "S stop  Del back");
      drawText(b.x + 3, b.y + rowH * 6, fitText(_message, b.w - 6));
      return;
    }

    drawText(b.x + 3, b.y, fitText("SD " + _dir + "  Enter=open", b.w - 6));
    int rows = (b.h / rowH) - 2;
    if (rows < 1) rows = 1;
    if (_items.empty()) {
      tft.setTextColor(COL_GRAY, COL_BLACK);
      drawText(b.x + 3, b.y + rowH * 2, "No MJPEG/AVI-MJPEG/WAV files");
    } else {
      if (_sel < _top) _top = _sel;
      if (_sel >= _top + rows) _top = _sel - rows + 1;
      for (int i = 0; i < rows; i++) {
        int idx = _top + i;
        if (idx >= (int)_items.size()) break;
        int y = b.y + rowH * (i + 1);
        bool selected = idx == _sel;
        uint16_t bg = (selected && focused) ? COL_WHITE : COL_BLACK;
        uint16_t fg = (selected && focused) ? COL_BLACK : (_items[idx].dir ? COL_CYAN : COL_WHITE);
        tft.fillRect(b.x, y, b.w, rowH, bg);
        tft.setTextColor(fg, bg);
        String label = _items[idx].name + (_items[idx].dir ? "/" : "");
        drawText(b.x + 3, y, fitText(label, b.w - 6));
      }
    }
    tft.setTextColor(COL_GRAY, COL_BLACK);
    drawText(b.x + 3, b.y + b.h - rowH, fitText(_message, b.w - 6));
  }

private:
  struct Entry { String name; bool dir; };
  void reload() {
    _items.clear();
    File d = canRead(_dir) ? SD.open(_dir) : File();
    if (d && d.isDirectory()) {
      for (File f = d.openNextFile(); f && _items.size() < 160; f = d.openNextFile()) {
        Entry e{fsBaseName(f.name()), f.isDirectory()};
        if (e.dir || isVideo(e.name) || isAudio(e.name)) _items.push_back(e);
        f.close();
      }
    }
    d.close();
    _sel = _top = 0;
  }
  void goUp() {
    int slash = _dir.lastIndexOf('/');
    _dir = slash <= 0 ? String("/") : _dir.substring(0, slash);
    reload();
  }
  void openSelected() {
    Entry e = _items[_sel];
    String path = fsJoin(_dir, e.name);
    if (e.dir) { _dir = path; reload(); return; }
    if (!canRead(path)) { _message = "Permission denied"; return; }
    _current = e.name;
    if (isVideo(e.name)) startVideo(path);
    else if (isAudio(e.name)) startAudio(path);
  }
  void startVideo(const String& path) {
    stopVideo();
    _file = SD.open(path, FILE_READ);
    if (!_file) { _message = "Cannot open video"; return; }
    _video = true; _paused = false; _lastFrame = 0;
    _message = endsWithIgnoreCase(path, ".avi") ? "AVI: MJPEG codec only" : "Raw MJPEG frames";
  }
  bool drawNextFrame() {
    if (!_file || !_file.available()) return false;
    uint8_t* jpg = (uint8_t*)malloc(MAX_JPEG_BYTES);
    if (!jpg) { _message = "Not enough RAM for frame"; return false; }
    size_t n = 0;
    bool started = false, ended = false;
    int previous = -1;
    // AVI MJPEG frames are JPEG bitstreams stored in RIFF chunks. Scan for
    // a real JPEG SOI marker; this naturally skips RIFF headers and other chunks.
    while (_file.available() && n < MAX_JPEG_BYTES) {
      int v = _file.read();
      if (v < 0) break;
      uint8_t c = (uint8_t)v;
      if (!started) {
        if (previous == 0xFF && c == 0xD8) {
          jpg[0] = 0xFF; jpg[1] = 0xD8; n = 2; started = true;
        }
        previous = c;
        continue;
      }
      jpg[n++] = c;
      if (n >= 2 && jpg[n - 2] == 0xFF && jpg[n - 1] == 0xD9) {
        ended = true; break;
      }
    }
    if (!ended || n < 4) { free(jpg); return false; }
    if (_jpgData) free(_jpgData);
    _jpgData = jpg;
    _jpgLen = n;
    return true;
  }
  void stopVideo() {
    if (_file) _file.close();
    if (_jpgData) free(_jpgData);
    _jpgData = nullptr; _jpgLen = 0;
    _video = false; _paused = false;
  }
  void startAudio(const String& path) {
    stopAudio();
    File f = SD.open(path, FILE_READ);
    if (!f) { _message = "Cannot open WAV"; return; }
    size_t len = f.size();
    if (len < 44 || len > MAX_WAV_BYTES) {
      f.close(); _message = "WAV must be 44B..384KB"; return;
    }
    _wavData = (uint8_t*)malloc(len);
    if (!_wavData) { f.close(); _message = "Not enough RAM"; return; }
    size_t got = f.read(_wavData, len);
    f.close();
    if (got != len || memcmp(_wavData, "RIFF", 4) || memcmp(_wavData + 8, "WAVE", 4)) {
      free(_wavData); _wavData = nullptr; _message = "Invalid WAV file"; return;
    }
    _wavLen = len;
    _audio = M5Cardputer.Speaker.playWav(_wavData, _wavLen, 1, 0, true);
    _message = _audio ? "WAV playback started" : "Speaker rejected WAV";
  }
  void stopAudio() {
    M5Cardputer.Speaker.stop();
    if (_wavData) free(_wavData);
    _wavData = nullptr; _wavLen = 0; _audio = false;
  }

  String _dir = "/";
  String _current;
  String _message = "MJPEG + AVI(MJPEG) + WAV";
  std::vector<Entry> _items;
  int _sel = 0, _top = 0;
  bool _video = false, _paused = false, _audio = false;
  uint32_t _lastFrame = 0;
  File _file;
  uint8_t* _jpgData = nullptr;
  size_t _jpgLen = 0;
  uint8_t* _wavData = nullptr;
  size_t _wavLen = 0;
};
} // namespace

void openMediaPlayer() { wmOpen(new MediaPlayerApp()); }
