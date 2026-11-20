// Terminal: bash-подобная оболочка — история, Tab, Ctrl+L/U/C, прокрутка и запрос пароля (su, sudo, passwd, useradd)
#include "core.h"

namespace {

// Общий префикс списка вариантов (для Tab)
String commonPrefix(const std::vector<String>& v) {
  String p = v[0];
  for (size_t i = 1; i < v.size(); i++) {
    unsigned k = 0;
    while (k < p.length() && k < v[i].length() && p.charAt(k) == v[i].charAt(k)) k++;
    p = p.substring(0, k);
  }
  return p;
}

class TerminalApp : public AppBase {
public:
  TerminalApp() : _sink(_out, 500, g_cols) {
    _out.push_back("CardputerOS 0.1 (ESP32-S3)");
    _out.push_back("Type 'help' for commands. Tab completes names.");
    _out.push_back("");
  }

  String title() const override { return "Terminal"; }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    if (k.ctrl) {
      switch (lowerc(k.ch)) {
        case 'c':
          if (_pw != PW_NONE) { _pw = PW_NONE; echoLine("^C"); }
          else echoLine(shellPrompt() + _in + "^C");
          _in = "";
          break;
        case 'l': _out.clear(); _out.push_back(""); break;
        case 'u': _in = ""; break;
        default: break;
      }
      _scroll = 0;
      return;
    }
    if (k.enter) {
      if (_pw != PW_NONE) finishPassword();
      else submit();
      return;
    }
    if (k.back) { if (_in.length()) _in.remove(_in.length() - 1); return; }
    if (_pw != PW_NONE) {                       // ввод пароля: только символы, без Tab и истории
      if (!k.fn && k.ch >= 32 && k.ch < 127 && _in.length() < 64) _in += k.ch;
      return;
    }
    if (k.tab) { complete(); return; }
    switch (navOf(k, false)) {
      case Nav::Up:    recall(-1); return;             // Fn+;  — предыдущая команда
      case Nav::Down:  recall(+1); return;             // Fn+.  — следующая команда
      case Nav::Left:  _scroll += 5; return;           // Fn+, — вверх по выводу
      case Nav::Right: _scroll = (_scroll > 5) ? _scroll - 5 : 0; return;
      default: break;
    }
    if (!k.fn && k.ch >= 32 && k.ch < 127 && _in.length() < 200) {
      _in += k.ch;
      _scroll = 0;
    }
  }

  void draw(const Box& b, bool) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    int rows = b.h / g_lineH - 1;                    // последняя строка — ввод
    if (rows < 1) return;

    int total = (int)_out.size();
    int maxScroll = (total > rows) ? total - rows : 0;
    if (_scroll > maxScroll) _scroll = maxScroll;
    int end = total - _scroll;
    int start = end - rows;
    if (start < 0) start = 0;

    tft.setTextColor(COL_GREEN, COL_BLACK);
    int y = b.y;
    for (int i = start; i < end; i++, y += g_lineH) drawText(b.x + 2, y, _out[i]);

    String line = tailText(currentPrompt() + maskedInput() + "_", b.w - 4);
    tft.setTextColor(COL_WHITE, COL_BLACK);
    drawText(b.x + 2, b.y + rows * g_lineH, line);
  }

