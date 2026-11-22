// Shell: bash-подобный интерпретатор CardputerOS
//   конвейеры |, перенаправление > и >>, списки ; && ||,
//   переменные $VAR ${VAR} $?, кавычки, ~, шаблоны * и ? в именах файлов
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>
#include <algorithm>
#include <utility>
#include "core.h"

namespace {

typedef std::vector<String> Args;
typedef int (*CmdFn)(Args& a, const String* in, Print& out);

struct Builtin {
  const char* name;
  CmdFn fn;            // nullptr: команду выполняет терминал (clear, history, exit)
  const char* usage;
  const char* desc;
};

struct Ent {
  String name;
  bool dir;
  size_t size;
};

struct Cmd {
  Args argv;
  String outFile;
  bool append = false;
  bool ok = true;
  String err;
};

// Вывод в строку: для конвейеров и перенаправления
class StrOut : public Print {
public:
  String s;
  size_t write(uint8_t c) override {
    if (s.length() < 16384) s += (char)c;
    return 1;
  }
};

String g_cwd = "/";
int g_status = 0;     // $?
int g_depth = 0;      // вложенность sh
std::vector<std::pair<String, String>> g_env;

const std::vector<Builtin>& builtins();
const Builtin* findBuiltin(const String& name);

// ---------- Окружение ----------
String envGet(const String& k) {
  for (auto& e : g_env)
    if (e.first == k) return e.second;
  return String();
}

void envSet(const String& k, const String& v) {
  for (auto& e : g_env) {
    if (e.first == k) { e.second = v; return; }
  }
  g_env.push_back(std::make_pair(k, v));
}

void envUnset(const String& k) {
  g_env.erase(std::remove_if(g_env.begin(), g_env.end(),
              [&k](const std::pair<String, String>& e) { return e.first == k; }),
              g_env.end());
}

// ---------- Файловые помощники ----------
bool exists(const String& p) {
  File f = SD.open(p);
  bool e = (bool)f;
  f.close();
  return e;
}

bool isDir(const String& p) {
  if (p == "/") return true;
  File f = SD.open(p);
  bool d = f && f.isDirectory();
  f.close();
  return d;
}

std::vector<Ent> listDir(const String& path) {
  std::vector<Ent> v;
  File d = SD.open(path);
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f && v.size() < 300; f = d.openNextFile()) {
      Ent e;
      e.name = fsBaseName(f.name());
      e.dir = f.isDirectory();
      e.size = e.dir ? 0 : f.size();
      f.close();
      v.push_back(e);
    }
  }
  d.close();
  std::sort(v.begin(), v.end(), [](const Ent& x, const Ent& y) { return x.name < y.name; });
  return v;
}

// Читает файл как текст, не больше maxBytes байт
bool readFileText(const String& path, size_t maxBytes, String& out) {
  out = "";
  String p = fsResolve(path);
  if (!canRead(p)) return false;
  File f = SD.open(p, FILE_READ);
  if (!f || f.isDirectory()) { f.close(); return false; }
  while (f.available() && out.length() < maxBytes) out += (char)f.read();
  f.close();
  return true;
}

// Перезаписывает (append=false) или дописывает файл
bool writeFile(const String& path, const String& data, bool append) {
  String p = fsResolve(path);
  if (!canWrite(p) || isDir(p)) return false;
  if (!append) SD.remove(p);
  File f = SD.open(p, FILE_APPEND);
  if (!f) return false;
  f.print(data);
  f.close();
  return true;
}

std::vector<String> splitLines(const String& t) {
  std::vector<String> v;
  String cur;
  for (unsigned i = 0; i < t.length(); i++) {
    char c = t.charAt(i);
    if (c == '\r') continue;
    if (c == '\n') { v.push_back(cur); cur = ""; }
    else cur += c;
  }
  if (cur.length()) v.push_back(cur);
  return v;
}

// Текст из файла-операнда с номером fi или из конвейера. false: ошибка уже выведена.
bool inputFrom(const Args& ops, size_t fi, const String* in, String& text, Print& out, const char* cmd) {
  if (ops.size() > fi) {
    if (readFileText(ops[fi], 16384, text)) return true;
    out.println(String(cmd) + ": " + ops[fi] + ": " + accessError(ops[fi]));
    return false;
  }
  if (in) { text = *in; return true; }
  out.println(String(cmd) + ": missing file operand");
  return false;
}

// ---------- Аргументы ----------
bool isFlagArg(const String& s) { return s.length() > 1 && s.charAt(0) == '-'; }

bool hasFlag(const Args& a, char f) {
  for (size_t i = 1; i < a.size(); i++)
    if (isFlagArg(a[i]) && a[i].indexOf(f) > 0) return true;
  return false;
}

Args operands(const Args& a) {
  Args v;
  for (size_t i = 1; i < a.size(); i++)
    if (!isFlagArg(a[i])) v.push_back(a[i]);
  return v;
}

// Шаблон с * и ?
bool globMatch(const String& p, const String& s, unsigned pi = 0, unsigned si = 0) {
  while (pi < p.length()) {
    char c = p.charAt(pi);
    if (c == '*') {
      for (unsigned k = si; k <= s.length(); k++)
        if (globMatch(p, s, pi + 1, k)) return true;
      return false;
    }
    if (si >= s.length()) return false;
    if (c != '?' && c != s.charAt(si)) return false;
    pi++;
    si++;
  }
  return si == s.length();
}

// ---------- Разбор командной строки ----------

