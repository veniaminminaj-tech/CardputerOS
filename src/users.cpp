// Пользователи и права: /etc/passwd на SD, пароли SHA-256 с солью, сессия, проверка доступа
#include <SD.h>
#include <string.h>
#include <esp_system.h>
#include "mbedtls/sha256.h"
#include "core.h"

namespace {

struct UserRec {
  String name;
  String salt;     // hex, 16 символов
  String hash;     // hex, 64 символа
  uint32_t uid = 0;
  String home;
};

const char* const kPasswd = "/etc/passwd";
const int kRounds = 1000;

std::vector<UserRec> g_users;
String g_session = "root";
bool g_loggedIn = false;

String toHex(const uint8_t* b, size_t n) {
  static const char* hx = "0123456789abcdef";
  String s;
  for (size_t i = 0; i < n; i++) {
    s += hx[b[i] >> 4];
    s += hx[b[i] & 0x0F];
  }
  return s;
}

void sha256(const uint8_t* in, size_t n, uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, in, n);
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

// Растяжение пароля: SHA-256 от соль+пароль, затем ещё kRounds раундов
String hashPassword(const String& salt, const String& pw) {
  String seed = salt + pw;
  uint8_t h[32];
  sha256((const uint8_t*)seed.c_str(), seed.length(), h);
  for (int r = 0; r < kRounds; r++) {
    uint8_t next[32];
    sha256(h, sizeof(h), next);
    memcpy(h, next, sizeof(h));
  }
  return toHex(h, sizeof(h));
}

String newSalt() {
  uint8_t r[8];
  for (int i = 0; i < 8; i += 4) {
    uint32_t v = esp_random();
    memcpy(r + i, &v, 4);
  }
  return toHex(r, sizeof(r));
}

// Строка вида name:salt:hash:uid:home
void parseLine(const String& l) {
  if (l.isEmpty()) return;
  std::vector<String> f;
  String cur;
  for (unsigned i = 0; i < l.length(); i++) {
    if (l.charAt(i) == ':') { f.push_back(cur); cur = ""; }
    else cur += l.charAt(i);
  }
  f.push_back(cur);
  if (f.size() != 5) return;
  UserRec u;
  u.name = f[0];
  u.salt = f[1];
  u.hash = f[2];
  u.uid = (uint32_t)f[3].toInt();
  u.home = f[4];
  g_users.push_back(u);
}

bool dbLoad() {
  g_users.clear();
  File f = SD.open(kPasswd, FILE_READ);
  if (!f) return false;
  String line;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\n' || c == '\r') { parseLine(line); line = ""; }
    else line += c;
  }
  parseLine(line);
  f.close();
  return !g_users.empty();
}

bool dbSave() {
  SD.mkdir("/etc");
  SD.remove(kPasswd);
  File f = SD.open(kPasswd, FILE_APPEND);
  if (!f) return false;
  for (auto& u : g_users) {
    f.println(u.name + ":" + u.salt + ":" + u.hash + ":" + String(u.uid) + ":" + u.home);
  }
  f.close();
  return true;
}

UserRec* findUser(const String& name) {
  for (auto& u : g_users)
    if (u.name == name) return &u;
  return nullptr;
}

}  // namespace

// ---------- База пользователей ----------
bool userDbInit() {
  SD.mkdir("/home");
  SD.mkdir("/root");
  if (!dbLoad()) {                        // первый запуск: root с паролем по умолчанию
    UserRec r;
    r.name = "root";
    r.salt = newSalt();
    r.hash = hashPassword(r.salt, "root");
    r.uid = 0;
    r.home = "/root";
    g_users.push_back(r);
    dbSave();
  }
  for (auto& u : g_users)
    if (!SD.exists(u.home)) SD.mkdir(u.home);
  return true;
}

bool userValidName(const String& n) {
  if (n.isEmpty() || n.length() > 16 || n == "root") return false;
  for (unsigned i = 0; i < n.length(); i++) {
    char c = n.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || (i > 0 && c == '-');
    if (!ok) return false;
  }
  return true;
}

bool userExists(const String& name) { return findUser(name) != nullptr; }

uint32_t userUid(const String& name) {
  UserRec* u = findUser(name);
  return u ? u->uid : 0xFFFFFFFFu;
}

String userHome(const String& name) {
  UserRec* u = findUser(name);
  if (u) return u->home;
  return "/home/" + name;
}

bool userCheck(const String& name, const String& pw) {
  UserRec* u = findUser(name);
  if (!u || pw.isEmpty()) return false;
  return hashPassword(u->salt, pw) == u->hash;
}

bool userAdd(const String& name, const String& pw) {
  if (!userValidName(name) || userExists(name) || pw.isEmpty()) return false;
  uint32_t uid = 1000;
  for (auto& u : g_users)
    if (u.uid >= uid) uid = u.uid + 1;
  UserRec r;
  r.name = name;
  r.salt = newSalt();
  r.hash = hashPassword(r.salt, pw);
  r.uid = uid;
  r.home = "/home/" + name;
  SD.mkdir("/home");
  SD.mkdir(r.home);
  g_users.push_back(r);
  return dbSave();
}

bool userDel(const String& name) {
  if (name == "root") return false;
  for (size_t i = 0; i < g_users.size(); i++) {
    if (g_users[i].name == name) {
      g_users.erase(g_users.begin() + i);
      return dbSave();
    }
  }
  return false;
}

bool userSetPassword(const String& name, const String& pw) {
  UserRec* u = findUser(name);
  if (!u || pw.isEmpty()) return false;
  u->salt = newSalt();
  u->hash = hashPassword(u->salt, pw);
  return dbSave();
}

std::vector<String> userNames() {
  std::vector<String> v;
  for (auto& u : g_users) v.push_back(u.name);
  return v;
}

// ---------- Сессия ----------
String sessionUser() { return g_session; }
bool sessionIsRoot() { return g_session == "root"; }
bool sessionLoggedIn() { return g_loggedIn; }
void sessionSetUser(const String& name) { g_session = name; }
void sessionSetLoggedIn(bool on) { g_loggedIn = on; }

// ---------- Права ----------
// Читать можно всё, кроме /root и /etc/passwd (для обычных пользователей)
bool canRead(const String& abs) {
  if (sessionIsRoot()) return true;
  if (abs == "/root" || abs.startsWith("/root/")) return false;
  if (abs == "/etc/passwd") return false;
  return true;
}

// Писать можно только в свою домашнюю папку (root — везде)
bool canWrite(const String& abs) {
  if (sessionIsRoot()) return true;
  String home = userHome(g_session);
  return abs == home || abs.startsWith(home + "/");
}

String accessError(const String& path) {
  return canRead(fsResolve(path)) ? String("No such file or directory") : String("Permission denied");
}

bool requireRoot(Print& out, const String& cmd) {
  if (sessionIsRoot()) return true;
  out.println(cmd + ": Permission denied (root required)");
  return false;
}
