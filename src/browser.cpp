// Browser: HTTP/HTTPS GET, разбор HTML в слова, навигация по ссылкам
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <utility>
#include "core.h"

namespace {

constexpr size_t MAX_BODY  = 24000;   // байт HTML на страницу
constexpr size_t MAX_WORDS = 2000;
constexpr size_t MAX_LINES = 400;
constexpr size_t MAX_LINKS = 80;
constexpr size_t MAX_HIST  = 30;
constexpr int    MAX_REDIR = 4;

struct Word {
  String text;
  uint16_t color = COL_WHITE;
  uint16_t bg = COL_BLACK;
  int link = -1;        // индекс ссылки или -1
  bool brk = false;     // начать с новой строки
  bool glue = false;    // без пробела перед словом
  int col = 0;          // позиция в строке (символы), задаётся в layout
};

struct Line {
  size_t first, count;
};

struct Page {
  String title;
  std::vector<Word> words;
  std::vector<Line> lines;
  std::vector<String> links;
  std::vector<int> linkLine;   // первая строка каждой ссылки
};

// ---------- URL ----------
struct Url {
  bool tls = false;
  uint16_t port = 80;
  String host;
  String path = "/";
  String origin;
};

bool parseUrl(const String& in, Url& u) {
  String s = in;
  if (s.startsWith("https://")) { u.tls = true; u.port = 443; s = s.substring(8); }
  else if (s.startsWith("http://")) { u.tls = false; u.port = 80; s = s.substring(7); }
  else return false;

  int hash = s.indexOf('#');
  if (hash >= 0) s = s.substring(0, hash);

  int slash = s.indexOf('/');
  String hostPort = (slash < 0) ? s : s.substring(0, slash);
  u.path = (slash < 0) ? String("/") : s.substring(slash);

  int colon = hostPort.indexOf(':');
  if (colon >= 0) {
    u.host = hostPort.substring(0, colon);
    u.port = (uint16_t)hostPort.substring(colon + 1).toInt();
  } else {
    u.host = hostPort;
  }
  if (u.host.isEmpty()) return false;

  u.origin = String(u.tls ? "https://" : "http://") + u.host;
  if ((u.tls && u.port != 443) || (!u.tls && u.port != 80)) u.origin += ":" + String(u.port);
  return true;
}

// Превращает href в абсолютный URL относительно base
String resolveUrl(const String& base, const String& href) {
  String h = href;
  h.trim();
  if (h.startsWith("http://") || h.startsWith("https://")) return h;
  Url b;
  if (!parseUrl(base, b)) return h;
  if (h.startsWith("//")) return String(b.tls ? "https:" : "http:") + h;
  if (h.startsWith("?")) {
    String path = b.path;
    int q = path.indexOf('?');
    if (q >= 0) path = path.substring(0, q);
    return b.origin + path + h;
  }
  if (h.startsWith("/")) return b.origin + h;

  String dir = b.path;
  int q = dir.indexOf('?');
  if (q >= 0) dir = dir.substring(0, q);
  int slash = dir.lastIndexOf('/');
  dir = (slash >= 0) ? dir.substring(0, slash + 1) : String("/");
  String joined = dir + h;
  // Normalize simple ./ and ../ path segments.
  std::vector<String> parts;
  int pos = 0;
  while (pos <= (int)joined.length()) {
    int end = joined.indexOf('/', pos);
    if (end < 0) end = joined.length();
    String part = joined.substring(pos, end);
    if (part == "..") { if (!parts.empty()) parts.pop_back(); }
    else if (part != "." && !part.isEmpty()) parts.push_back(part);
    pos = end + 1;
    if (end >= (int)joined.length()) break;
  }
  String normalized = "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) normalized += "/";
    normalized += parts[i];
  }
  return b.origin + normalized;
}