// $VAR, ${VAR}, $? — подставляет значение в w, двигает i
void expandVar(const String& s, unsigned& i, String& w) {
  const unsigned n = s.length();
  i++;                                              // пропускаем '$'
  if (i >= n) { w += '$'; return; }
  char c = s.charAt(i);
  if (c == '?') { w += String((long)g_status); i++; return; }
  if (c == '{') {
    int e = s.indexOf('}', i);
    if (e < 0) { w += "${"; i++; return; }
    w += envGet(s.substring(i + 1, e));
    i = (unsigned)e + 1;
    return;
  }
  String name;
  while (i < n) {
    char d = s.charAt(i);
    bool ok = (d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') || d == '_';
    if (!ok) break;
    name += d;
    i++;
  }
  if (name.isEmpty()) { w += '$'; return; }
  w += envGet(name);
}

// Читает одно слово с раскрытием кавычек, $, ~. glob = true, если есть * или ? вне кавычек.
bool readWord(const String& s, unsigned& i, String& w, bool& glob) {
  const unsigned n = s.length();
  while (i < n && s.charAt(i) == ' ') i++;
  const unsigned start = i;
  w = "";
  glob = false;
  bool any = false;
  while (i < n) {
    char c = s.charAt(i);
    if (c == ' ' || c == '|' || c == ';' || c == '>') break;
    if (c == '&' && i + 1 < n && s.charAt(i + 1) == '&') break;
    any = true;
    if (c == '\'') {
      i++;
      while (i < n && s.charAt(i) != '\'') w += s.charAt(i++);
      if (i < n) i++;
    } else if (c == '"') {
      i++;
      while (i < n && s.charAt(i) != '"') {
        char d = s.charAt(i);
        if (d == '$') {
          expandVar(s, i, w);
        } else if (d == '\\' && i + 1 < n &&
                   (s.charAt(i + 1) == '"' || s.charAt(i + 1) == '\\' || s.charAt(i + 1) == '$')) {
          w += s.charAt(i + 1);
          i += 2;
        } else {
          w += d;
          i++;
        }
      }
      if (i < n) i++;
    } else if (c == '\\') {
      if (i + 1 < n) w += s.charAt(i + 1);
      i += 2;
    } else if (c == '$') {
      expandVar(s, i, w);
    } else if (c == '~' && i == start && (i + 1 >= n || s.charAt(i + 1) == '/')) {
      w += envGet("HOME");
      i++;
    } else {
      if (c == '*' || c == '?') glob = true;
      w += c;
      i++;
    }
  }
  return any;
}

// Шаблон раскрывается в список путей; нет совпадений — слово остаётся как есть
void addGlob(Args& argv, const String& pat) {
  int slash = pat.lastIndexOf('/');
  String dirTyped = (slash >= 0) ? pat.substring(0, slash + 1) : String();
  String namePat = (slash >= 0) ? pat.substring(slash + 1) : pat;
  Args hits;
  String real = fsResolve(dirTyped.isEmpty() ? String(".") : dirTyped);
  for (auto& e : listDir(real)) {
    if (e.name.startsWith(".")) continue;
    if (globMatch(namePat, e.name)) hits.push_back(dirTyped + e.name + (e.dir ? "/" : ""));
  }
  if (hits.empty()) { argv.push_back(pat); return; }
  std::sort(hits.begin(), hits.end());
  for (auto& h : hits) argv.push_back(h);
}

Cmd parseCmd(const String& s) {
  Cmd c;
  unsigned i = 0;
  const unsigned n = s.length();
  for (;;) {
    while (i < n && s.charAt(i) == ' ') i++;
    if (i >= n) break;
    if (s.charAt(i) == '>') {
      i++;
      if (i < n && s.charAt(i) == '>') { c.append = true; i++; }
      String target;
      bool g = false;
      if (!readWord(s, i, target, g)) {
        c.ok = false;
        c.err = "syntax error near '>'";
        return c;
      }
      c.outFile = target;
      continue;
    }
    String w;
    bool g = false;
    if (!readWord(s, i, w, g)) {
      c.ok = false;
      c.err = "syntax error";
      return c;
    }
    if (g) addGlob(c.argv, w);
    else c.argv.push_back(w);
  }
  return c;
}

// Режет строку по |, ;, && и || вне кавычек.
// ops[k] — разделитель между parts[k] и parts[k+1]: 0 '|', 1 ';', 2 '&&', 3 '||'
void splitTop(const String& s, std::vector<String>& parts, std::vector<int>& ops) {
  parts.clear();
  ops.clear();
  String cur;
  char q = 0;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (q) {
      cur += c;
      if (c == '\\' && q == '"' && i + 1 < s.length()) { cur += s.charAt(++i); continue; }
      if (c == q) q = 0;
      continue;
    }
    if (c == '\\' && i + 1 < s.length()) { cur += c; cur += s.charAt(++i); continue; }
    if (c == '\'' || c == '"') { q = c; cur += c; continue; }
    bool more = (i + 1 < s.length());
    if (c == '&' && more && s.charAt(i + 1) == '&') {
      parts.push_back(cur); ops.push_back(2); cur = ""; i++; continue;
    }
    if (c == '|' && more && s.charAt(i + 1) == '|') {
      parts.push_back(cur); ops.push_back(3); cur = ""; i++; continue;
    }
    if (c == '|') { parts.push_back(cur); ops.push_back(0); cur = ""; continue; }
    if (c == ';') { parts.push_back(cur); ops.push_back(1); cur = ""; continue; }
    cur += c;
  }
  parts.push_back(cur);
}

// ---------- Исполнение ----------
int runArgv(const Args& argv, const String* in, Print& out) {
  if (argv.empty()) return 0;
  int eq = argv[0].indexOf('=');
  if (eq > 0 && argv.size() == 1) {                 // присваивание: NAME=value
    envSet(argv[0].substring(0, eq), argv[0].substring(eq + 1));
    return 0;
  }
  const Builtin* b = findBuiltin(argv[0]);
  if (!b) {
    out.println("bash: " + argv[0] + ": command not found");
    return 127;
  }
  if (!b->fn) return 0;
  Args a = argv;
  return b->fn(a, in, out);
}

int runPipeline(const std::vector<String>& stages, Print& out) {
  String buf;
  bool haveIn = false;
  int st = 0;
  for (size_t j = 0; j < stages.size(); j++) {
    Cmd c = parseCmd(stages[j]);
    if (!c.ok) { out.println("bash: " + c.err); return 2; }
    const String* in = haveIn ? &buf : nullptr;
    bool last = (j + 1 == stages.size());
    if (last && c.outFile.isEmpty()) {
      st = runArgv(c.argv, in, out);
      continue;
    }
    StrOut so;
    st = runArgv(c.argv, in, so);
    if (!c.outFile.isEmpty()) {
      if (!writeFile(c.outFile, so.s, c.append)) {
        out.println("bash: " + c.outFile + ": " +
                    (canWrite(fsResolve(c.outFile)) ? String("cannot create file") : String("Permission denied")));
        return 1;
      }
      buf = "";
    } else {
      buf = so.s;
    }
    haveIn = true;
  }
  return st;
}

// ---------- Команды: файлы ----------
int cmdLs(Args& a, const String*, Print& out) {
  bool all = hasFlag(a, 'a'), lng = hasFlag(a, 'l');
  Args paths = operands(a);
  if (paths.empty()) paths.push_back(".");
  int st = 0;
  for (size_t p = 0; p < paths.size(); p++) {
    const String& shown = paths[p];
    String real = fsResolve(shown);
    if (!canRead(real)) {
      out.println("ls: cannot open directory '" + shown + "': Permission denied");
      st = 2;
      continue;
    }
    if (!exists(real)) {
      out.println("ls: cannot access '" + shown + "': No such file or directory");
      st = 2;
      continue;
    }
    if (!isDir(real)) { out.println(shown); continue; }
    if (paths.size() > 1) out.println(shown + ":");
    for (auto& e : listDir(real)) {
      if (!all && e.name.startsWith(".")) continue;
      String label = e.name + (e.dir ? "/" : "");
      if (lng) out.println(String(e.dir ? "d" : "-") + "rw-r--r-- " + String((unsigned long)e.size) + " " + label);
      else out.print(label + "  ");
    }
    if (!lng) out.println();
    if (paths.size() > 1 && p + 1 < paths.size()) out.println();
  }
  return st;
}

