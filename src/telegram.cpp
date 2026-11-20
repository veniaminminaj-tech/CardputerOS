// Telegram Bot API: getUpdates і sendMessage через HTTPS GET, вікно чату, команда tg
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <stdlib.h>
#include "core.h"

namespace {

const char* const kConf = "/root/telegram.conf";   // токен тут, тож читати можна лише як root
const char* const kHost = "api.telegram.org";
const uint32_t kPollMs = 5000;
const size_t kInboxMax = 40;

struct TgMsg {
  String chat;     // chat id зберігаємо рядком: ідентифікатори можуть перевищувати int32
  String from;
  String text;
};

String g_token;
String g_chat;                  // чат за замовчуванням, куди йдуть відповіді
long g_offset = 0;              // наступний update_id
std::vector<TgMsg> g_inbox;
uint32_t g_lastPoll = 0;
int g_unread = 0;
String g_status = "tg: not configured";

// ---------- Конфігурація ----------
void parseConfLine(const String& l) {
  int eq = l.indexOf('=');
  if (eq <= 0) return;
  String k = l.substring(0, eq);
  String v = l.substring(eq + 1);
  if (k == "token") g_token = v;
  else if (k == "chat") g_chat = v;
}

void loadConf() {
  g_token = "";
  g_chat = "";
  File f = SD.open(kConf, FILE_READ);
  if (!f) return;
  String line;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\n' || c == '\r') { parseConfLine(line); line = ""; }
    else line += c;
  }
  parseConfLine(line);
  f.close();
}

bool saveConf() {
  SD.mkdir("/root");
  SD.remove(kConf);
  File f = SD.open(kConf, FILE_APPEND);
  if (!f) return false;
  f.println("token=" + g_token);
  f.println("chat=" + g_chat);
  f.close();
  return true;
}

// ---------- Утиліти ----------
String urlEncode(const String& s) {
  static const char* hx = "0123456789ABCDEF";
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s.charAt(i);
    bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      o += (char)c;
    } else {
      o += '%';
      o += hx[c >> 4];
      o += hx[c & 0x0F];
    }
  }
  return o;
}

String joinFrom(const std::vector<String>& a, size_t from) {
  String s;
  for (size_t i = from; i < a.size(); i++) {
    if (i > from) s += ' ';
    s += a[i];
  }
  return s;
}

// Розгортає chunked-кодування HTTP/1.1
String dechunk(const String& r) {
  String out;
  unsigned p = 0;
  while (p < r.length()) {
    int e = r.indexOf("\r\n", p);
    if (e < 0) break;
    long n = strtol(r.substring(p, e).c_str(), nullptr, 16);
    if (n <= 0) break;
    p = (unsigned)e + 2;
    out += r.substring(p, p + n);
    p += n + 2;
  }
  return out;
}

// ---------- Мінімальний розбір JSON ----------
// Позиція одразу після `"key":` або -1
int findKey(const String& s, const String& key, int from) {
  String k = "\"" + key + "\":";
  int i = s.indexOf(k, (unsigned)(from < 0 ? 0 : from));
  return (i < 0) ? -1 : i + (int)k.length();
}

// Число або chat id як рядок
String readToken(const String& s, int p) {
  String t;
  if (p < 0) return t;
  unsigned i = (unsigned)p;
  while (i < s.length() && s.charAt(i) == ' ') i++;
  while (i < s.length() && ((s.charAt(i) >= '0' && s.charAt(i) <= '9') || s.charAt(i) == '-')) {
    t += s.charAt(i);
    i++;
  }
  return t;
}

// Рядок у лапках; переноси рядків і табуляції замінюються пробілом, \uXXXX — знаком ?
String readStr(const String& s, int p) {
  String out;
  if (p < 0) return out;
  unsigned i = (unsigned)p;
  while (i < s.length() && s.charAt(i) == ' ') i++;
  if (i >= s.length() || s.charAt(i) != '"') return out;
  i++;
  while (i < s.length() && s.charAt(i) != '"') {
    char c = s.charAt(i);
    if (c == '\\' && i + 1 < s.length()) {
      char d = s.charAt(++i);
      if (d == 'n' || d == 't') out += ' ';
      else if (d == 'u') { out += '?'; i += 4; }
      else out += d;
    } else {
      out += c;
    }
    i++;
  }
  return out;
}

