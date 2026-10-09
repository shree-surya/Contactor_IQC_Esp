#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#if __has_include(<esp_random.h>)
#include <esp_random.h>
#endif
#include <sys/time.h>
#include <time.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>

#include "gsheets.h"
#include "app_data.h"
#include "config.h"
#include "google_roots.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define GS_CONFIGURED 1
#else
#define GS_CONFIGURED 0
#define GSHEET_ID ""
#define GSA_CLIENT_EMAIL ""
#define GSA_PRIVATE_KEY ""
#endif

static const char *QUEUE_FILE = "/queue_v2.txt";  // v2 = one row per unit
static const char *QUEUE_TMP = "/queue_v2.tmp";
static const char *CACHE_FILE = "/sheet_cache.json";
static const char *TZ_INDIA = "IST-5:30";
static const int UPLOAD_BATCH = 10;
static const uint32_t RETRY_MS = 30000;

static SemaphoreHandle_t s_mtx;
static int s_pending = 0;
static bool s_syncReq = true;  // sync once after boot
static bool s_syncedOnce = false;  // Specs/Operators read successfully since boot
static bool s_syncErr = false;     // last sync attempt failed
static bool s_hasCache = false;    // a synced copy exists in flash
static uint32_t s_nextTry = 0;     // retry back-off
static volatile bool s_hold = false;  // no network work while a test runs
static char s_status[64] = "Starting";
static char s_lastSync[24] = "-";

// Synced data waiting to be applied by the UI thread
static ModelSpec s_newModels[MAX_MODELS];
static int s_newModelCount = 0;
static char s_newOps[MAX_OPERATORS][NAME_MAX_LEN + 1];
static char s_newPass[MAX_OPERATORS][PASS_MAX_LEN + 1];
static int s_newOpCount = 0;
static bool s_newData = false;

static String s_token;
static uint32_t s_tokenExp = 0;

struct Lock {
  Lock() { xSemaphoreTake(s_mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mtx); }
};

static void set_status(const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Lock l;
  if (strcmp(buf, s_status) != 0) {
    strlcpy(s_status, buf, sizeof(s_status));
    Serial.printf("[sheets] %s\n", s_status);
  }
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

bool gsheets_time_valid() { return time(nullptr) > 1700000000; }

void gsheets_now_str(char *buf, size_t len) {
  if (!gsheets_time_valid()) {
    strlcpy(buf, "NO TIME", len);
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  strftime(buf, len, "%Y-%m-%d %H:%M:%S", &t);
}

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// Fallback when NTP (UDP 123) is blocked: take the time from Google's
// HTTP "Date" header, e.g. "Tue, 07 Oct 2026 12:00:00 GMT".
static bool time_from_http() {
  WiFiClientSecure client;
  client.setCACert(GOOGLE_ROOT_CAS);
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, "https://oauth2.googleapis.com/")) return false;
  const char *keys[] = {"Date"};
  http.collectHeaders(keys, 1);
  http.GET();
  String date = http.header("Date");
  http.end();

  int day, year, hh, mm, ss;
  char mon[4];
  if (sscanf(date.c_str(), "%*3s, %d %3s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6) return false;
  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *p = strstr(months, mon);
  if (!p) return false;
  unsigned m = (p - months) / 3 + 1;
  time_t t = (time_t)(days_from_civil(year, m, day) * 86400 + hh * 3600 + mm * 60 + ss);
  struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
  Serial.printf("[sheets] clock set from HTTP Date: %s\n", date.c_str());
  return true;
}

// ---------------------------------------------------------------------------
// Service-account login (JWT signed with RS256 -> OAuth access token)
// ---------------------------------------------------------------------------

static String b64url(const uint8_t *data, size_t len) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, data, len);
  char *buf = (char *)malloc(olen + 1);
  if (!buf) return "";
  mbedtls_base64_encode((unsigned char *)buf, olen + 1, &olen, data, len);
  buf[olen] = '\0';
  String s(buf);
  free(buf);
  s.replace('+', '-');
  s.replace('/', '_');
  while (s.endsWith("=")) s.remove(s.length() - 1);
  return s;
}