int cmdCd(Args& a, const String*, Print& out) {
  String target = a.size() > 1 ? a[1] : envGet("HOME");
  String p = fsResolve(target);
  if (!canRead(p)) {
    out.println("bash: cd: " + target + ": Permission denied");
    return 1;
  }
  if (!isDir(p)) {
    out.println("bash: cd: " + target + ": No such file or directory");
    return 1;
  }
  g_cwd = p;
  envSet("PWD", p);
  return 0;
}

int cmdPwd(Args&, const String*, Print& out) {
  out.println(g_cwd);
  return 0;
}

int cmdCat(Args& a, const String* in, Print& out) {
  bool num = hasFlag(a, 'n');
  Args files = operands(a);
  auto emit = [&](const String& text) {
    long i = 1;
    for (auto& l : splitLines(text)) {
      if (num) out.println(String(i++) + "\t" + l);
      else out.println(l);
    }
  };
  if (files.empty()) {
    if (in) emit(*in);
    return 0;
  }
  int st = 0;
  for (auto& f : files) {
    String t;
    if (!readFileText(f, 8192, t)) {
      out.println("cat: " + f + ": " + accessError(f));
      st = 1;
      continue;
    }
    emit(t);
  }
  return st;
}

int headTail(Args& a, const String* in, Print& out, bool head) {
  int n = 10;
  Args files;
  for (size_t i = 1; i < a.size(); i++) {
    if (a[i] == "-n" && i + 1 < a.size()) n = (int)a[++i].toInt();
    else if (!isFlagArg(a[i])) files.push_back(a[i]);
  }
  String text;
  if (!files.empty()) {
    if (!readFileText(files[0], 16384, text)) {
      out.println(a[0] + ": " + files[0] + ": " + accessError(files[0]));
      return 1;
    }
  } else if (in) {
    text = *in;
  } else {
    out.println("usage: " + a[0] + " [-n N] [file]");
    return 1;
  }
  std::vector<String> lines = splitLines(text);
  int total = (int)lines.size();
  int from = head ? 0 : (total - n > 0 ? total - n : 0);
  int to = head ? (n < total ? n : total) : total;
  for (int i = from; i < to; i++) out.println(lines[i]);
  return 0;
}

int cmdHead(Args& a, const String* in, Print& out) { return headTail(a, in, out, true); }
int cmdTail(Args& a, const String* in, Print& out) { return headTail(a, in, out, false); }

int cmdWc(Args& a, const String* in, Print& out) {
  bool fl = hasFlag(a, 'l'), fw = hasFlag(a, 'w'), fc = hasFlag(a, 'c');
  if (!fl && !fw && !fc) fl = fw = fc = true;
  Args files = operands(a);
  String text;
  if (!files.empty()) {
    if (!readFileText(files[0], 16384, text)) {
      out.println("wc: " + files[0] + ": " + accessError(files[0]));
      return 1;
    }
  } else if (in) {
    text = *in;
  }
  long lines = 0, words = 0;
  bool inWord = false;
  for (unsigned i = 0; i < text.length(); i++) {
    char c = text.charAt(i);
    if (c == '\n') lines++;
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') inWord = false;
    else if (!inWord) { inWord = true; words++; }
  }
  String r;
  if (fl) r += String(lines) + " ";
  if (fw) r += String(words) + " ";
  if (fc) r += String((long)text.length()) + " ";
  if (!files.empty()) r += files[0];
  r.trim();
  out.println(r);
  return 0;
}

int cmdTouch(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.empty()) { out.println("touch: missing file operand"); return 1; }
  int st = 0;
  for (auto& f : ops) {
    String p = fsResolve(f);
    if (!canWrite(p)) { out.println("touch: cannot touch '" + f + "': Permission denied"); st = 1; continue; }
    File x = SD.open(p, FILE_APPEND);
    if (!x) { out.println("touch: cannot touch '" + f + "'"); st = 1; }
    x.close();
  }
  return st;
}

bool mkdirP(const String& path) {
  String cur;
  int start = 1;
  while (start <= (int)path.length()) {
    int slash = path.indexOf('/', start);
    int end = (slash < 0) ? (int)path.length() : slash;
    String seg = path.substring(start, end);
    if (!seg.isEmpty()) {
      cur += "/" + seg;
      if (!isDir(cur) && !SD.mkdir(cur)) return false;
    }
    if (slash < 0) break;
    start = slash + 1;
  }
  return true;
}

int cmdMkdir(Args& a, const String*, Print& out) {
  bool parents = hasFlag(a, 'p');
  Args ops = operands(a);
  if (ops.empty()) { out.println("mkdir: missing operand"); return 1; }
  int st = 0;
  for (auto& op : ops) {
    String p = fsResolve(op);
    if (!canWrite(p)) { out.println("mkdir: cannot create directory '" + op + "': Permission denied"); st = 1; continue; }
    bool ok = parents ? mkdirP(p) : (!exists(p) && SD.mkdir(p));
    if (!ok) { out.println("mkdir: cannot create directory '" + op + "'"); st = 1; }
  }
  return st;
}

bool removeTree(const String& p) {
  for (auto& e : listDir(p)) {
    String c = fsJoin(p, e.name);
    bool ok = e.dir ? removeTree(c) : SD.remove(c);
    if (!ok) return false;
  }
  return SD.rmdir(p);
}

int cmdRm(Args& a, const String*, Print& out) {
  bool rec = hasFlag(a, 'r') || hasFlag(a, 'R');
  bool force = hasFlag(a, 'f');
  Args ops = operands(a);
  if (ops.empty()) { out.println("rm: missing operand"); return 1; }
  int st = 0;
  for (auto& op : ops) {
    String p = fsResolve(op);
    if (p == "/") { out.println("rm: refusing to remove '/'"); st = 1; continue; }
    if (!canWrite(p)) { out.println("rm: cannot remove '" + op + "': Permission denied"); st = 1; continue; }
    if (!exists(p)) {
      if (!force) { out.println("rm: cannot remove '" + op + "': No such file or directory"); st = 1; }
      continue;
    }
    bool ok;
    if (isDir(p)) {
      if (!rec) { out.println("rm: cannot remove '" + op + "': Is a directory"); st = 1; continue; }
      ok = removeTree(p);
    } else {
      ok = SD.remove(p);
    }
    if (!ok) { out.println("rm: cannot remove '" + op + "'"); st = 1; }
  }
  return st;
}