// ---------- HTTPS ----------
// GET до Bot API. Повертає HTTP-код (або від'ємний код помилки), тіло — у body.
int apiGet(const String& method, const String& query, String& body) {
  body = "";
  if (!wifiUp()) return -1;
  WiFiClientSecure tls;
  tls.setInsecure();                          // прототип: сертифікат не перевіряється
  tls.setTimeout(10);
  if (!tls.connect(kHost, 443)) return -2;

  String path = "/bot" + g_token + "/" + method;
  if (!query.isEmpty()) path += "?" + query;
  tls.print("GET " + path + " HTTP/1.1\r\nHost: " + kHost +
            "\r\nUser-Agent: CardputerOS/0.1\r\nConnection: close\r\n\r\n");

  String head;
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    if (tls.available()) {
      head += (char)tls.read();
      if (head.endsWith("\r\n\r\n")) break;
      t0 = millis();
    } else if (!tls.connected()) {
      break;
    } else {
      delay(1);
    }
  }

  int code = -3;
  if (head.startsWith("HTTP/")) {
    int sp = head.indexOf(' ');
    code = head.substring(sp + 1, sp + 4).toInt();
  }
  String lower = head;
  lower.toLowerCase();
  bool chunked = lower.indexOf("transfer-encoding: chunked") >= 0;

  String raw;
  t0 = millis();
  while (raw.length() < 24000 && millis() - t0 < 10000) {
    if (tls.available()) {
      raw += (char)tls.read();
      t0 = millis();
    } else if (!tls.connected()) {
      break;
    } else {
      delay(1);
    }
  }
  tls.stop();
  body = chunked ? dechunk(raw) : raw;
  return code;
}

// ---------- Вхідні повідомлення ----------
void pushInbox(const TgMsg& m) {
  g_inbox.push_back(m);
  while (g_inbox.size() > kInboxMax) g_inbox.erase(g_inbox.begin());
}

// Розбирає відповідь getUpdates; повертає кількість нових текстових повідомлень, -1 при помилці
int parseUpdates(const String& body) {
  if (body.indexOf("\"ok\":true") < 0) return -1;
  int added = 0;
  int u = findKey(body, "update_id", 0);
  while (u >= 0) {
    int next = findKey(body, "update_id", u);
    unsigned endPos = (next < 0) ? body.length() : (unsigned)next;
    String seg = body.substring((unsigned)u, endPos);

    long uid = readToken(body, u).toInt();
    if (uid + 1 > g_offset) g_offset = uid + 1;

    int tp = findKey(seg, "text", 0);
    if (tp >= 0) {
      int cp = seg.indexOf("\"chat\":{");
      int ip = (cp >= 0) ? findKey(seg, "id", cp) : -1;
      String chat = readToken(seg, ip);
      int fp = findKey(seg, "first_name", 0);
      String from = (fp >= 0) ? readStr(seg, fp) : String("user");
      if (!chat.isEmpty()) {
        TgMsg m;
        m.chat = chat;
        m.from = from;
        m.text = readStr(seg, tp);
        pushInbox(m);
        if (g_chat.isEmpty()) {           // первий чат, який написав боту, стає типовим
          g_chat = chat;
          saveConf();
        }
        added++;
      }
    }
    u = next;
  }
  return added;
}

void pollOnce() {
  String q = "timeout=0&limit=10";
  if (g_offset > 0) q += "&offset=" + String(g_offset);
  String body;
  int code = apiGet("getUpdates", q, body);
  if (code != 200) {
    g_status = "tg: poll error " + String(code);
    return;
  }
  int n = parseUpdates(body);
  if (n < 0) {
    g_status = "tg: bad response";
    return;
  }
  g_unread += n;
  g_status = "tg: online, unread " + String(g_unread);
}

bool setToken(const String& t) {
  g_token = t;
  g_offset = 0;
  g_lastPoll = 0;
  g_status = "tg: ready";
  return saveConf();
}

// ---------- Вікно чату ----------
class TgApp : public AppBase {
public:
  TgApp() { g_unread = 0; }

  String title() const override { return "Telegram"; }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    if (k.enter) { sendInput(); return; }
    if (k.back) { if (_in.length()) _in.remove(_in.length() - 1); return; }
    switch (navOf(k, false)) {
      case Nav::Left:  _scroll += 3; return;                 // Fn+,
      case Nav::Right: _scroll = (_scroll > 3) ? _scroll - 3 : 0; return;  // Fn+/
      default: break;
    }
    if (!k.fn && !k.ctrl && !k.alt && k.ch >= 32 && k.ch < 127 && _in.length() < 200) _in += k.ch;
  }

  void draw(const Box& b, bool) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    int rows = b.h / g_lineH - 2;
    if (rows < 1) return;

    int total = (int)g_inbox.size();
    int maxScroll = (total > rows) ? total - rows : 0;
    if (_scroll > maxScroll) _scroll = maxScroll;
    int end = total - _scroll;
    int start = end - rows;
    if (start < 0) start = 0;

    int y = b.y;
    for (int i = start; i < end; i++, y += g_lineH) {
      const TgMsg& m = g_inbox[i];
      tft.setTextColor(m.from == "me" ? COL_CYAN : COL_GREEN, COL_BLACK);
      drawText(b.x + 2, y, fitText(m.from + ": " + m.text, b.w - 4));
    }

    int sy = b.y + b.h - 2 * g_lineH;
    tft.fillRect(b.x, sy, b.w, g_lineH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);
    drawText(b.x + 2, sy, fitText(g_status, b.w - 4));
    tft.setTextColor(COL_WHITE, COL_BLACK);
    drawText(b.x + 2, sy + g_lineH, tailText("> " + _in + "_", b.w - 4));
  }

