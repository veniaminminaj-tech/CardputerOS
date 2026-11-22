#pragma once
// Общие типы и интерфейсы CardputerOS
#include <Arduino.h>
#include <Print.h>
#include <vector>
#include "config.h"

extern int g_charW;   // ширина символа шрифта, px
extern int g_lineH;   // высота строки, px
extern int g_cols;    // символов в строке окна

// ---------- Ввод ----------
struct KeyEvent {
  char ch = 0;                                   // печатный символ, 0 — нет
  bool enter = false, back = false, tab = false;
  bool fn = false, shift = false, ctrl = false, alt = false, opt = false;
};

enum class Nav { None, Up, Down, Left, Right };

inline char lowerc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Стрелки: Fn + ; . , /   |   WASD — если wasd == true
inline Nav navOf(const KeyEvent& k, bool wasd) {
  if (k.fn) {
    switch (k.ch) {
      case ';': return Nav::Up;
      case '.': return Nav::Down;
      case ',': return Nav::Left;
      case '/': return Nav::Right;
      default:  return Nav::None;
    }
  }
  if (wasd && !k.ctrl && !k.alt) {
    switch (lowerc(k.ch)) {
      case 'w': return Nav::Up;
      case 's': return Nav::Down;
      case 'a': return Nav::Left;
      case 'd': return Nav::Right;
      default:  break;
    }
  }
  return Nav::None;
}

// ---------- Окна и приложения ----------
struct Box { int x, y, w, h; };

class AppBase {
public:
  virtual ~AppBase() {}
  virtual String title() const = 0;
  virtual void onKey(const KeyEvent& k) = 0;
  virtual void draw(const Box& body, bool focused) = 0;
  virtual void tick() {}
  bool closeRequested = false;
};

// ---------- Текст (встроенный шрифт выводит только ASCII) ----------
inline String asciiOnly(const String& s) {
  String out;
  out.reserve(s.length());
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s.charAt(i);
    if (c >= 32 && c < 127) out += (char)c;
    else if (c >= 0xC0) out += '?';          // первый байт UTF-8 (кириллица и т.п.)
  }
  return out;
}

inline void drawText(int x, int y, const String& s) {
  String t = asciiOnly(s);
  tft.drawString(t.c_str(), x, y);
}

inline String fitText(const String& s, int px) {
  int maxc = px / g_charW;
  return ((int)s.length() > maxc) ? s.substring(0, maxc) : s;
}

inline String tailText(const String& s, int px) {
  int maxc = px / g_charW;
  return ((int)s.length() > maxc) ? s.substring(s.length() - maxc) : s;
}

inline String fsBaseName(const String& p) {
  int i = p.lastIndexOf('/');
  return (i < 0) ? p : p.substring(i + 1);
}

inline String fsJoin(const String& dir, const String& name) {
  return (dir == "/") ? "/" + name : dir + "/" + name;
}

// ---------- Print -> список строк (терминал и скрипты) ----------
class LineSink : public Print {
public:
  LineSink(std::vector<String>& lines, size_t maxLines, int cols)
    : _lines(lines), _maxLines(maxLines), _cols(cols) {}

  size_t write(uint8_t c) override {
    if (_lines.empty()) _lines.push_back("");
    if (c == '\r') return 1;
    if (c == '\n') {
      _lines.push_back("");
    } else {
      if ((int)_lines.back().length() >= _cols) _lines.push_back("");   // перенос
      _lines.back() += (char)c;
    }
    while (_lines.size() > _maxLines) _lines.erase(_lines.begin());
    return 1;
  }

private:
  std::vector<String>& _lines;
  size_t _maxLines;
  int _cols;
};

// ---------- Менеджер окон (wm.cpp) ----------
bool wmOpen(AppBase* app);          // открыть окно, WM берёт владение app
void wmCloseTop();                  // Ctrl+W
void wmCycle();                     // Alt+Tab
void wmToggleDesktop();             // Opt — показать/скрыть рабочий стол
bool wmShowDesktop();
bool wmHasWindows();
void wmRedraw();                    // пометить экран как требующий перерисовки
bool wmTakeDirty();
void wmTick();
void wmDrawAll();
void wmTopKey(const KeyEvent& k);
void wmCloseAll();                  // закрыть все окна (logout)

// ---------- Система (main.cpp) ----------
String clockText();
int batteryPct();
bool wifiUp();

// ---------- Приложения ----------
void openTerminal();                       // terminal.cpp
void openFiles(const String& dir);         // filemanager.cpp
void openBrowser(const String& url);       // browser.cpp
void openScript(const String& path);       // scripting.cpp
String scriptReadName(const String& path); // scripting.cpp

// ---------- Shell (shell.cpp) ----------
void shellInit();                                                              // окружение, HOME=/
// ---------- Пользователи и права (users.cpp) ----------
bool userDbInit();                                  // /etc/passwd на SD; root/root при первом запуске
bool userExists(const String& name);
bool userValidName(const String& name);
uint32_t userUid(const String& name);
String userHome(const String& name);
bool userCheck(const String& name, const String& pw);
bool userAdd(const String& name, const String& pw);
bool userDel(const String& name);
bool userSetPassword(const String& name, const String& pw);
std::vector<String> userNames();
String sessionUser();
bool sessionIsRoot();
bool sessionLoggedIn();
void sessionSetUser(const String& name);
void sessionSetLoggedIn(bool on);
bool canRead(const String& abs);                    // абсолютный путь
bool canWrite(const String& abs);
String accessError(const String& path);             // "Permission denied" или "No such file or directory"
bool requireRoot(Print& out, const String& cmd);    // печатает ошибку, если не root

// ---------- Вход (login.cpp) ----------
void openLogin();

// ---------- Системні застосунки (system_apps.cpp) ----------
void openSettings();
void openDevice();

// ---------- Telegram Bot API (telegram.cpp) ----------
void tgInit();                                                          // читає /root/telegram.conf
void tgTick();                                                          // опитування getUpdates раз на 5 с
bool tgSend(const String& chat, const String& text, String& err);
int tgCommand(const std::vector<String>& args, Print& out);             // команда tg
void openTelegram();                                                    // вікно чату

// ---------- Запросы пароля, выход, смена пользователя (shell.cpp) ----------
enum PwKind { PW_NONE, PW_SU, PW_SUDO, PW_PASSWD_OLD, PW_NEW, PW_RETYPE };
struct PwRequest {
  PwKind kind = PW_NONE;
  String user;
  String cmd;
};
PwRequest shellTakePwRequest();                     // терминал забирает запрос пароля
bool shellTakeLogout();                             // main забирает запрос logout
void shellSwitchUser(const String& name, bool toHome);
int shellRunAsRoot(const String& cmd, Print& out);

String fsResolve(const String& path);
String shellCwd();
String shellPrompt();                                                          // root@cardputer:~$
int shellExec(const String& line, Print& out);                                 // возвращает код выхода
void shellComplete(const String& word, bool cmdPos, std::vector<String>& out); // для Tab
bool httpFetchText(const String& url, String& body, int& code, String& err);   // browser.cpp, для curl

// ---------- Media Player ----------
void openMediaPlayer();