int cmdCp(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.size() < 2) { out.println("usage: cp src dst"); return 1; }
  String src = fsResolve(ops[0]);
  String dst = fsResolve(ops[1]);
  if (!exists(src) || isDir(src)) {
    out.println("cp: cannot stat '" + ops[0] + "': No such file or directory");
    return 1;
  }
  if (!canRead(src) || !canWrite(dst)) {
    out.println("cp: " + ops[1] + ": Permission denied");
    return 1;
  }
  if (isDir(dst)) dst = fsJoin(dst, fsBaseName(src));
  if (src == dst) return 0;
  File fin = SD.open(src, FILE_READ);
  SD.remove(dst);
  File fout = SD.open(dst, FILE_APPEND);
  if (!fout) {
    out.println("cp: cannot create '" + ops[1] + "'");
    fin.close();
    return 1;
  }
  uint8_t buf[512];
  while (fin.available()) {
    int n = fin.read(buf, sizeof(buf));
    if (n <= 0) break;
    fout.write(buf, (size_t)n);
  }
  fin.close();
  fout.close();
  return 0;
}

int cmdMv(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.size() < 2) { out.println("usage: mv src dst"); return 1; }
  String src = fsResolve(ops[0]);
  String dst = fsResolve(ops[1]);
  if (!canWrite(src) || !canWrite(dst)) {
    out.println("mv: " + ops[0] + ": Permission denied");
    return 1;
  }
  if (!exists(src)) {
    out.println("mv: cannot stat '" + ops[0] + "': No such file or directory");
    return 1;
  }
  if (isDir(dst)) dst = fsJoin(dst, fsBaseName(src));
  if (exists(dst)) {
    if (isDir(dst)) { out.println("mv: cannot overwrite directory"); return 1; }
    SD.remove(dst);
  }
  if (!SD.rename(src, dst)) { out.println("mv: failed"); return 1; }
  return 0;
}

void findRec(const String& real, const String& shown, const String& pat, Print& out, int depth) {
  if (depth > 8) return;
  for (auto& e : listDir(real)) {
    String rp = fsJoin(real, e.name);
    String sp = shown + "/" + e.name;
    if (pat.isEmpty() || globMatch(pat, e.name)) out.println(sp);
    if (e.dir) findRec(rp, sp, pat, out, depth + 1);
  }
}

int cmdFind(Args& a, const String*, Print& out) {
  String dir = ".";
  String pat;
  for (size_t i = 1; i < a.size(); i++) {
    if (a[i] == "-name" && i + 1 < a.size()) pat = a[++i];
    else if (!isFlagArg(a[i])) dir = a[i];
  }
  String real = fsResolve(dir);
  if (!exists(real)) {
    out.println("find: '" + dir + "': No such file or directory");
    return 1;
  }
  String shown = dir;
  if (shown == "/") shown = "";
  else while (shown.endsWith("/")) shown.remove(shown.length() - 1);
  if (pat.isEmpty()) out.println(dir);
  findRec(real, shown, pat, out, 0);
  return 0;
}

void treeRec(const String& real, const String& prefix, Print& out, int depth, int& dirs, int& files) {
  if (depth > 5) return;
  std::vector<Ent> kids = listDir(real);
  for (size_t i = 0; i < kids.size(); i++) {
    bool last = (i + 1 == kids.size());
    out.println(prefix + (last ? "`-- " : "|-- ") + kids[i].name + (kids[i].dir ? "/" : ""));
    if (kids[i].dir) {
      dirs++;
      treeRec(fsJoin(real, kids[i].name), prefix + (last ? "    " : "|   "), out, depth + 1, dirs, files);
    } else {
      files++;
    }
  }
}

int cmdTree(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  String shown = ops.empty() ? String(".") : ops[0];
  String real = fsResolve(shown);
  if (!isDir(real)) { out.println("tree: '" + shown + "': not a directory"); return 1; }
  out.println(shown);
  int dirs = 0, files = 0;
  treeRec(real, "", out, 0, dirs, files);
  out.println(String(dirs) + " directories, " + String(files) + " files");
  return 0;
}

uint64_t sizeOf(const String& p) {
  if (!isDir(p)) {
    File f = SD.open(p);
    uint64_t s = f ? f.size() : 0;
    f.close();
    return s;
  }
  uint64_t t = 0;
  for (auto& e : listDir(p)) t += e.dir ? sizeOf(fsJoin(p, e.name)) : e.size;
  return t;
}

String fmtSize(uint64_t n, bool human) {
  if (!human) return String((unsigned long)((n + 1023) / 1024)) + "K";
  if (n < 1024) return String((unsigned long)n) + "B";
  if (n < 1024UL * 1024UL) return String((float)n / 1024.0f, 1) + "K";
  return String((float)n / (1024.0f * 1024.0f), 1) + "M";
}

int cmdDu(Args& a, const String*, Print& out) {
  bool human = hasFlag(a, 'h');
  Args ops = operands(a);
  String shown = ops.empty() ? String(".") : ops[0];
  String real = fsResolve(shown);
  if (!exists(real)) { out.println("du: cannot access '" + shown + "'"); return 1; }
  out.println(fmtSize(sizeOf(real), human) + "\t" + shown);
  return 0;
}

int cmdDf(Args&, const String*, Print& out) {
  uint64_t tot = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  if (tot == 0) { out.println("df: SD card not mounted"); return 1; }
  out.println("Filesystem  Size(MB)  Used(MB)  Avail(MB)  Use%");
  out.println("sdcard      " + String((unsigned long)(tot >> 20)) + "  " +
              String((unsigned long)(used >> 20)) + "  " +
              String((unsigned long)((tot - used) >> 20)) + "  " +
              String((unsigned long)(used * 100 / tot)) + "%");
  return 0;
}

int cmdBasename(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.empty()) { out.println("basename: missing operand"); return 1; }
  String s = ops[0];
  while (s.length() > 1 && s.endsWith("/")) s.remove(s.length() - 1);
  out.println(fsBaseName(s));
  return 0;
}

int cmdDirname(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.empty()) { out.println("dirname: missing operand"); return 1; }
  String s = ops[0];
  while (s.length() > 1 && s.endsWith("/")) s.remove(s.length() - 1);
  int i = s.lastIndexOf('/');
  if (i < 0) out.println(".");
  else if (i == 0) out.println("/");
  else out.println(s.substring(0, i));
  return 0;
}