static int rng(void *, unsigned char *out, size_t len) {
  esp_fill_random(out, len);
  return 0;
}

static bool make_jwt(String &jwt) {
  static const char HEADER[] = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
  static const char KEY[] = GSA_PRIVATE_KEY;
  time_t now = time(nullptr);
  char claims[400];
  snprintf(claims, sizeof(claims),
           "{\"iss\":\"%s\",\"scope\":\"https://www.googleapis.com/auth/spreadsheets\","
           "\"aud\":\"https://oauth2.googleapis.com/token\",\"iat\":%ld,\"exp\":%ld}",
           GSA_CLIENT_EMAIL, (long)now, (long)now + 3600);
  String signing = b64url((const uint8_t *)HEADER, strlen(HEADER)) + "." +
                   b64url((const uint8_t *)claims, strlen(claims));

  uint8_t hash[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t *)signing.c_str(), signing.length(), hash);

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int r = mbedtls_pk_parse_key(&pk, (const uint8_t *)KEY, sizeof(KEY), nullptr, 0);
  if (r != 0) {
    Serial.printf("[sheets] private key parse error -0x%04x\n", -r);
    mbedtls_pk_free(&pk);
    return false;
  }
  uint8_t sig[512];
  size_t sigLen = 0;
  r = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, &sigLen, rng, nullptr);
  mbedtls_pk_free(&pk);
  if (r != 0) {
    Serial.printf("[sheets] sign error -0x%04x\n", -r);
    return false;
  }
  jwt = signing + "." + b64url(sig, sigLen);
  return true;
}

static bool get_token() {
  if (s_token.length() && (int32_t)(s_tokenExp - millis()) > 120000) return true;
  set_status("Signing in");
  String jwt;
  if (!make_jwt(jwt)) {
    set_status("Error: bad key in secrets.h");
    return false;
  }
  WiFiClientSecure client;
  client.setCACert(GOOGLE_ROOT_CAS);
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, "https://oauth2.googleapis.com/token")) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=" + jwt);
  String body = http.getString();
  http.end();
  if (code != 200) {
    Serial.printf("[sheets] token HTTP %d: %s\n", code, body.c_str());
    set_status("Error: login failed (%d)", code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    set_status("Error: bad login reply");
    return false;
  }
  s_token = doc["access_token"].as<String>();
  s_tokenExp = millis() + (uint32_t)(doc["expires_in"] | 3600) * 1000UL;
  return s_token.length() > 0;
}

static int api(const char *method, const String &url, const String &body, String &resp) {
  WiFiClientSecure client;
  client.setCACert(GOOGLE_ROOT_CAS);
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, url)) return -1;
  http.addHeader("Authorization", "Bearer " + s_token);
  int code;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
  }
  resp = http.getString();
  http.end();
  if (code == 401) s_token = "";  // expired: log in again next time
  if (code != 200) Serial.printf("[sheets] %s HTTP %d: %.300s\n", method, code, resp.c_str());
  return code;
}

// ---------------------------------------------------------------------------
// Specs + Operators
// ---------------------------------------------------------------------------

static float cell_float(JsonVariant v) {
  if (v.isNull()) return NAN;
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    char *end;
    float f = strtof(s, &end);
    return end != s ? f : NAN;  // "NA", "" -> not checked
  }
  return v.as<float>();
}

static uint16_t cell_uint(JsonVariant v, uint16_t def) {
  if (v.isNull()) return def;
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    return *s ? (uint16_t)atoi(s) : def;
  }
  long n = v.as<long>();
  return n > 0 && n < 65536 ? (uint16_t)n : def;
}