private:
  // Выводит строку с переносом; последняя строка буфера остаётся пустой заглушкой
  void echoLine(const String& s) {
    if (!_out.back().isEmpty()) _out.push_back("");
    _sink.println(s);
  }

  // Приглашение: обычное или текст запроса пароля
  String currentPrompt() const {
    switch (_pw) {
      case PW_NONE:       return shellPrompt();
      case PW_SU:         return "Password: ";
      case PW_SUDO:       return "[sudo] password for " + _pwUser + ": ";
      case PW_PASSWD_OLD: return "Current password: ";
      case PW_NEW:        return "New password: ";
      case PW_RETYPE:     return "Retype new password: ";
    }
    return String();
  }

  // При вводе пароля вместо символов показываются звёздочки
  String maskedInput() const {
    if (_pw == PW_NONE) return _in;
    String s;
    for (unsigned i = 0; i < _in.length(); i++) s += '*';
    return s;
  }

  void submit() {
    String line = _in;
    _in = "";
    _scroll = 0;
    echoLine(shellPrompt() + line);

    String t = line;
    t.trim();
    if (t.isEmpty()) return;
    if (_hist.empty() || _hist.back() != t) _hist.push_back(t);
    if (_hist.size() > 50) _hist.erase(_hist.begin());
    _hpos = (int)_hist.size();

    if (t == "clear") { _out.clear(); _out.push_back(""); return; }
    if (t == "exit") { closeRequested = true; return; }
    if (t == "history") {
      for (size_t i = 0; i < _hist.size(); i++)
        _sink.println(String((unsigned long)(i + 1)) + "  " + _hist[i]);
      return;
    }
    shellExec(t, _sink);
    takePasswordRequest();
  }

  // Команда могла попросить пароль (su, sudo, passwd, useradd)
  void takePasswordRequest() {
    PwRequest r = shellTakePwRequest();
    if (r.kind == PW_NONE) return;
    _pw = r.kind;
    _pwUser = r.user;
    _pwCmd = r.cmd;
    _pwNew = "";
    _in = "";
  }

  // Enter в режиме пароля: проверка и переход к следующему шагу
  void finishPassword() {
    String pw = _in;
    _in = "";
    _scroll = 0;
    PwKind k = _pw;
    _pw = PW_NONE;
    switch (k) {
      case PW_SU:
        if (userCheck(_pwUser, pw)) shellSwitchUser(_pwUser, false);
        else _sink.println("su: Authentication failure");
        break;

      case PW_SUDO:
        if (userCheck(sessionUser(), pw)) {
          shellRunAsRoot(_pwCmd, _sink);
          takePasswordRequest();              // команда могла попросить ещё пароль
        } else {
          _sink.println("sudo: 1 incorrect password attempt");
        }
        break;

      case PW_PASSWD_OLD:
        if (userCheck(_pwUser, pw)) _pw = PW_NEW;
        else _sink.println("passwd: Authentication token manipulation error");
        break;

      case PW_NEW:
        if (pw.isEmpty()) { _sink.println("Password must not be empty."); break; }
        _pwNew = pw;
        _pw = PW_RETYPE;
        break;

      case PW_RETYPE:
        if (pw != _pwNew) {
          _sink.println("Sorry, passwords do not match.");
        } else if (_pwCmd == "useradd") {
          if (userAdd(_pwUser, pw)) _sink.println(String("user '") + _pwUser + "' created");
          else _sink.println("useradd: could not create user");
        } else {
          if (userSetPassword(_pwUser, pw)) _sink.println("passwd: password updated successfully");
          else _sink.println("passwd: failure");
        }
        break;

      default:
        break;
    }
  }

  void recall(int d) {
    if (_hist.empty()) return;
    int n = (int)_hist.size();
    int p = _hpos + d;
    if (p < 0) p = 0;
    if (p > n) p = n;
    _hpos = p;
    _in = (p == n) ? String("") : _hist[p];
  }

  // Tab: дополняет команду или имя файла; при нескольких вариантах печатает их
  void complete() {
    int sp = _in.lastIndexOf(' ');
    bool cmdPos = (sp < 0);
    String word = cmdPos ? _in : _in.substring(sp + 1);
    String base = cmdPos ? String() : _in.substring(0, sp + 1);

    std::vector<String> m;
    shellComplete(word, cmdPos, m);
    if (m.empty()) return;

    if (m.size() == 1) {
      _in = base + m[0] + (m[0].endsWith("/") ? "" : " ");
      return;
    }
    String cp = commonPrefix(m);
    if (cp.length() > word.length()) {
      _in = base + cp;
      return;
    }
    String list;
    for (auto& x : m) list += x + "  ";
    echoLine(list);
  }

  std::vector<String> _out;      // должен идти до _sink
  LineSink _sink;
  String _in;
  std::vector<String> _hist;
  int _hpos = 0;
  int _scroll = 0;
  PwKind _pw = PW_NONE;          // что сейчас спрашиваем
  String _pwUser;                // чей пароль (или пользователь, которого создаём/меняем)
  String _pwCmd;                 // команда для sudo или "passwd" / "useradd"
  String _pwNew;                 // первый ввод нового пароля
};

}  // namespace

void openTerminal() { wmOpen(new TerminalApp()); }