String normalizeInput(const String& in) {
  String s = in;
  s.trim();
  if (s.isEmpty()) return s;
  if (!s.startsWith("http://") && !s.startsWith("https://")) s = "https://" + s;
  return s;
}

// ---------- Сеть ----------
int readByte(WiFiClient* c) {
  uint32_t t = millis();
  while (!c->available()) {
    if (!c->connected() || millis() - t > 10000) return -1;
    delay(1);
  }
  return c->read();
}

String readLine(WiFiClient* c) {
  String s;
  for (;;) {
    int b = readByte(c);
    if (b < 0 || b == '\n') break;
    if (s.length() < 256) s += (char)b;
  }
  return s;
}

void readBody(WiFiClient* c, bool chunked, String& body) {
  if (chunked) {
    while (body.length() < MAX_BODY) {
      String sz = readLine(c);
      long n = strtol(sz.c_str(), nullptr, 16);
      if (n <= 0) break;
      for (long i = 0; i < n; i++) {
        int b = readByte(c);
        if (b < 0) return;
        if (body.length() < MAX_BODY) body += (char)b;
      }
      readLine(c);                              // CRLF после чанка
    }
    return;
  }
  uint8_t buf[256];
  uint32_t last = millis();
  while (body.length() < MAX_BODY) {
    int avail = c->available();
    if (avail > 0) {
      int want = (avail < (int)sizeof(buf)) ? avail : (int)sizeof(buf);
      int n = c->read(buf, want);
      for (int i = 0; i < n && body.length() < MAX_BODY; i++) body += (char)buf[i];
      last = millis();
    } else {
      if (!c->connected() || millis() - last > 10000) break;
      delay(1);
    }
  }
}

// Один GET без редиректов. Возвращает HTTP-код или -1 при ошибке соединения.
int fetchOnce(const Url& u, String& location, String& body) {
  WiFiClientSecure tls;
  WiFiClient plain;
  WiFiClient* c = nullptr;
  if (u.tls) {
    tls.setInsecure();                          // прототип: сертификат не проверяется
    tls.setTimeout(10);
    if (!tls.connect(u.host.c_str(), u.port)) return -1;
    c = &tls;
  } else {
    plain.setTimeout(10);
    if (!plain.connect(u.host.c_str(), u.port)) return -1;
    c = &plain;
  }

  // Request a lightweight mobile page and disable compression: this firmware
  // does not include a gzip/Brotli decoder.
  String req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
               "\r\nUser-Agent: Mozilla/5.0 (Linux; Android 10; CardputerOS) AppleWebKit/537.36 Chrome/120.0 Mobile Safari/537.36\r\n"
               "Accept: text/html,application/xhtml+xml;q=0.9,*/*;q=0.5\r\n"
               "Accept-Language: en-US,en;q=0.8\r\nAccept-Encoding: identity\r\n"
               "Connection: close\r\n\r\n";
  c->print(req);

  String head;
  while (head.length() < 4096) {
    int b = readByte(c);
    if (b < 0) break;
    head += (char)b;
    if (head.endsWith("\r\n\r\n")) break;
  }

  String lower = head;
  lower.toLowerCase();
  int code = -1;
  if (head.startsWith("HTTP/")) {
    int sp = head.indexOf(' ');
    code = head.substring(sp + 1, sp + 4).toInt();
  }
  int li = lower.indexOf("\nlocation:");
  if (li >= 0) {
    int e = head.indexOf('\r', li);
    unsigned endPos = (e < 0) ? head.length() : (unsigned)e;
    location = head.substring(li + 10, endPos);
    location.trim();
  }
  bool chunked = lower.indexOf("transfer-encoding: chunked") >= 0;

  body = "";
  body.reserve(MAX_BODY);
  readBody(c, chunked, body);
  c->stop();
  return code;
}