static int parse_models(JsonArray rows, ModelSpec *out) {
  int n = 0;
  for (JsonArray row : rows) {
    if (n >= MAX_MODELS) break;
    String name = row[0].as<String>();
    name.trim();
    if (!name.length() || name == "null") continue;
    ModelSpec m;
    memset(&m, 0, sizeof(m));
    strlcpy(m.name, name.c_str(), sizeof(m.name));
    m.inrushLsl = cell_float(row[1]);
    m.inrushUsl = cell_float(row[2]);
    m.contLsl = cell_float(row[3]);
    m.contUsl = cell_float(row[4]);
    if (isnan(m.contLsl) || isnan(m.contUsl)) continue;  // continuous limits are mandatory
    app_data_default_timings(m);
    m.inrushWinMs = cell_uint(row[5], m.inrushWinMs);
    m.settleMs = cell_uint(row[6], m.settleMs);
    m.avgMs = cell_uint(row[7], m.avgMs);
    m.releaseMs = cell_uint(row[8], m.releaseMs);
    m.cycles = (uint8_t)cell_uint(row[9], m.cycles);
    if (m.cycles > MAX_CYCLES) m.cycles = MAX_CYCLES;
    m.cycleGapMs = cell_uint(row[10], m.cycleGapMs);
    m.staggerMs = cell_uint(row[11], m.staggerMs);
    out[n++] = m;
  }
  return n;
}

// Operators tab: column A = name, column B = password (empty = no password)
static int parse_operators(JsonArray rows, char out[][NAME_MAX_LEN + 1], char pass[][PASS_MAX_LEN + 1]) {
  int n = 0;
  for (JsonArray row : rows) {
    if (n >= MAX_OPERATORS) break;
    String name = row[0].as<String>();
    name.trim();
    if (!name.length() || name == "null") continue;
    String pw = row[1].isNull() ? String("") : row[1].as<String>();
    pw.trim();
    strlcpy(out[n], name.c_str(), NAME_MAX_LEN + 1);
    strlcpy(pass[n], pw.c_str(), PASS_MAX_LEN + 1);
    n++;
  }
  return n;
}

