// Scripting: мини-интерпретатор .lua-файлов (ПОДМНОЖЕСТВО, не настоящий Lua)
//
// Поддерживается:
//   -- комментарий
//   -- @name Название     (первая строка: имя на рабочем столе)
//   print(a, b, ...)      аргументы: "строка", число, battery(), uptime(), wifi(), time()
//   sleep(ms)             пауза без блокировки интерфейса
//   wait_key()            ждать любую клавишу
//   clear()               очистить вывод
//   cmd("команда")        выполнить команду терминала и вывести результат
#include <SD.h>
#include "core.h"

namespace {

String trimCopy(String s) {
  s.trim();
  return s;
}

// Делит аргументы по запятым вне кавычек
std::vector<String> splitArgs(const String& s) {
  std::vector<String> out;
  if (trimCopy(s).isEmpty()) return out;
  String cur;
  bool q = false;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '"') q = !q;
    if (c == ',' && !q) {
      out.push_back(trimCopy(cur));
      cur = "";
      continue;
    }
    cur += c;
  }
  out.push_back(trimCopy(cur));
  return out;
}

// Вычисляет аргумент: строку, число или встроенную функцию
String evalArg(const String& raw, bool& ok) {
  String s = trimCopy(raw);
  ok = true;
  if (s.length() >= 2 && s.startsWith("\"") && s.endsWith("\"")) {
    return s.substring(1, s.length() - 1);
  }
  if (s.endsWith("()")) {
    String f = s.substring(0, s.length() - 2);
    if (f == "battery") return String(batteryPct()) + "%";
    if (f == "uptime")  return String((unsigned long)(millis() / 1000)) + " s";
    if (f == "wifi")    return wifiUp() ? String("connected") : String("offline");
    if (f == "time")    return clockText();
    ok = false;
    return String();
  }
  bool num = !s.isEmpty();
  for (unsigned i = 0; i < s.length() && num; i++) {
    char c = s.charAt(i);
    if (!((c >= '0' && c <= '9') || (c == '-' && i == 0))) num = false;
  }
  if (num) return s;
  ok = false;
  return String();
}

class ScriptApp : public AppBase {
public:
  enum State { RUN, SLEEP, WAITKEY, DONE };

  explicit ScriptApp(const String& path) : _path(path), _sink(_out, 300, g_cols) {
    _out.push_back("");
    _name = scriptReadName(path);
    if (_name.isEmpty()) _name = fsBaseName(path);
    load();
  }

  String title() const override { return "Script: " + _name; }

  // Выполняет до 50 строк за тик, пока скрипт не уснул и не ждёт клавишу
  void tick() override {
    if (_state == DONE || _state == WAITKEY) return;
    if (_state == SLEEP) {
      if ((int32_t)(millis() - _wakeAt) < 0) return;
      _state = RUN;
      _dirty = true;
    }
    for (int steps = 0; steps < 50 && _state == RUN; steps++) {
      if (_pc >= _src.size()) {
        _state = DONE;
        log("[finished]");
        break;
      }
      String line = trimCopy(_src[_pc]);
      _pc++;
      execLine(line);
    }
    if (_dirty) {
      _dirty = false;
      wmRedraw();
    }
  }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    if (_state == WAITKEY && (k.ch || k.enter || k.back || k.tab)) {
      _state = RUN;
      _dirty = true;
      return;
    }
    if (_state == DONE && (k.enter || k.back)) {
      closeRequested = true;
      return;
    }
    switch (navOf(k, true)) {
      case Nav::Up:   _scroll++; break;
      case Nav::Down: if (_scroll > 0) _scroll--; break;
      default: break;
    }
  }

  void draw(const Box& b, bool) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);

    const char* st = (_state == RUN)     ? "running"
                   : (_state == SLEEP)   ? "sleep"
                   : (_state == WAITKEY) ? "waiting key"
                   : "done";
    tft.fillRect(b.x, b.y, b.w, g_lineH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);
    drawText(b.x + 2, b.y, fitText(_name + " [" + st + "]", b.w - 4));

    int rows = b.h / g_lineH - 1;
    if (rows < 1) return;
    int total = (int)_out.size();
    int maxScroll = (total > rows) ? total - rows : 0;
    if (_scroll > maxScroll) _scroll = maxScroll;
    int end = total - _scroll;
    int start = end - rows;
    if (start < 0) start = 0;

    tft.setTextColor(COL_GREEN, COL_BLACK);
    int y = b.y + g_lineH;
    for (int i = start; i < end; i++, y += g_lineH) drawText(b.x + 2, y, _out[i]);
  }

private:
  void log(const String& s) {
    _sink.println(s);
    _dirty = true;
  }

  void fail(const String& msg) {
    log("error line " + String((unsigned long)_pc) + ": " + msg);
    _state = DONE;
  }

  void load() {
    File f = SD.open(_path, FILE_READ);
    if (!f) {
      _state = DONE;
      log("cannot open " + _path);
      return;
    }
    String cur;
    size_t bytes = 0;
    while (f.available() && bytes < 8192 && _src.size() < 300) {
      char c = (char)f.read();
      bytes++;
      if (c == '\n') {
        _src.push_back(cur);
        cur = "";
      } else if (c != '\r') {
        cur += c;
      }
    }
    if (cur.length()) _src.push_back(cur);
    f.close();
  }

  void execLine(const String& line) {
    if (line.isEmpty() || line.startsWith("--")) return;
    _dirty = true;

    int lp = line.indexOf('(');
    if (lp <= 0 || !line.endsWith(")")) {
      fail("syntax error");
      return;
    }
    String fn = trimCopy(line.substring(0, lp));
    String argStr = line.substring(lp + 1, line.length() - 1);

    std::vector<String> args;
    std::vector<String> raw = splitArgs(argStr);
    for (size_t i = 0; i < raw.size(); i++) {
      bool ok = false;
      String v = evalArg(raw[i], ok);
      if (!ok) {
        fail("bad argument " + raw[i]);
        return;
      }
      args.push_back(v);
    }

    if (fn == "print") {
      for (size_t i = 0; i < args.size(); i++) {
        if (i) _sink.print(' ');
        _sink.print(args[i]);
      }
      _sink.println();
    } else if (fn == "sleep") {
      uint32_t ms = args.empty() ? 0 : (uint32_t)args[0].toInt();
      _wakeAt = millis() + ms;
      _state = SLEEP;
    } else if (fn == "wait_key") {
      _state = WAITKEY;
      log("[press a key]");
    } else if (fn == "clear") {
      _out.clear();
      _out.push_back("");
    } else if (fn == "cmd") {
      if (!args.empty()) shellExec(args[0], _sink);
    } else {
      fail("unknown function " + fn);
    }
  }

  String _path;
  String _name;
  std::vector<String> _src;
  size_t _pc = 0;
  State _state = RUN;
  uint32_t _wakeAt = 0;
  std::vector<String> _out;     // должен идти до _sink
  LineSink _sink;
  int _scroll = 0;
  bool _dirty = true;
};

}  // namespace

// Читает имя из первой строки вида "-- @name Название"
String scriptReadName(const String& path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return String();
  String line;
  while (f.available() && line.length() < 80) {
    char c = (char)f.read();
    if (c == '\n') break;
    line += c;
  }
  f.close();
  line.trim();
  if (!line.startsWith("-- @name")) return String();
  String name = line.substring(8);
  name.trim();
  return name;
}

void openScript(const String& path) {
  if (!canRead(path)) return;
  wmOpen(new ScriptApp(path));
}