// GET с переходами по редиректам
bool httpGet(String url, String& finalUrl, String& body, int& code, String& err) {
  for (int hop = 0; hop <= MAX_REDIR; hop++) {
    Url u;
    if (!parseUrl(url, u)) { err = "bad url"; return false; }
    String loc;
    code = fetchOnce(u, loc, body);
    if (code < 0) { err = "connect failed"; return false; }
    if (code >= 300 && code < 400 && !loc.isEmpty()) {
      url = resolveUrl(url, loc);
      continue;
    }
    finalUrl = url;
    return true;
  }
  err = "too many redirects";
  return false;
}

// ---------- HTML helpers ----------
bool isWs(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
bool isTagChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

String attrValue(const String& attrs, const char* key) {
  String lower = attrs;
  lower.toLowerCase();
  String k = String(key) + "=";
  int i = lower.indexOf(k);
  if (i < 0) return String();
  unsigned pos = (unsigned)i + k.length();
  if (pos >= attrs.length()) return String();

  String v;
  char q = attrs.charAt(pos);
  if (q == '"' || q == '\'') {
    int e = attrs.indexOf(q, pos + 1);
    unsigned endPos = (e < 0) ? attrs.length() : (unsigned)e;
    v = attrs.substring(pos + 1, endPos);
  } else {
    unsigned e = pos;
    while (e < attrs.length() && attrs.charAt(e) != ' ') e++;
    v = attrs.substring(pos, e);
  }
  v.trim();
  return v;
}

bool isNavigable(const String& href) {
  String l = href;
  l.toLowerCase();
  return !(l.startsWith("#") || l.startsWith("javascript:") || l.startsWith("mailto:") ||
           l.startsWith("tel:") || l.startsWith("data:"));
}

String decodeEntities(const String& s) {
  String out;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c != '&') { out += c; continue; }
    int semi = s.indexOf(';', i);
    if (semi < 0 || semi - (int)i > 8) { out += c; continue; }
    String ent = s.substring(i + 1, semi);
    if (ent == "amp") out += '&';
    else if (ent == "lt") out += '<';
    else if (ent == "gt") out += '>';
    else if (ent == "quot") out += '"';
    else if (ent == "apos" || ent == "#39") out += '\'';
    else if (ent == "nbsp") out += ' ';
    else if (ent.startsWith("#")) {
      int code = ent.substring(1).toInt();
      out += (code >= 32 && code < 127) ? (char)code : '?';
    } else {
      out += c;
      continue;
    }
    i = (unsigned)semi;
  }
  return out;
}