// Cache format = the two "values" arrays exactly as the Sheets API returns them
static void save_cache(JsonArray specRows, JsonArray opRows) {
  JsonDocument doc;
  doc["specs"] = specRows;
  doc["operators"] = opRows;
  doc["synced"] = s_lastSync;
  Lock l;
  File f = LittleFS.open(CACHE_FILE, "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

static void load_cache() {
  File f = LittleFS.open(CACHE_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  static ModelSpec models[MAX_MODELS];
  static char ops[MAX_OPERATORS][NAME_MAX_LEN + 1];
  static char pass[MAX_OPERATORS][PASS_MAX_LEN + 1];
  int nm = parse_models(doc["specs"].as<JsonArray>(), models);
  int no = parse_operators(doc["operators"].as<JsonArray>(), ops, pass);
  app_data_set_models(models, nm);
  app_data_set_operators(ops, pass, no);
  s_hasCache = nm > 0;
  strlcpy(s_lastSync, doc["synced"] | "-", sizeof(s_lastSync));
  Serial.printf("[sheets] cache loaded: %d models, %d operators (synced %s)\n", nm, no, s_lastSync);
}

static bool do_sync() {
  set_status("Reading Specs");
  String url = String("https://sheets.googleapis.com/v4/spreadsheets/") + GSHEET_ID +
               "/values:batchGet?ranges=Specs%21A2%3AL9&ranges=Operators%21A2%3AB11&valueRenderOption=UNFORMATTED_VALUE";
  String resp;
  int code = api("GET", url, "", resp);
  if (code != 200) {
    set_status(code == 404 ? "Error: Sheet ID not found" : code == 403 ? "Error: Sheet not shared" : "Error: read failed (%d)", code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    set_status("Error: bad Sheet reply");
    return false;
  }
  JsonArray specRows = doc["valueRanges"][0]["values"].as<JsonArray>();
  JsonArray opRows = doc["valueRanges"][1]["values"].as<JsonArray>();

  static ModelSpec models[MAX_MODELS];
  static char ops[MAX_OPERATORS][NAME_MAX_LEN + 1];
  static char pass[MAX_OPERATORS][PASS_MAX_LEN + 1];
  int nm = parse_models(specRows, models);
  int no = parse_operators(opRows, ops, pass);
  if (nm == 0) {
    set_status("Error: no valid rows in Specs");
    return false;
  }
  char now[24];
  gsheets_now_str(now, sizeof(now));
  {
    Lock l;
    memcpy(s_newModels, models, sizeof(ModelSpec) * nm);
    s_newModelCount = nm;
    for (int i = 0; i < no; i++) {
      strlcpy(s_newOps[i], ops[i], NAME_MAX_LEN + 1);
      strlcpy(s_newPass[i], pass[i], PASS_MAX_LEN + 1);
    }
    s_newOpCount = no;
    s_newData = true;
    s_syncedOnce = true;
    s_syncErr = false;
    s_hasCache = true;
    strlcpy(s_lastSync, now, sizeof(s_lastSync));
  }
  save_cache(specRows, opRows);
  Serial.printf("[sheets] synced %d models, %d operators\n", nm, no);
  return true;
}

// ---------------------------------------------------------------------------
// Results queue (one JSON array per line in /queue.txt)
// ---------------------------------------------------------------------------

// Results row: S.No, Serial, Timestamp, Operator, Model, Channel, Overall,
// Fail Step, then 7 columns per cycle (Open, Inrush A, Inrush P/F, Cont A,
// Cont P/F, Continuity, Release). A leading ' makes Sheets keep the text
// exactly as sent (no date/number guessing).
void gsheets_enqueue(const ResultRow &r) {
  char ts[24], ch[4];
  gsheets_now_str(ts, sizeof(ts));
  snprintf(ch, sizeof(ch), "%d", r.ch);

  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  a.add("=ROW()-1");
  a.add(String("'") + r.serial);
  a.add(strcmp(ts, "NO TIME") == 0 ? String(ts) : String("'") + ts);  // NO TIME is replaced at upload
  a.add(r.op);
  a.add(r.model);
  a.add(ch);
  a.add(r.overall);
  a.add(r.failStep);
  for (int i = 0; i < r.cycles && i < MAX_CYCLES; i++) {
    const CycleResult &c = r.cyc[i];
    a.add(c.open);
    a.add(c.inrushA);
    a.add(c.inrushPF);
    a.add(c.contA);
    a.add(c.contPF);
    a.add(c.contin);
    a.add(c.release);
  }
  String line;
  serializeJson(doc, line);

  Lock l;
  File f = LittleFS.open(QUEUE_FILE, "a");
  if (f) {
    f.println(line);
    f.close();
    s_pending++;
  }
}

static int count_queue() {
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return 0;
  int n = 0;
  while (f.available()) {
    if (f.readStringUntil('\n').length() > 1) n++;
  }
  f.close();
  return n;
}

// Drops the first n lines of the queue file
static void queue_drop(int n) {
  File in = LittleFS.open(QUEUE_FILE, "r");
  File out = LittleFS.open(QUEUE_TMP, "w");
  int i = 0, kept = 0;
  while (in && in.available()) {
    String line = in.readStringUntil('\n');
    if (line.length() <= 1) continue;
    if (i++ < n) continue;
    out.println(line);
    kept++;
  }
  if (in) in.close();
  if (out) out.close();
  LittleFS.remove(QUEUE_FILE);
  LittleFS.rename(QUEUE_TMP, QUEUE_FILE);
  s_pending = kept;
}

static bool upload_batch() {
  String lines[UPLOAD_BATCH];
  int n = 0;
  {
    Lock l;
    File f = LittleFS.open(QUEUE_FILE, "r");
    while (f && f.available() && n < UPLOAD_BATCH) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length()) lines[n++] = line;
    }
    if (f) f.close();
  }
  if (n == 0) {
    Lock l;
    s_pending = 0;
    return true;
  }

  char now[24];
  gsheets_now_str(now, sizeof(now));
  String offlineStamp = String("'") + now + "*";  // * = logged offline, this is the upload time

  JsonDocument body;
  JsonArray values = body["values"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonDocument row;
    if (deserializeJson(row, lines[i]) || !row.is<JsonArray>()) continue;  // skip a corrupt line
    if (row[2] == "NO TIME") row[2] = offlineStamp;
    values.add(row.as<JsonArray>());
  }
  String payload;
  serializeJson(body, payload);

  set_status("Uploading %d row(s)", n);
  String url = String("https://sheets.googleapis.com/v4/spreadsheets/") + GSHEET_ID +
               "/values/Results%21A1:append?valueInputOption=USER_ENTERED&insertDataOption=INSERT_ROWS";
  String resp;
  int code = api("POST", url, payload, resp);
  if (code != 200) {
    set_status(code == 403 ? "Error: Sheet not shared" : "Error: upload failed (%d)", code);
    return false;
  }
  Lock l;
  queue_drop(n);
  return true;
}

// ---------------------------------------------------------------------------
// Background task
// ---------------------------------------------------------------------------

static void task(void *) {
  bool ntpStarted = false;
  uint32_t lastHttpTime = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (WiFi.status() != WL_CONNECTED) {
      set_status(GS_CONFIGURED ? "Waiting for WiFi" : "Not configured (no secrets.h)");
      continue;
    }
    // TLS uploads load the CPU and PSRAM bus that also feed the RGB panel, so
    // nothing is sent until the batch ends.
    if (s_hold) {
      set_status("Paused (test running)");
      continue;
    }
    if (!ntpStarted) {
      configTzTime(TZ_INDIA, "time.google.com", "pool.ntp.org");
      ntpStarted = true;
    }
    if (!gsheets_time_valid()) {
      set_status("Getting time");
      if (millis() - lastHttpTime > 15000) {
        lastHttpTime = millis();
        time_from_http();
      }
      continue;
    }
    if (!GS_CONFIGURED) {
      set_status("Not configured (no secrets.h)");
      continue;
    }
    if ((int32_t)(millis() - s_nextTry) < 0) continue;

    bool wantSync, haveRows;
    {
      Lock l;
      wantSync = s_syncReq;
      haveRows = s_pending > 0;
    }
    if (!wantSync && !haveRows) {
      set_status("OK");
      continue;
    }
    if (!get_token()) {
      Lock l;
      if (wantSync) s_syncErr = true;
      s_nextTry = millis() + RETRY_MS;
      continue;
    }
    if (wantSync) {
      bool ok = do_sync();
      Lock l;
      if (ok) {
        s_syncReq = false;
      } else {
        s_syncErr = true;
        s_nextTry = millis() + RETRY_MS;
        continue;
      }
    }
    if (haveRows && !upload_batch()) {
      Lock l;
      s_nextTry = millis() + RETRY_MS;
    }
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void gsheets_begin() {
  s_mtx = xSemaphoreCreateMutex();
  setenv("TZ", TZ_INDIA, 1);
  tzset();
  if (!LittleFS.begin(true)) {
    Serial.println("[sheets] LittleFS mount failed");
  }
  load_cache();
  s_pending = count_queue();
  Serial.printf("[sheets] %s, %d row(s) waiting\n", GS_CONFIGURED ? "configured" : "NOT configured", s_pending);
  xTaskCreatePinnedToCore(task, "gsheets", 16384, nullptr, 1, nullptr, 0);
}

void gsheets_request_sync() {
  Lock l;
  s_syncReq = true;
  s_nextTry = 0;
}

void gsheets_set_hold(bool hold) { s_hold = hold; }

void gsheets_flush_now() {
  Lock l;
  s_nextTry = 0;
}

GsState gsheets_state() {
  if (!GS_CONFIGURED) return GS_OFF;
  if (WiFi.status() != WL_CONNECTED) return GS_NO_WIFI;
  Lock l;
  if (s_syncedOnce && !s_syncReq) return GS_SYNCED;
  if (s_syncErr) return GS_ERROR;
  return GS_SYNCING;
}

bool gsheets_has_cache() {
  Lock l;
  return s_hasCache;
}

bool gsheets_has_new_data() {
  Lock l;
  return s_newData;
}

void gsheets_apply_new_data() {
  Lock l;
  if (!s_newData) return;
  app_data_set_models(s_newModels, s_newModelCount);
  app_data_set_operators(s_newOps, s_newPass, s_newOpCount);
  s_newData = false;
}

int gsheets_pending_count() {
  Lock l;
  return s_pending;
}

bool gsheets_configured() { return GS_CONFIGURED; }

void gsheets_status(char *buf, size_t len) {
  Lock l;
  strlcpy(buf, s_status, len);
}

void gsheets_last_sync(char *buf, size_t len) {
  Lock l;
  strlcpy(buf, s_lastSync, len);
}