// ---------- Команды: текст ----------
int cmdEcho(Args& a, const String*, Print& out) {
  bool nl = true;
  size_t i = 1;
  if (i < a.size() && a[i] == "-n") { nl = false; i++; }
  String s;
  for (size_t j = i; j < a.size(); j++) {
    if (j > i) s += ' ';
    s += a[j];
  }
  if (nl) out.println(s);
  else out.print(s);
  return 0;
}

int cmdPrintf(Args& a, const String*, Print& out) {
  if (a.size() < 2) { out.println("usage: printf format [arg...]"); return 1; }
  const String& f = a[1];
  size_t ai = 2;
  String s;
  for (unsigned i = 0; i < f.length(); i++) {
    char c = f.charAt(i);
    if (c == '\\' && i + 1 < f.length()) {
      char d = f.charAt(++i);
      s += (d == 'n') ? '\n' : (d == 't') ? '\t' : d;
      continue;
    }
    if (c == '%' && i + 1 < f.length()) {
      char d = f.charAt(++i);
      if (d == '%') s += '%';
      else if (d == 's') { if (ai < a.size()) s += a[ai++]; }
      else if (d == 'd') { long v = 0; if (ai < a.size()) v = a[ai++].toInt(); s += String(v); }
      else { s += '%'; s += d; }
      continue;
    }
    s += c;
  }
  out.print(s);
  return 0;
}

int cmdGrep(Args& a, const String* in, Print& out) {
  bool ci = hasFlag(a, 'i'), num = hasFlag(a, 'n'), inv = hasFlag(a, 'v');
  Args ops = operands(a);
  if (ops.empty()) { out.println("usage: grep [-inv] pattern [file]"); return 2; }
  String pat = ops[0];
  if (ci) pat.toLowerCase();
  Args rest;
  if (ops.size() > 1) rest.push_back(ops[1]);
  String text;
  if (!inputFrom(rest, 0, in, text, out, "grep")) return 2;
  int hits = 0;
  std::vector<String> lines = splitLines(text);
  for (size_t i = 0; i < lines.size(); i++) {
    String cmp = lines[i];
    if (ci) cmp.toLowerCase();
    bool m = cmp.indexOf(pat) >= 0;
    if (m != inv) {
      hits++;
      if (num) out.println(String((unsigned long)(i + 1)) + ":" + lines[i]);
      else out.println(lines[i]);
    }
  }
  return hits ? 0 : 1;
}

int cmdSort(Args& a, const String* in, Print& out) {
  String text;
  if (!inputFrom(operands(a), 0, in, text, out, "sort")) return 1;
  std::vector<String> v = splitLines(text);
  std::sort(v.begin(), v.end());
  if (hasFlag(a, 'r')) std::reverse(v.begin(), v.end());
  for (auto& l : v) out.println(l);
  return 0;
}

int cmdUniq(Args& a, const String* in, Print& out) {
  String text;
  if (!inputFrom(operands(a), 0, in, text, out, "uniq")) return 1;
  bool have = false;
  String prev;
  for (auto& l : splitLines(text)) {
    if (!have || l != prev) out.println(l);
    prev = l;
    have = true;
  }
  return 0;
}

int cmdTee(Args& a, const String* in, Print& out) {
  bool app = hasFlag(a, 'a');
  Args ops = operands(a);
  if (ops.empty()) { out.println("usage: tee [-a] file"); return 1; }
  String text = in ? *in : String();
  out.print(text);
  return writeFile(ops[0], text, app) ? 0 : 1;
}

int cmdSeq(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  long first = 1, last = 0;
  if (ops.size() == 1) last = ops[0].toInt();
  else if (ops.size() >= 2) { first = ops[0].toInt(); last = ops[1].toInt(); }
  else { out.println("usage: seq [first] last"); return 1; }
  long count = 0;
  for (long i = first; i <= last && count < 1000; i++, count++) out.println(String(i));
  return 0;
}

// ---------- Команды: оболочка ----------
int cmdExport(Args& a, const String*, Print& out) {
  if (a.size() < 2) {
    for (auto& e : g_env) out.println("declare -x " + e.first + "=\"" + e.second + "\"");
    return 0;
  }
  int st = 0;
  for (size_t i = 1; i < a.size(); i++) {
    int eq = a[i].indexOf('=');
    if (eq <= 0) {
      out.println("bash: export: '" + a[i] + "': not a valid identifier");
      st = 1;
      continue;
    }
    envSet(a[i].substring(0, eq), a[i].substring(eq + 1));
  }
  return st;
}

int cmdUnset(Args& a, const String*, Print&) {
  for (size_t i = 1; i < a.size(); i++) envUnset(a[i]);
  return 0;
}

int cmdEnv(Args&, const String*, Print& out) {
  for (auto& e : g_env) out.println(e.first + "=" + e.second);
  return 0;
}

int cmdTrue(Args&, const String*, Print&) { return 0; }
int cmdFalse(Args&, const String*, Print&) { return 1; }

int cmdSh(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.empty()) { out.println("usage: sh script"); return 2; }
  if (g_depth >= 4) { out.println("sh: nesting too deep"); return 1; }
  String text;
  if (!readFileText(ops[0], 4096, text)) {
    out.println("sh: " + ops[0] + ": " + accessError(ops[0]));
    return 127;
  }
  g_depth++;
  int st = 0;
  for (auto& l : splitLines(text)) {
    String t = l;
    t.trim();
    if (t.isEmpty() || t.startsWith("#")) continue;
    st = shellExec(t, out);
  }
  g_depth--;
  return st;
}

int cmdWhich(Args& a, const String*, Print& out) {
  int st = 0;
  for (size_t i = 1; i < a.size(); i++) {
    if (findBuiltin(a[i])) out.println("/bin/" + a[i]);
    else st = 1;
  }
  return st;
}

int cmdType(Args& a, const String*, Print& out) {
  int st = 0;
  for (size_t i = 1; i < a.size(); i++) {
    if (findBuiltin(a[i])) out.println(a[i] + " is a shell builtin");
    else { out.println("bash: type: " + a[i] + ": not found"); st = 1; }
  }
  return st;
}

int cmdMan(Args& a, const String*, Print& out) {
  if (a.size() < 2) { out.println("What manual page do you want? Try: man ls"); return 1; }
  const Builtin* b = findBuiltin(a[1]);
  if (!b) { out.println("No manual entry for " + a[1]); return 1; }
  out.println("NAME");
  out.println(String("    ") + b->name + " - " + b->desc);
  out.println("SYNOPSIS");
  out.println(String("    ") + b->name + " " + b->usage);
  return 0;
}