uint16_t htmlColor(String value, uint16_t fallback) {
  value.trim();
  value.toLowerCase();
  if (value.startsWith("#")) {
    String h = value.substring(1);
    if (h.length() == 3) {
      int r = strtol(h.substring(0, 1).c_str(), nullptr, 16) * 17;
      int g = strtol(h.substring(1, 2).c_str(), nullptr, 16) * 17;
      int b = strtol(h.substring(2, 3).c_str(), nullptr, 16) * 17;
      return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    if (h.length() == 6) {
      long rgb = strtol(h.c_str(), nullptr, 16);
      return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
    }
  }
  if (value == "black") return COL_BLACK;
  if (value == "white") return COL_WHITE;
  if (value == "red") return 0xF800;
  if (value == "green") return COL_GREEN;
  if (value == "blue") return 0x001F;
  if (value == "yellow") return COL_YELLOW;
  if (value == "cyan" || value == "aqua") return COL_CYAN;
  if (value == "gray" || value == "grey") return COL_GRAY;
  return fallback;
}

void applyInlineStyle(const String& style, uint16_t& fg, uint16_t& bg) {
  int pos = 0;
  while (pos < (int)style.length()) {
    int end = style.indexOf(';', pos);
    if (end < 0) end = style.length();
    String item = style.substring(pos, end);
    int colon = item.indexOf(':');
    if (colon > 0) {
      String key = item.substring(0, colon);
      String value = item.substring(colon + 1);
      key.trim(); value.trim(); key.toLowerCase();
      if (key == "color") fg = htmlColor(value, fg);
      else if (key == "background" || key == "background-color") bg = htmlColor(value, bg);
    }
    pos = end + 1;
  }
}


// Tiny CSS subset: tag, .class and #id selectors; color/background only.
struct CssRule { String selector; uint16_t fg = COL_WHITE; uint16_t bg = COL_BLACK; bool hasFg = false; bool hasBg = false; };
using CssRules = std::vector<CssRule>;

void parseCssRules(const String& css, CssRules& rules) {
  int pos = 0;
  while (pos < (int)css.length() && rules.size() < 48) {
    int open = css.indexOf('{', pos);
    if (open < 0) break;
    int close = css.indexOf('}', open + 1);
    if (close < 0) break;
    String selectors = css.substring(pos, open);
    String declarations = css.substring(open + 1, close);
    int sp = 0;
    while (sp < (int)selectors.length() && rules.size() < 48) {
      int comma = selectors.indexOf(',', sp);
      if (comma < 0) comma = selectors.length();
      String sel = selectors.substring(sp, comma); sel.trim(); sel.toLowerCase();
      if (!sel.isEmpty() && sel.indexOf(' ') < 0 && sel.indexOf(':') < 0) {
        CssRule r; r.selector = sel;
        int dp = 0;
        while (dp < (int)declarations.length()) {
          int de = declarations.indexOf(';', dp); if (de < 0) de = declarations.length();
          String item = declarations.substring(dp, de); int c = item.indexOf(':');
          if (c > 0) {
            String key=item.substring(0,c), val=item.substring(c+1); key.trim(); val.trim(); key.toLowerCase();
            if (key == "color") { r.fg=htmlColor(val,COL_WHITE); r.hasFg=true; }
            else if (key == "background" || key == "background-color") { r.bg=htmlColor(val,COL_BLACK); r.hasBg=true; }
          }
          dp=de+1;
        }
        if (r.hasFg || r.hasBg) rules.push_back(r);
      }
      sp = comma + 1;
    }
    pos = close + 1;
  }
}

bool cssMatches(const String& selector, const String& tag, const String& attrs) {
  if (selector.startsWith(".")) {
    String classes = " " + attrValue(attrs, "class") + " "; classes.toLowerCase();
    return classes.indexOf(" " + selector.substring(1) + " ") >= 0;
  }
  if (selector.startsWith("#")) {
    String id = attrValue(attrs, "id"); id.toLowerCase();
    return id == selector.substring(1);
  }
  return selector == tag;
}

void applyCssRules(const CssRules& rules, const String& tag, const String& attrs,
                   uint16_t& fg, uint16_t& bg) {
  for (const CssRule& r : rules) if (cssMatches(r.selector, tag, attrs)) {
    if (r.hasFg) fg = r.fg;
    if (r.hasBg) bg = r.bg;
  }
}

String collectEmbeddedCss(const String& html) {
  String out; int pos = 0;
  while ((pos = html.indexOf("<style", pos)) >= 0 && out.length() < 6000) {
    int start = html.indexOf('>', pos); if (start < 0) break;
    int end = html.indexOf("</style", start + 1); if (end < 0) break;
    out += html.substring(start + 1, end); out += '\n'; pos = end + 7;
  }
  return out;
}

// Tiny JavaScript subset: var/let/const string values, + concatenation,
// document.write(...), and document.title = ... . No DOM, events or eval.
String jsValue(String expr, const std::vector<std::pair<String,String>>& vars) {
  expr.trim(); String out; bool quoted=false; char quote=0; String token;
  auto flush=[&]() { token.trim(); if (token.isEmpty()) return; bool found=false;
    for (const auto& v:vars) if (v.first==token) { out+=v.second; found=true; break; }
    if (!found) out+=token; token="";
  };
  for (unsigned i=0;i<expr.length();i++) { char c=expr.charAt(i);
    if (quoted) { if (c==quote) quoted=false; else if (c=='\\' && i+1<expr.length()) out+=expr.charAt(++i); else out+=c; }
    else if (c=='\\'' || c=='"' || c=='`') { flush(); quoted=true; quote=c; }
    else if (c=='+') flush(); else if (c!=' ' && c!='\t' && c!='\r' && c!='\n') token+=c;
  }
  flush(); return out;
}

String runTinyJavaScript(const String& html) {
  String output; std::vector<std::pair<String,String>> vars; int pos=0; unsigned budget=0;
  while ((pos=html.indexOf("<script",pos))>=0 && budget++<12) {
    int start=html.indexOf('>',pos); if(start<0) break;
    int end=html.indexOf("</script",start+1); if(end<0) break;
    String code=html.substring(start+1,end); int cp=0;
    while(cp<(int)code.length() && budget++<100) {
      int semi=code.indexOf(';',cp); if(semi<0) semi=code.length();
      String st=code.substring(cp,semi); st.trim();
      if(st.startsWith("//")) { cp=semi+1; continue; }
      int eq=st.indexOf('=');
      if(st.startsWith("var ")||st.startsWith("let ")||st.startsWith("const ")) {
        int space=st.indexOf(' '); String rest=st.substring(space+1); int e=rest.indexOf('=');
        if(e>0) { String name=rest.substring(0,e); name.trim(); String val=jsValue(rest.substring(e+1),vars);
          bool replaced=false; for(auto& v:vars) if(v.first==name){v.second=val;replaced=true;}
          if(!replaced && vars.size()<12) vars.push_back({name,val}); }
      } else if(st.startsWith("document.write(")) {
        int a=st.indexOf('('), b=st.lastIndexOf(')'); if(b>a) output+=jsValue(st.substring(a+1,b),vars);
      } else if(st.startsWith("document.title") && eq>0) {
        // Title is handled by the HTML title tag; append a title node for this subset.
        output += "<title>" + jsValue(st.substring(eq+1),vars) + "</title>";
      }
      cp=semi+1;
    }
    pos=end+8;
  }
  return output;
}

// Lightweight HTML renderer: semantic blocks, links, entities and basic inline CSS.
class HtmlParser {
public:
  explicit HtmlParser(Page& p, const CssRules& css) : _p(p), _css(css) {}

  void run(const String& html) {
    unsigned n = html.length(), i = 0;
    while (i < n) {
      if (html.charAt(i) == '<') {
        if (html.startsWith("<!--", i)) {
          int e = html.indexOf("-->", i);
          i = (e < 0) ? n : (unsigned)e + 3;
          continue;
        }
        int e = html.indexOf('>', i);
        if (e < 0) break;
        tag(html.substring(i + 1, e));
        i = (unsigned)e + 1;
      } else {
        int lt = html.indexOf('<', i);
        unsigned end = (lt < 0) ? n : (unsigned)lt;
        text(html.substring(i, end));
        i = end;
      }
    }
  }

  void finish() { layout(); }

private:
  void tag(String t) {
    bool closing = t.startsWith("/");
    if (closing) t = t.substring(1);
    unsigned k = 0;
    while (k < t.length() && isTagChar(t.charAt(k))) k++;
    String name = t.substring(0, k);
    name.toLowerCase();
    String attrs = t.substring(k);

    if (closing) {
      _styleColor = COL_WHITE;
      _styleBg = COL_BLACK;
    } else {
      applyCssRules(_css, name, attrs, _styleColor, _styleBg);
      applyInlineStyle(attrValue(attrs, "style"), _styleColor, _styleBg);
      if (name == "font") _styleColor = htmlColor(attrValue(attrs, "color"), _styleColor);
    }

    if (_skip) {
      if (closing && name == _skipName) _skip = false;
      return;
    }
    if (!closing && (name == "script" || name == "style" || name == "noscript" ||
                     name == "svg" || name == "template")) {
      _skip = true;
      _skipName = name;
      return;
    }
    if (name == "title") { if (!closing) _p.title = ""; _inTitle = !closing; return; }

    if (name == "a") {
      _link = -1;
      if (!closing) {
        String href = attrValue(attrs, "href");
        if (!href.isEmpty() && isNavigable(href) && _p.links.size() < MAX_LINKS) {
          _link = (int)_p.links.size();
          _p.links.push_back(href);
        }
      }
      return;
    }

    if (name == "h1" || name == "h2" || name == "h3" || name == "h4") {
      if (closing) {
        _heading = 0;
        blank();
      } else {
        blank();
        _heading = (name == "h1") ? COL_YELLOW : (name == "h2") ? COL_ORANGE : COL_GREEN;
        pendBreak();
      }
      return;
    }

    if (name == "br" || name == "tr" || name == "dt" || name == "dd") { pendBreak(); return; }
    if (name == "li") { pendBreak(); if (!closing) add("*", COL_GRAY); return; }
    if (name == "hr") { pendBreak(); add("----------------", COL_GRAY); pendBreak(); return; }
    if (name == "img") {
      String alt = attrValue(attrs, "alt");
      if (!alt.isEmpty()) add("[" + alt + "]", COL_GRAY);
      return;
    }
    if (name == "p" || name == "div" || name == "section" || name == "article" ||
        name == "header" || name == "footer" || name == "nav" || name == "main" ||
        name == "ul" || name == "ol" || name == "table" || name == "blockquote" ||
        name == "pre" || name == "form") {
      blank();
    }
  }

  void text(const String& raw) {
    if (_skip || raw.isEmpty()) return;
    String s = decodeEntities(raw);
    if (_inTitle) {
      for (unsigned k = 0; k < s.length() && _p.title.length() < 80; k++) {
        char c = s.charAt(k);
        _p.title += isWs(c) ? ' ' : c;
      }
      return;
    }
    if (s.isEmpty()) return;

    bool glue = _endNonSpace && !isWs(s.charAt(0));
    _endNonSpace = !isWs(s.charAt(s.length() - 1));

    String cur;
    bool first = true;
    for (unsigned k = 0; k <= s.length(); k++) {
      char c = (k < s.length()) ? s.charAt(k) : ' ';
      if (isWs(c)) {
        if (cur.length()) {
          add(cur, curColor(), glue && first);
          cur = "";
          first = false;
        }
      } else {
        cur += c;
      }
    }
  }

  void add(const String& t, uint16_t color, bool glue = false) {
    if (_p.words.size() >= MAX_WORDS) return;
    Word w;
    w.text = (t.length() > 40) ? t.substring(0, 40) : t;
    w.color = (_link >= 0) ? COL_CYAN : (_styleColor != COL_WHITE ? _styleColor : color);
    w.bg = _styleBg;
    w.link = _link;
    w.brk = _pendBrk;
    w.glue = glue;
    _pendBrk = false;
    _p.words.push_back(w);
  }

  void pendBreak() { _pendBrk = true; }

  // Пустая строка между блоками
  void blank() {
    if (!_p.words.empty() && !_p.words.back().text.isEmpty()) {
      pendBreak();
      add("", COL_WHITE);
    }
    pendBreak();
  }

  uint16_t curColor() const {
    if (_link >= 0) return COL_CYAN;
    return _heading ? _heading : COL_WHITE;
  }

  void layout() {
    _p.lines.clear();
    _p.linkLine.clear();
    size_t n = _p.words.size();
    if (n == 0) return;

    int cols = g_cols;
    size_t lineStart = 0;
    int col = 0;
    for (size_t i = 0; i < n; i++) {
      Word& w = _p.words[i];
      int len = (int)w.text.length();
      int sep = (i == 0 || w.brk || w.glue) ? 0 : 1;
      bool brk = (i == 0) || w.brk || (col + sep + len > cols);
      if (brk) {
        if (i > 0) pushLine(lineStart, i);
        lineStart = i;
        col = 0;
        w.col = 0;
      } else {
        w.col = col + sep;
      }
      col = w.col + len;
    }
    pushLine(lineStart, n);

    _p.linkLine.assign(_p.links.size(), -1);
    for (size_t li = 0; li < _p.lines.size(); li++) {
      const Line& L = _p.lines[li];
      for (size_t j = L.first; j < L.first + L.count; j++) {
        int lk = _p.words[j].link;
        if (lk >= 0 && (size_t)lk < _p.linkLine.size() && _p.linkLine[lk] < 0) {
          _p.linkLine[lk] = (int)li;
        }
      }
    }
  }

  void pushLine(size_t first, size_t end) {
    if (end > first && _p.lines.size() < MAX_LINES) {
      _p.lines.push_back(Line{first, end - first});
    }
  }

  Page& _p;
  const CssRules& _css;
  bool _skip = false;
  String _skipName;
  bool _inTitle = false;
  int _link = -1;
  uint16_t _heading = 0;
  bool _pendBrk = false;
  bool _endNonSpace = false;
  uint16_t _styleColor = COL_WHITE;
  uint16_t _styleBg = COL_BLACK;
};

// ---------- Приложение ----------
class BrowserApp : public AppBase {
public:
  explicit BrowserApp(const String& url) {
    _status = "Type a URL and press Enter. Wi-Fi: 'wifi connect' in Terminal.";
    if (!url.isEmpty()) {
      _input = url;
      _editing = !navigate(url, false);
    }
  }

  String title() const override {
    if (_hasPage && !_page.title.isEmpty()) return "Web: " + _page.title;
    return String("Browser");
  }

  void onKey(const KeyEvent& k) override {
    wmRedraw();
    if (_editing) {                              // ввод адреса
      if (k.enter) {
        if (_input.isEmpty() && _hasPage) { _editing = false; return; }
        _editing = !navigate(_input, true);
        return;
      }
      if (k.back) {
        if (_input.length()) _input.remove(_input.length() - 1);
        return;
      }
      if (!k.fn && !k.ctrl && !k.alt && k.ch >= 32 && k.ch < 127) _input += k.ch;
      return;
    }
    // режим просмотра
    if (!k.fn && !k.ctrl && lowerc(k.ch) == 'g') {
      _input = _url;
      _editing = true;
      return;
    }
    if (k.back) { goBack(); return; }
    if (k.enter) { followSelected(); return; }
    switch (navOf(k, true)) {
      case Nav::Up:    selectLink(-1); break;
      case Nav::Down:  selectLink(+1); break;
      case Nav::Left:  scrollBy(-_rows); break;
      case Nav::Right: scrollBy(+_rows); break;
      default: break;
    }
  }

  void draw(const Box& b, bool) override {
    tft.fillRect(b.x, b.y, b.w, b.h, COL_BLACK);
    _rows = b.h / g_lineH - 2;
    if (_rows < 1) _rows = 1;

    // адресная строка
    tft.fillRect(b.x, b.y, b.w, g_lineH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);
    String bar = _editing ? tailText("> " + _input + "_", b.w - 4)
                          : fitText(_hasPage ? _url : String("(no page)"), b.w - 4);
    drawText(b.x + 2, b.y, bar);

    // содержимое страницы
    int y0 = b.y + g_lineH;
    if (_hasPage) {
      int last = _top + _rows;
      if (last > (int)_page.lines.size()) last = (int)_page.lines.size();
      for (int li = _top; li < last; li++) {
        int y = y0 + (li - _top) * g_lineH;
        const Line& L = _page.lines[li];
        for (size_t j = L.first; j < L.first + L.count; j++) {
          const Word& w = _page.words[j];
          if (w.text.isEmpty()) continue;
          bool sel = (w.link >= 0 && w.link == _sel);
          tft.setTextColor(sel ? COL_BLACK : w.color, sel ? COL_WHITE : w.bg);
          drawText(b.x + 2 + w.col * g_charW, y, w.text);
        }
      }
    } else {
      tft.setTextColor(COL_GRAY, COL_BLACK);
      drawText(b.x + 2, y0, "Type a URL and press Enter");
    }

    // строка статуса
    int sy = b.y + b.h - g_lineH;
    tft.fillRect(b.x, sy, b.w, g_lineH, COL_DGRAY);
    tft.setTextColor(COL_WHITE, COL_DGRAY);
    drawText(b.x + 2, sy, fitText(_status, b.w - 4));
  }

private:
  // Загружает страницу; при ошибке текущая страница остаётся на месте
  bool navigate(const String& raw, bool pushHist) {
    String url = normalizeInput(raw);
    if (url.isEmpty()) return false;
    if (!wifiUp()) {
      _status = "No Wi-Fi. Use 'wifi connect <ssid> <pass>' in Terminal.";
      return false;
    }
    String finalUrl, body, err;
    int code = 0;
    if (!httpGet(url, finalUrl, body, code, err)) {
      _status = "Error: " + err;
      return false;
    }
    if (pushHist && _hasPage) {
      _hist.push_back(_url);
      if (_hist.size() > MAX_HIST) _hist.erase(_hist.begin());
    }
    _url = finalUrl;
    _hasPage = true;
    _page = Page();
    // Run a bounded, non-DOM JavaScript subset before HTML parsing.
    String jsOutput = runTinyJavaScript(body);
    if (!jsOutput.isEmpty()) body += jsOutput;
    CssRules cssRules;
    parseCssRules(collectEmbeddedCss(body), cssRules);
    HtmlParser parser(_page, cssRules);
    parser.run(body);
    parser.finish();
    _sel = _page.links.empty() ? -1 : 0;
    _top = 0;
    _status = "HTTP " + String(code) + ", " + String(_page.links.size()) +
              " links | text + mini CSS/JS | [g] URL";
    return true;
  }

  void goBack() {
    if (_hist.empty()) { _status = "No history"; return; }
    String prev = _hist.back();
    _hist.pop_back();
    navigate(prev, false);
  }

  void followSelected() {
    if (_sel < 0 || _sel >= (int)_page.links.size()) {
      _status = "No link selected: press W/S";
      return;
    }
    navigate(resolveUrl(_url, _page.links[_sel]), true);
  }

  void selectLink(int d) {
    int n = (int)_page.links.size();
    if (n == 0) { scrollBy(d); return; }
    _sel = (_sel < 0) ? 0 : (_sel + d + n) % n;
    ensureSelVisible();
  }

  void ensureSelVisible() {
    if (_sel < 0 || _sel >= (int)_page.linkLine.size()) return;
    int ln = _page.linkLine[_sel];
    if (ln < 0) return;
    if (ln < _top) _top = ln;
    else if (ln >= _top + _rows) _top = ln - _rows + 1;
    clampTop();
  }

  void scrollBy(int d) {
    _top += d;
    clampTop();
  }

  void clampTop() {
    int maxTop = (int)_page.lines.size() - _rows;
    if (maxTop < 0) maxTop = 0;
    if (_top > maxTop) _top = maxTop;
    if (_top < 0) _top = 0;
  }

  String _url, _input, _status;
  bool _editing = true;
  bool _hasPage = false;
  Page _page;
  std::vector<String> _hist;
  int _sel = -1;
  int _top = 0;
  int _rows = 20;
};

}  // namespace

void openBrowser(const String& url) { wmOpen(new BrowserApp(url)); }

// Для команды curl: GET без разбора HTML
bool httpFetchText(const String& url, String& body, int& code, String& err) {
  String finalUrl;
  return httpGet(normalizeInput(url), finalUrl, body, code, err);
}
