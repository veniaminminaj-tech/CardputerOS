// Экран входа: открывается при загрузке и по командам login / logout
#include "core.h"

namespace {

class LoginApp : public AppBase {
public:
  LoginApp() : _status("Enter user name, then password.") {}

  String title() const override { return "Login"; }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    if (k.enter) {
      if (!_onPass) {
        if (!_user.isEmpty()) _onPass = true;
      } else {
        tryLogin();
      }
      return;
    }
    if (k.tab) { _onPass = !_onPass; return; }
    if (k.back) {
      String& f = _onPass ? _pass : _user;
      if (f.length()) f.remove(f.length() - 1);
      return;
    }
    if (k.ctrl || k.alt || k.fn) return;
    if (k.ch >= 32 && k.ch < 127) {
      String& f = _onPass ? _pass : _user;
      if (f.length() < 24) f += k.ch;
    }
  }

  void draw(const Box& b, bool) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    int y = b.y + 8;
    tft.setTextColor(COL_GREEN, COL_BLACK);
    drawText(b.x + 8, y, "CardputerOS login");

    y += g_lineH * 2;
    tft.setTextColor(_onPass ? COL_GRAY : COL_WHITE, COL_BLACK);
    drawText(b.x + 8, y, String(_onPass ? "  login:    " : "> login:    ") + _user + (_onPass ? "" : "_"));

    y += g_lineH * 2;
    String masked;
    for (unsigned i = 0; i < _pass.length(); i++) masked += '*';
    tft.setTextColor(_onPass ? COL_WHITE : COL_GRAY, COL_BLACK);
    drawText(b.x + 8, y, String(_onPass ? "> password: " : "  password: ") + masked + (_onPass ? "_" : ""));

    y += g_lineH * 3;
    tft.setTextColor(COL_YELLOW, COL_BLACK);
    drawText(b.x + 8, y, _status);

    y += g_lineH * 2;
    tft.setTextColor(COL_GRAY, COL_BLACK);
    drawText(b.x + 8, y, "Tab: switch field   Enter: next");
  }

private:
  void tryLogin() {
    if (userCheck(_user, _pass)) {
      shellSwitchUser(_user, true);
      sessionSetLoggedIn(true);
      closeRequested = true;
      return;
    }
    _status = "Login incorrect";
    _pass = "";
    _onPass = false;
  }

  String _user, _pass, _status;
  bool _onPass = false;
};

}  // namespace

void openLogin() { wmOpen(new LoginApp()); }