int cmdHelp(Args& a, const String* in, Print& out) {
  if (a.size() > 1) return cmdMan(a, in, out);
  out.println("CardputerOS shell. Builtins:");
  String line;
  for (auto& b : builtins()) {
    String n = String(b.name) + " ";
    if (line.length() + n.length() > (unsigned)g_cols) {
      out.println(line);
      line = "";
    }
    line += n;
  }
  if (!line.isEmpty()) out.println(line);
  out.println("Operators: | > >> ; && ||   Try: man <name>");
  return 0;
}

// ---------- Команды: система ----------
int cmdUname(Args& a, const String*, Print& out) {
  if (hasFlag(a, 'a')) out.println("CardputerOS 0.1 " + envGet("HOSTNAME") + " " + String(ESP.getChipModel()) + " xtensa");
  else out.println("CardputerOS");
  return 0;
}

int cmdHostname(Args&, const String*, Print& out) {
  out.println(envGet("HOSTNAME"));
  return 0;
}

int cmdWhoami(Args&, const String*, Print& out) {
  out.println(envGet("USER"));
  return 0;
}

int cmdId(Args&, const String*, Print& out) {
  String u = envGet("USER");
  String id = String(userUid(u));
  out.println("uid=" + id + "(" + u + ") gid=" + id + "(" + u + ") groups=" + id + "(" + u + ")");
  return 0;
}

int cmdUptime(Args&, const String*, Print& out) {
  unsigned long s = millis() / 1000;
  out.println("up " + String(s / 3600) + "h " + String((s % 3600) / 60) + "m " + String(s % 60) + "s");
  return 0;
}

int cmdDate(Args&, const String*, Print& out) {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    out.println("date: time not synchronized, connect Wi-Fi first");
    return 1;
  }
  struct tm t;
  localtime_r(&now, &t);
  char buf[48];
  strftime(buf, sizeof(buf), "%a %d %b %Y %H:%M:%S", &t);
  out.println(buf);
  return 0;
}

int cmdFree(Args&, const String*, Print& out) {
  out.println("heap:  total " + String(ESP.getHeapSize() / 1024) + " KB, free " +
              String(ESP.getFreeHeap() / 1024) + " KB, min free " +
              String(ESP.getMinFreeHeap() / 1024) + " KB");
  return 0;
}

int cmdSleep(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.empty()) { out.println("usage: sleep seconds"); return 1; }
  long s = ops[0].toInt();
  if (s < 0) s = 0;
  if (s > 30) s = 30;
  delay((uint32_t)s * 1000);            // блокирует интерфейс
  return 0;
}

int cmdReboot(Args&, const String*, Print& out) {
  if (!requireRoot(out, "reboot")) return 1;
  out.println("rebooting...");
  delay(300);
  ESP.restart();
  return 0;
}

int cmdPoweroff(Args&, const String*, Print& out) {
  if (!requireRoot(out, "poweroff")) return 1;
  out.println("powering off (deep sleep), press RESET to wake");
  delay(300);
  esp_deep_sleep_start();
  return 0;
}

// ---------- Команды: сеть и приложения ----------
int cmdWifi(Args& a, const String*, Print& out) {
  String sub = (a.size() > 1) ? a[1] : String();
  if ((sub == "connect" || sub == "disconnect") && !requireRoot(out, "wifi")) return 1;
  if (sub == "scan") {
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    if (n <= 0) out.println("no networks found");
    for (int i = 0; i < n && i < 15; i++) {
      out.print(String(i + 1) + ") " + WiFi.SSID(i));
      out.print("  rssi " + String(WiFi.RSSI(i)));
      out.println(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "  open" : "  secured");
    }
    WiFi.scanDelete();
    return 0;
  }
  if (sub == "connect" && a.size() >= 3) {
    String pass = (a.size() > 3) ? a[3] : String();
    out.println("connecting to " + a[2] + " ...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(a[2].c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
      out.println("ok, IP " + WiFi.localIP().toString());
      configTzTime(TIMEZONE_POSIX, "pool.ntp.org");
      return 0;
    }
    out.println("failed");
    WiFi.disconnect();
    return 1;
  }
  if (sub == "status") {
    if (wifiUp()) out.println("connected: " + WiFi.SSID() + " " + WiFi.localIP().toString());
    else out.println("disconnected");
    return 0;
  }
  if (sub == "disconnect") {
    WiFi.disconnect(true);
    out.println("disconnected");
    return 0;
  }
  out.println("usage: wifi scan | connect SSID [PASS] | status | disconnect");
  return 1;
}

int cmdIp(Args& a, const String*, Print& out) {
  if (a.size() < 2 || (a[1] != "addr" && a[1] != "a")) { out.println("usage: ip addr"); return 1; }
  out.println("1: lo: <LOOPBACK> state UNKNOWN");
  out.println("    inet 127.0.0.1/8");
  if (!wifiUp()) {
    out.println("2: wlan0: <DOWN> state DOWN");
    return 0;
  }
  out.println("2: wlan0: <UP> state UP");
  out.println("    inet " + WiFi.localIP().toString() + "/24");
  out.println("    ssid " + WiFi.SSID());
  return 0;
}

int cmdCurl(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.empty()) { out.println("curl: try 'curl <url>'"); return 2; }
  if (!wifiUp()) { out.println("curl: no network, run: wifi connect SSID PASS"); return 1; }
  String body, err;
  int code = 0;
  if (!httpFetchText(ops[0], body, code, err)) { out.println("curl: " + err); return 1; }
  if (body.length() > 2000) body = body.substring(0, 2000) + "\n[...]";
  out.println(body);
  return 0;
}

int cmdBrowse(Args& a, const String*, Print&) {
  Args ops = operands(a);
  openBrowser(ops.empty() ? String() : ops[0]);
  return 0;
}

int cmdMedia(Args&, const String*, Print&) { openMediaPlayer(); return 0; }

int cmdFiles(Args& a, const String*, Print&) {
  Args ops = operands(a);
  openFiles(ops.empty() ? g_cwd : fsResolve(ops[0]));
  return 0;
}

int cmdRun(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  if (ops.empty()) { out.println("usage: run <file.lua>"); return 2; }
  String p = fsResolve(ops[0]);
  if (!canRead(p)) { out.println("run: " + ops[0] + ": Permission denied"); return 1; }
  openScript(p);
  return 0;
}

// ---------- Таблица команд ----------
// ---------- Команды: пользователи и привилегии ----------
PwRequest g_pwReq;          // запрос пароля, который терминал покажет пользователю
bool g_logoutReq = false;   // запрос выхода, его обработает main

void requestPw(PwKind kind, const String& user, const String& cmd) {
  g_pwReq.kind = kind;
  g_pwReq.user = user;
  g_pwReq.cmd = cmd;
}