private:
  void sendInput() {
    if (_in.isEmpty()) return;
    String text = _in;
    _in = "";
    _scroll = 0;
    String err;
    if (tgSend(g_chat, text, err)) {
      TgMsg m;
      m.chat = g_chat;
      m.from = "me";
      m.text = text;
      pushInbox(m);
      g_status = "tg: sent";
    } else {
      g_status = "tg: " + err;
    }
  }

  String _in;
  int _scroll = 0;
};

}  // namespace

// ---------- Публічний API ----------
void tgInit() {
  loadConf();
  g_status = g_token.isEmpty() ? String("tg: no token, run: tg token BOT_TOKEN") : String("tg: ready");
}

// Опитування раз на kPollMs, якщо є токен і Wi-Fi
void tgTick() {
  if (g_token.isEmpty() || !wifiUp()) return;
  uint32_t now = millis();
  if (g_lastPoll && now - g_lastPoll < kPollMs) return;
  g_lastPoll = now;
  pollOnce();
}

bool tgSend(const String& chat, const String& text, String& err) {
  if (g_token.isEmpty()) { err = "no token"; return false; }
  if (!wifiUp()) { err = "no Wi-Fi"; return false; }
  if (chat.isEmpty()) { err = "no chat yet, write to the bot first"; return false; }
  String t = (text.length() > 300) ? text.substring(0, 300) : text;
  String body;
  int code = apiGet("sendMessage", "chat_id=" + chat + "&text=" + urlEncode(t), body);
  if (code == 200 && body.indexOf("\"ok\":true") >= 0) return true;
  err = "HTTP " + String(code);
  return false;
}

// tg token|chat|send|to|inbox|poll|status|open
int tgCommand(const std::vector<String>& a, Print& out) {
  String sub = (a.size() > 1) ? a[1] : String();

  if (sub == "token") {
    if (!requireRoot(out, "tg token")) return 1;
    if (a.size() < 3) { out.println("usage: tg token BOT_TOKEN"); return 2; }
    if (!setToken(a[2])) { out.println("tg: cannot write /root/telegram.conf"); return 1; }
    out.println("token saved, polling started");
    return 0;
  }
  if (sub == "chat") {
    if (!requireRoot(out, "tg chat")) return 1;
    if (a.size() < 3) { out.println("usage: tg chat CHAT_ID"); return 2; }
    g_chat = a[2];
    saveConf();
    out.println("default chat: " + g_chat);
    return 0;
  }
  if (sub == "send") {
    if (a.size() < 3) { out.println("usage: tg send TEXT"); return 2; }
    String text = joinFrom(a, 2);
    String err;
    if (!tgSend(g_chat, text, err)) { out.println("tg: " + err); return 1; }
    TgMsg m;
    m.chat = g_chat;
    m.from = "me";
    m.text = text;
    pushInbox(m);
    out.println("sent");
    return 0;
  }
  if (sub == "to") {
    if (a.size() < 4) { out.println("usage: tg to CHAT_ID TEXT"); return 2; }
    String text = joinFrom(a, 3);
    String err;
    if (!tgSend(a[2], text, err)) { out.println("tg: " + err); return 1; }
    out.println("sent to " + a[2]);
    return 0;
  }
  if (sub == "inbox") {
    int total = (int)g_inbox.size();
    int from = total > 10 ? total - 10 : 0;
    if (total == 0) out.println("(no messages yet)");
    for (int i = from; i < total; i++) out.println("[" + g_inbox[i].from + "] " + g_inbox[i].text);
    g_unread = 0;
    return 0;
  }
  if (sub == "poll") {
    if (g_token.isEmpty()) { out.println("tg: no token"); return 1; }
    if (!wifiUp()) { out.println("tg: no Wi-Fi"); return 1; }
    g_lastPoll = millis();
    pollOnce();
    out.println(g_status);
    return 0;
  }
  if (sub == "status") {
    out.println(g_status);
    out.println("chat: " + (g_chat.isEmpty() ? String("none") : g_chat));
    out.println("messages: " + String((unsigned long)g_inbox.size()));
    return 0;
  }
  if (sub == "open") {
    openTelegram();
    return 0;
  }
  out.println("usage: tg token BOT_TOKEN | chat ID | send TEXT | to ID TEXT");
  out.println("       tg inbox | poll | status | open");
  return 1;
}

void openTelegram() { wmOpen(new TgApp()); }
