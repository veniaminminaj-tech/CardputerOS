// Media Player: MJPEG video frames and WAV audio from microSD.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <AnimatedGIF.h>
#define MINIMP3_IMPLEMENTATION
#include <minimp3.h>
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
bool isVideo(const String& n) { return endsWithIgnoreCase(n, ".mjpg") || endsWithIgnoreCase(n, ".mjpeg") || endsWithIgnoreCase(n, ".avi") || endsWithIgnoreCase(n, ".mov") || endsWithIgnoreCase(n, ".gif"); }
bool isAudio(const String& n) { return endsWithIgnoreCase(n, ".wav") || endsWithIgnoreCase(n, ".mp3"); }

// AnimatedGIF uses callbacks so the file is streamed from SD instead of loaded
// completely into RAM. The draw callback writes one RGB565 scanline at a time.
AnimatedGIF gifDecoder;
File gifFile;
int gifClipX = 0, gifClipY = 0, gifClipW = 320, gifClipH = 220;

void* gifOpenFile(const char* name, int32_t* size) {
  gifFile = SD.open(name, FILE_READ);
  if (!gifFile) return nullptr;
  *size = (int32_t)gifFile.size();
  return &gifFile;
}
void gifCloseFile(void* handle) {
  File* f = static_cast<File*>(handle);
  if (f) f->close();
}
int32_t gifReadFile(GIFFILE* file, uint8_t* buffer, int32_t length) {
  if (!file || !file->fHandle || length <= 0) return 0;
  File* f = static_cast<File*>(file->fHandle);
  int32_t got = (int32_t)f->read(buffer, (size_t)length);
  file->iPos = (int32_t)f->position();
  return got;
}
int32_t gifSeekFile(GIFFILE* file, int32_t position) {
  if (!file || !file->fHandle || position < 0 || position > file->iSize) return 0;
  File* f = static_cast<File*>(file->fHandle);
  if (!f->seek((uint32_t)position)) return 0;
  file->iPos = position;
  return position;
}
void gifDrawLine(GIFDRAW* draw) {
  if (!draw || !draw->pPixels || !draw->pPalette) return;
  const int y = gifClipY + draw->iY + draw->y;
  if (y < gifClipY || y >= gifClipY + gifClipH) return;
  static uint16_t line[320];
  const int x0 = gifClipX + draw->iX;
  int runStart = -1, runLen = 0;
  for (int i = 0; i < draw->iWidth; ++i) {
    const int x = x0 + i;
    const uint8_t index = draw->pPixels[i];
    const bool transparent = draw->ucHasTransparency && index == draw->ucTransparent;
    if (!transparent && x >= gifClipX && x < gifClipX + gifClipW && x >= 0 && x < 320) {
      line[runLen++] = draw->pPalette[index];
      if (runStart < 0) runStart = x;
    } else if (runLen) {
      tft.pushImage(runStart, y, runLen, 1, line);
      runStart = -1; runLen = 0;
    }
    if (runLen == 320) {
      tft.pushImage(runStart, y, runLen, 1, line);
      runStart = -1; runLen = 0;
    }
  }
  if (runLen) tft.pushImage(runStart, y, runLen, 1, line);
}

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
        if (_mp3Mode) {
          _audioPaused = !_audioPaused;
          if (_audioPaused) M5Cardputer.Speaker.stop();
        } else if (M5Cardputer.Speaker.isPlaying()) M5Cardputer.Speaker.stop();
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
    if (_audio && _mp3Mode && !_audioPaused) { tickMp3(); return; }
    if (!_video || _paused) return;
    uint32_t now = millis();
    if (_gifMode) {
      if ((int32_t)(now - _gifNextFrame) < 0) return;
      int delayMs = 100;
      int rc = gifDecoder.playFrame(false, &delayMs);
      if (rc < 0) {
        stopVideo();
        _message = "GIF decode error";
      } else {
        if (rc == 0) gifDecoder.reset(); // loop animated GIFs
        if (delayMs < 20) delayMs = 20;
        if (delayMs > 1000) delayMs = 1000;
        _gifNextFrame = now + (uint32_t)delayMs;
      }
      wmRedraw();
      return;
    }
    if (!_file || now - _lastFrame < FRAME_INTERVAL_MS) return;
    _lastFrame = now;
    if (!drawNextFrame()) {
      stopVideo();
      _message = "Video ended / invalid MJPEG";
    }
    wmRedraw();
  }

  void draw(const Box& b, bool focused) override {
    int rowH = g_lineH;
    // GIF frames may update only changed scanlines. Keep the previous canvas
    // between WM redraws; clear it once when a new GIF is opened.
    if (!_gifMode || _gifFirstDraw) {
      tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
      _gifFirstDraw = false;
    }
    tft.fillRect(b.x, b.y, b.w, rowH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);

    if (_video) {
      if (_gifMode) {
        gifClipX = b.x;
        gifClipY = b.y + rowH;
        gifClipW = b.w;
        gifClipH = b.h - rowH;
        tft.setTextColor(COL_WHITE, COL_DGRAY);
        drawText(b.x + 3, b.y, _paused ? "GIF PAUSED [Enter]" : "GIF PLAYING [Enter]");
        return;
      }
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
      drawText(b.x + 3, b.y, _mp3Mode ? (_audioPaused ? "MP3 PAUSED [Enter]" : "MP3 PLAYING [Enter]") : "WAV AUDIO [Enter] pause/resume");
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
      drawText(b.x + 3, b.y + rowH * 2, "No MJPEG/AVI-MJPEG/MOV-MJPEG/GIF/WAV files");
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
    if (endsWithIgnoreCase(e.name, ".gif")) startGif(path);
    else if (isVideo(e.name)) startVideo(path);
    else if (isAudio(e.name)) startAudio(path);
  }
  void startGif(const String& path) {
    stopVideo();
    gifDecoder.begin(GIF_PALETTE_RGB565_LE);
    if (!gifDecoder.open(path.c_str(), gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDrawLine)) {
      if (gifFile) gifFile.close();
      _message = "Cannot decode GIF";
      return;
    }
    _video = true;
    _gifMode = true;
    _gifFirstDraw = true;
    _paused = false;
    _gifNextFrame = 0;
    _message = "Animated GIF";
  }
  void startVideo(const String& path) {
    stopVideo();
    _file = SD.open(path, FILE_READ);
    if (!_file) { _message = "Cannot open video"; return; }
    _video = true; _paused = false; _lastFrame = 0;
    _message = endsWithIgnoreCase(path, ".avi") ? "AVI/MOV: MJPEG codec only" : "Raw MJPEG frames";
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
    if (_gifMode) gifDecoder.close();
    _gifMode = false;
    _gifFirstDraw = false;
    if (_file) _file.close();
    if (_jpgData) free(_jpgData);
    _jpgData = nullptr; _jpgLen = 0;
    _video = false; _paused = false;
  }
  void startMp3(const String& path) {
    stopAudio();
    _mp3File = SD.open(path, FILE_READ);
    if (!_mp3File || _mp3File.isDirectory()) {
      _mp3File.close();
      _message = "Cannot open MP3";
      return;
    }

    // Skip an optional ID3v2 tag; MP3 frames remain streamed from SD.
    uint8_t id3[10];
    size_t got = _mp3File.read(id3, sizeof(id3));
    if (got == sizeof(id3) && id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3') {
      uint32_t tagSize = ((uint32_t)(id3[6] & 0x7F) << 21) |
                         ((uint32_t)(id3[7] & 0x7F) << 14) |
                         ((uint32_t)(id3[8] & 0x7F) << 7) |
                         (uint32_t)(id3[9] & 0x7F);
      uint32_t tagEnd = 10 + tagSize + ((id3[5] & 0x10) ? 10 : 0);
      if (tagEnd > _mp3File.size() || !_mp3File.seek(tagEnd)) _mp3File.seek(0);
    } else {
      _mp3File.seek(0);
    }

    mp3dec_init(&_mp3Decoder);
    _mp3InputLen = 0;
    _mp3Eof = false;
    _mp3Pending = false;
    _mp3BufferIndex = 0;
    _mp3Rate = 0;
    _audioPaused = false;
    _mp3Mode = true;
    _audio = true;
    _message = "MP3 decoder: streaming from SD";
  }

  void fillMp3Input() {
    while (_mp3InputLen < (int)sizeof(_mp3Input) && _mp3File.available()) {
      int n = _mp3File.read(_mp3Input + _mp3InputLen,
                            sizeof(_mp3Input) - (size_t)_mp3InputLen);
      if (n <= 0) break;
      _mp3InputLen += n;
    }
    if (!_mp3File.available()) _mp3Eof = true;
  }

  void consumeMp3Input(int count) {
    if (count <= 0) return;
    if (count >= _mp3InputLen) {
      _mp3InputLen = 0;
      return;
    }
    memmove(_mp3Input, _mp3Input + count, (size_t)(_mp3InputLen - count));
    _mp3InputLen -= count;
  }

  void tickMp3() {
    // Do not overwrite PCM buffers while I2S still reads queued samples.
    if (M5Cardputer.Speaker.isPlaying(0) >= 2) return;
    if (_mp3Pending) {
      int16_t* pcm = _mp3Pcm[_mp3BufferIndex];
      if (!M5Cardputer.Speaker.playRaw(pcm, _mp3Samples * _mp3Channels,
                                      _mp3Rate, _mp3Channels == 2, 1, 0, false)) {
        _message = "MP3 audio output unavailable";
        stopAudio();
        return;
      }
      _mp3Pending = false;
      _mp3BufferIndex ^= 1;
      return;
    }

    fillMp3Input();
    if (_mp3InputLen < 4) {
      if (_mp3Eof) {
        stopAudio();
        _message = "MP3 finished";
      }
      return;
    }

    mp3dec_frame_info_t info = {};
    int samples = mp3dec_decode_frame(&_mp3Decoder, _mp3Input, _mp3InputLen,
                                      _mp3Pcm[_mp3BufferIndex], &info);
    if (samples > 0 && info.frame_bytes > 0 && info.channels > 0 && info.hz > 0) {
      int consumed = info.frame_offset + info.frame_bytes;
      if (consumed > 0 && consumed <= _mp3InputLen) consumeMp3Input(consumed);
      _mp3Samples = samples;
      _mp3Channels = info.channels;
      _mp3Rate = (uint32_t)info.hz;
      _mp3Pending = true;
      return;
    }

    if (info.frame_bytes > 0) {
      int consumed = info.frame_offset + info.frame_bytes;
      consumeMp3Input(consumed > 0 ? consumed : 1);
    } else if (_mp3InputLen > 3) {
      consumeMp3Input(1); // resynchronise on the next possible MPEG audio frame
    } else if (_mp3Eof) {
      stopAudio();
      _message = "MP3 ended (no more frames)";
    }
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
    if (_mp3File) _mp3File.close();
    _mp3Mode = false;
    _audioPaused = false;
    _mp3Pending = false;
    _mp3InputLen = 0;
    if (_wavData) free(_wavData);
    _wavData = nullptr; _wavLen = 0; _audio = false;
  }

  String _dir = "/";
  String _current;
  String _message = "MJPEG + AVI/MOV(MJPEG) + WAV";
  std::vector<Entry> _items;
  int _sel = 0, _top = 0;
  bool _video = false, _paused = false, _audio = false, _gifMode = false, _gifFirstDraw = false;
  bool _mp3Mode = false, _audioPaused = false, _mp3Eof = false, _mp3Pending = false;
  uint32_t _lastFrame = 0, _gifNextFrame = 0, _mp3Rate = 0;
  mp3dec_t _mp3Decoder;
  File _mp3File;
  uint8_t _mp3Input[4096];
  int _mp3InputLen = 0, _mp3Samples = 0, _mp3Channels = 0, _mp3BufferIndex = 0;
  int16_t _mp3Pcm[2][MINIMP3_MAX_SAMPLES_PER_FRAME];
  File _file;
  uint8_t* _jpgData = nullptr;
  size_t _jpgLen = 0;
  uint8_t* _wavData = nullptr;
  size_t _wavLen = 0;
};
} // namespace

void openMediaPlayer() { wmOpen(new MediaPlayerApp()); }