// Собирает аргументы обратно в строку (для sudo), кавычки ставятся где нужно
String joinArgs(const Args& a, size_t from) {
  String s;
  for (size_t i = from; i < a.size(); i++) {
    if (i > from) s += ' ';
    const String& w = a[i];
    bool plain = !w.isEmpty();
    for (unsigned k = 0; k < w.length() && plain; k++) {
      char c = w.charAt(k);
      if (c == ' ' || c == '|' || c == ';' || c == '>' || c == '&' || c == '"' ||
          c == '\'' || c == '$' || c == '*' || c == '?' || c == '~' || c == '\\') {
        plain = false;
      }
    }
    if (plain) { s += w; continue; }
    s += '"';
    for (unsigned k = 0; k < w.length(); k++) {
      char c = w.charAt(k);
      if (c == '"' || c == '\\' || c == '$') s += '\\';
      s += c;
    }
    s += '"';
  }
  return s;
}

int cmdSu(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  String target = ops.empty() ? String("root") : ops[0];
  if (!userExists(target)) {
    out.println("su: user " + target + " does not exist");
    return 1;
  }
  if (sessionIsRoot()) {                  // root переходит без пароля
    shellSwitchUser(target, false);
    return 0;
  }
  requestPw(PW_SU, target, "");           // пароль целевого пользователя спросит терминал
  return 0;
}

int cmdSudo(Args& a, const String*, Print& out) {
  if (a.size() < 2) { out.println("usage: sudo command [args...]"); return 2; }
  String cmd = joinArgs(a, 1);
  if (sessionIsRoot()) return shellExec(cmd, out);
  requestPw(PW_SUDO, sessionUser(), cmd); // пароль текущего пользователя
  return 0;
}

int cmdPasswd(Args& a, const String*, Print& out) {
  Args ops = operands(a);
  String target = ops.empty() ? sessionUser() : ops[0];
  if (!userExists(target)) {
    out.println("passwd: user '" + target + "' does not exist");
    return 1;
  }
  if (!sessionIsRoot() && target != sessionUser()) {
    out.println("passwd: You may not change the password for " + target + ".");
    return 1;
  }
  requestPw(sessionIsRoot() ? PW_NEW : PW_PASSWD_OLD, target, "passwd");
  return 0;
}

int cmdUseradd(Args& a, const String*, Print& out) {
  if (!requireRoot(out, "useradd")) return 1;
  Args ops = operands(a);
  if (ops.empty()) { out.println("usage: useradd name"); return 2; }
  if (!userValidName(ops[0])) {
    out.println("useradd: invalid user name '" + ops[0] + "'");
    return 1;
  }
  if (userExists(ops[0])) {
    out.println("useradd: user '" + ops[0] + "' already exists");
    return 1;
  }
  requestPw(PW_NEW, ops[0], "useradd");   // пароль нового пользователя спросит терминал
  return 0;
}

int cmdUserdel(Args& a, const String*, Print& out) {
  if (!requireRoot(out, "userdel")) return 1;
  Args ops = operands(a);
  if (ops.empty()) { out.println("usage: userdel name"); return 2; }
  if (ops[0] == "root") { out.println("userdel: cannot remove root"); return 1; }
  if (!userExists(ops[0])) {
    out.println("userdel: user '" + ops[0] + "' does not exist");
    return 1;
  }
  if (!userDel(ops[0])) { out.println("userdel: failed"); return 1; }
  out.println("user '" + ops[0] + "' removed (home directory kept)");
  return 0;
}

int cmdUsers(Args&, const String*, Print& out) {
  String line;
  for (auto& n : userNames()) {
    if (!line.isEmpty()) line += "  ";
    line += n;
  }
  out.println(line);
  return 0;
}

int cmdLogin(Args&, const String*, Print&) {
  openLogin();
  return 0;
}

int cmdLogout(Args&, const String*, Print&) {
  g_logoutReq = true;                     // main закроет окна и покажет экран входа
  return 0;
}

// ---------- Команди: Telegram ----------
int cmdTg(Args& a, const String*, Print& out) { return tgCommand(a, out); }

const std::vector<Builtin>& builtins() {
  static const std::vector<Builtin> t = {
    {"ls",       cmdLs,       "[-al] [dir...]",                 "list directory contents"},
    {"cd",       cmdCd,       "[dir]",                          "change directory"},
    {"pwd",      cmdPwd,      "",                               "print working directory"},
    {"cat",      cmdCat,      "[-n] [file...]",                 "concatenate files to output"},
    {"head",     cmdHead,     "[-n N] [file]",                  "output first lines"},
    {"tail",     cmdTail,     "[-n N] [file]",                  "output last lines"},
    {"wc",       cmdWc,       "[-lwc] [file]",                  "count lines, words, bytes"},
    {"echo",     cmdEcho,     "[-n] [arg...]",                  "print arguments"},
    {"printf",   cmdPrintf,   "format [arg...]",                "formatted output (%s %d %% \\n \\t)"},
    {"grep",     cmdGrep,     "[-inv] pattern [file]",          "print lines containing text"},
    {"sort",     cmdSort,     "[-r] [file]",                    "sort lines"},
    {"uniq",     cmdUniq,     "[file]",                         "remove adjacent duplicate lines"},
    {"tee",      cmdTee,      "[-a] file",                      "copy input to file and output"},
    {"seq",      cmdSeq,      "[first] last",                   "print number sequence"},
    {"touch",    cmdTouch,    "file...",                        "create empty file"},
    {"mkdir",    cmdMkdir,    "[-p] dir...",                    "make directories"},
    {"rm",       cmdRm,       "[-rf] path...",                  "remove files or directories"},
    {"cp",       cmdCp,       "src dst",                        "copy a file"},
    {"mv",       cmdMv,       "src dst",                        "move or rename a file"},
    {"find",     cmdFind,     "[dir] [-name pattern]",          "search for files"},
    {"tree",     cmdTree,     "[dir]",                          "show directory tree"},
    {"du",       cmdDu,       "[-h] [path]",                    "estimate file space usage"},
    {"df",       cmdDf,       "",                               "show SD card space"},
    {"basename", cmdBasename, "path",                           "strip directory from path"},
    {"dirname",  cmdDirname,  "path",                           "strip last path component"},
    {"wifi",     cmdWifi,     "scan | connect SSID [PASS] | status | disconnect", "Wi-Fi control"},
    {"ip",       cmdIp,       "addr",                           "show network addresses"},
    {"curl",     cmdCurl,     "url",                            "fetch URL and print body"},
    {"browse",   cmdBrowse,   "[url]",                          "open browser window"},
    {"media",    cmdMedia,    "",                               "open microSD MJPEG/WAV media player"},
    {"files",    cmdFiles,    "[dir]",                          "open file manager window"},
    {"run",      cmdRun,      "file.lua",                       "run script in a window"},
    {"sh",       cmdSh,       "script",                         "run commands from a file"},
    {"export",   cmdExport,   "[NAME=value]",                   "set environment variable"},
    {"unset",    cmdUnset,    "NAME...",                        "remove environment variable"},
    {"env",      cmdEnv,      "",                               "print environment"},
    {"printenv", cmdEnv,      "",                               "print environment"},
    {"uname",    cmdUname,    "[-a]",                           "print system information"},
    {"hostname", cmdHostname, "",                               "show host name"},
    {"whoami",   cmdWhoami,   "",                               "print current user"},
    {"id",       cmdId,       "",                               "print user and group ids"},
    {"uptime",   cmdUptime,   "",                               "time since boot"},
    {"date",     cmdDate,     "",                               "print date and time"},
    {"free",     cmdFree,     "",                               "show memory usage"},
    {"sleep",    cmdSleep,    "seconds",                        "pause for N seconds (max 30)"},
    {"true",     cmdTrue,     "",                               "exit with status 0"},
    {"false",    cmdFalse,    "",                               "exit with status 1"},
    {"which",    cmdWhich,    "name...",                        "locate a command"},
    {"type",     cmdType,     "name...",                        "describe a command"},
    {"help",     cmdHelp,     "[command]",                      "list commands or show usage"},
    {"man",      cmdMan,      "command",                        "show command manual"},
    {"history",  nullptr,     "",                               "show command history (terminal)"},
    {"clear",    nullptr,     "",                               "clear the screen (terminal)"},
    {"exit",     nullptr,     "",                               "close terminal (terminal)"},
    {"reboot",   cmdReboot,   "",                               "restart the device"},
    {"poweroff", cmdPoweroff, "",                               "deep sleep, press RESET to wake"},
    {"su",       cmdSu,       "[user]",                         "switch user (asks password)"},
    {"sudo",     cmdSudo,     "command...",                     "run command as root"},
    {"passwd",   cmdPasswd,   "[user]",                         "change password"},
    {"useradd",  cmdUseradd,  "name",                           "create user (root only)"},
    {"userdel",  cmdUserdel,  "name",                           "remove user (root only)"},
    {"users",    cmdUsers,    "",                               "list users"},
    {"login",    cmdLogin,    "",                               "open login window"},
    {"logout",   cmdLogout,   "",                               "lock session, show login"},
    {"tg",       cmdTg,       "token T | chat ID | send TEXT | to ID TEXT | inbox | poll | status | open", "Telegram Bot API client"},
  };
  return t;
}

const Builtin* findBuiltin(const String& name) {
  for (auto& b : builtins())
    if (name == b.name) return &b;
  return nullptr;
}

}  // namespace

// ---------- Публичный API ----------
void shellInit() {
  shellSwitchUser("root", false);
  envSet("HOSTNAME", "cardputer");
  envSet("SHELL", "/bin/bash");
  envSet("PATH", "/apps");
  envSet("TERM", "cardputer");
  envSet("PWD", "/");
  g_cwd = "/";
}

// Нормализует путь относительно текущей папки: убирает . и ..
String fsResolve(const String& path) {
  String full = path.startsWith("/") ? path : g_cwd + "/" + path;
  std::vector<String> parts;
  unsigned start = 0;
  while (start <= full.length()) {
    int slash = full.indexOf('/', start);
    unsigned end = (slash < 0) ? full.length() : (unsigned)slash;
    String seg = full.substring(start, end);
    start = end + 1;
    if (seg.isEmpty() || seg == ".") continue;
    if (seg == "..") {
      if (!parts.empty()) parts.pop_back();
      continue;
    }
    parts.push_back(seg);
  }
  String out;
  for (const String& p : parts) out += "/" + p;
  return out.isEmpty() ? String("/") : out;
}

String shellCwd() { return g_cwd; }

String shellPrompt() {
  String d = (g_cwd == envGet("HOME")) ? String("~") : g_cwd;
  return envGet("USER") + "@" + envGet("HOSTNAME") + ":" + d + "$ ";
}

// Варианты для Tab: имена команд в первом слове, иначе имена файлов
void shellComplete(const String& word, bool cmdPos, std::vector<String>& out) {
  out.clear();
  if (cmdPos) {
    for (auto& b : builtins())
      if (String(b.name).startsWith(word)) out.push_back(String(b.name));
    return;
  }
  int slash = word.lastIndexOf('/');
  String dirTyped = (slash >= 0) ? word.substring(0, slash + 1) : String();
  String pref = (slash >= 0) ? word.substring(slash + 1) : word;
  String real = fsResolve(dirTyped.isEmpty() ? String(".") : dirTyped);
  for (auto& e : listDir(real)) {
    if (e.name.startsWith(pref)) out.push_back(dirTyped + e.name + (e.dir ? "/" : ""));
  }
}

// Смена пользователя: переменные окружения и (по желанию) домашняя папка
void shellSwitchUser(const String& name, bool toHome) {
  sessionSetUser(name);
  String home = userHome(name);
  envSet("USER", name);
  envSet("HOME", home);
  if (toHome) {
    g_cwd = home;
    envSet("PWD", home);
  }
}

// sudo: выполнить строку от root и вернуть прежнего пользователя
int shellRunAsRoot(const String& cmd, Print& out) {
  String saved = sessionUser();
  shellSwitchUser("root", false);
  int st = shellExec(cmd, out);
  shellSwitchUser(saved, false);
  return st;
}

PwRequest shellTakePwRequest() {
  PwRequest r = g_pwReq;
  g_pwReq = PwRequest();
  return r;
}

bool shellTakeLogout() {
  bool v = g_logoutReq;
  g_logoutReq = false;
  return v;
}

int shellExec(const String& line, Print& out) {
  std::vector<String> parts;
  std::vector<int> ops;
  splitTop(line, parts, ops);
  const size_t n = parts.size();
  size_t start = 0;
  int prevOp = 1;
  int status = 0;
  for (size_t k = 0; k < n; k++) {
    int op = (k + 1 == n) ? 1 : ops[k];
    if (op == 0) continue;                              // '|' — конвейер продолжается
    bool skip = (prevOp == 2 && g_status != 0) || (prevOp == 3 && g_status == 0);
    if (!skip) {
      std::vector<String> stages(parts.begin() + start, parts.begin() + k + 1);
      status = runPipeline(stages, out);
      g_status = status;
    }
    prevOp = op;
    start = k + 1;
  }
  return status;
}
