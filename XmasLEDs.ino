#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "secrets.h"

#define USB_1 D7
#define USB_2 D8

struct PersistentSettings {
    uint32_t magic;
    uint16_t autoOnOffsetMinutes;
    uint8_t autoModeEnabled;
    uint8_t reserved;
};

const char* DEVICE_NAME = "NightLEDs";
const char* DEVICE_LABEL = "Night LEDs";
const char* NTP_SERVER = "europe.pool.ntp.org";
const char* OPEN_METEO_URL =
    "https://api.open-meteo.com/v1/forecast?latitude=46.20&longitude=6.15&daily=sunset&timezone=Europe%2FZurich&forecast_days=1";

const uint32_t SETTINGS_MAGIC = 0x4E4C4453UL;
const size_t EEPROM_SIZE_BYTES = 32;
const unsigned long HTTP_CONNECTION_KEEPALIVE_MS = 2000;
const unsigned long NTP_SYNC_INTERVAL_MS = 5UL * 60UL * 1000UL;
const unsigned long SUNSET_REFRESH_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
const unsigned long SUNSET_RETRY_INTERVAL_MS = 60UL * 1000UL;
const unsigned long UI_AUTO_REFRESH_SECONDS = 30;

const uint8_t AUTO_RESET_HOUR = 3;
const uint16_t DEFAULT_SUNSET_OFFSET_MINUTES = 45;
const uint16_t MAX_SUNSET_OFFSET_MINUTES = 240;
const uint8_t SNOOZE_PRESET_COUNT = 5;
const uint16_t SNOOZE_PRESETS[SNOOZE_PRESET_COUNT] = {5, 10, 30, 60, 120};

const char otaAuthPin[] = OTA_AUTH_PIN;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, 3600, NTP_SYNC_INTERVAL_MS);
WiFiServer server(80);

bool outputsOn = false;
bool autoMode = true;
bool hasTimeSync = false;
bool hasSunsetData = false;
bool autoSuppressedToday = false;
bool softApMode = false;

uint16_t autoOnOffsetMinutes = DEFAULT_SUNSET_OFFSET_MINUTES;
uint16_t activeSnoozeMinutes = 0;

uint8_t sunsetHour = 0;
uint8_t sunsetMinute = 0;
int utcOffsetInSeconds = 3600;

unsigned long snoozeStartedAtMs = 0;
unsigned long lastNtpSyncMs = 0;
unsigned long lastSunsetSyncMs = 0;
unsigned long lastSunsetAttemptMs = 0;

uint32_t lastProcessedDayId = 0;
uint32_t lastSunsetDayId = 0;
uint32_t lastAutoOnDayId = 0;
uint32_t lastAutoOffDayId = 0;
int lastSunsetHttpCode = 0;

String localIpAddress = "";
String timezoneAbbreviation = "CET";

void handleOTA();
void connectWifi();
void loadPersistentSettings();
void savePersistentSettingsIfNeeded();
void primeTimeSources();
void refreshTimeClient();
void refreshSunsetIfNeeded();
bool requestSunsetTime();
void handleClientConnection();
void routeRequest(const String& requestPath, WiFiClient& client);
void sendPage(WiFiClient& client);
void sendRedirect(WiFiClient& client, const char* location = "/");
void setOutputs(bool turnOn);
void setManualOutput(bool turnOn);
void toggleManualOutput();
void startSnooze(uint16_t minutes);
void clearSnooze();
void updateSnoozeState();
void suspendAutoUntilTomorrow();
void handleDayTransition();
void applyAutoMode();
uint32_t getLocalDayId();
uint32_t getScheduleDayId();
bool isAutoSuppressed();
bool isInAutoWindow();
int getCurrentMinutesOfDay();
int getAutoStartMinutesOfDay();
bool isInAutoOffWindow();
String getCurrentTimeLabel();
String getSunsetTimeLabel();
String getAutoStartLabel();
String getSnoozeLabel();
String getNetworkLabel();
String getIpAddressLabel();
String getUptimeLabel();
String getRelativeAgeLabel(unsigned long timestampMs);
String getAutoStatusLabel();
String getOutputLabel();
String boolLabel(bool value);
String htmlEscaped(const String& value);
void setAutoModeEnabled(bool enabled);
void setAutoOnOffsetMinutes(uint16_t minutes);
String getRequestPath(const String& requestLine);
String getPathWithoutQuery(const String& requestPath);
int getQueryIntValue(const String& requestPath, const char* key, int fallbackValue);
void addMetric(WiFiClient& client, const String& label, const String& value);

void setup() {
    pinMode(USB_1, OUTPUT);
    pinMode(USB_2, OUTPUT);
    setOutputs(false);

    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println(F("Booting Night LEDs controller..."));

    loadPersistentSettings();

    connectWifi();
    server.begin();
    handleOTA();
    primeTimeSources();

    Serial.println(F("Setup complete."));
}

void loop() {
    ArduinoOTA.handle();
    refreshTimeClient();
    handleDayTransition();
    refreshSunsetIfNeeded();
    updateSnoozeState();
    applyAutoMode();
    handleClientConnection();
}

void handleOTA() {
    ArduinoOTA.setHostname(DEVICE_NAME);
    ArduinoOTA.setPassword(otaAuthPin);
    ArduinoOTA.onStart([]() {
        Serial.println(F("OTA update started."));
    });
    ArduinoOTA.onEnd([]() {
        Serial.println(F("\nOTA update completed."));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA progress: %u%%\r", (progress * 100U) / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA error [%u]\n", error);
    });
    ArduinoOTA.begin();
}

void connectWifi() {
    WiFi.mode(WIFI_STA);

    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);

    if (wifiManager.autoConnect(DEVICE_NAME)) {
        softApMode = false;
        localIpAddress = WiFi.localIP().toString();
        Serial.print(F("WiFi connected. IP: "));
        Serial.println(localIpAddress);
        return;
    }

    softApMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DEVICE_NAME);
    localIpAddress = WiFi.softAPIP().toString();

    Serial.println(F("WiFi not configured. Running in access point mode."));
    Serial.print(F("AP IP: "));
    Serial.println(localIpAddress);
}

void loadPersistentSettings() {
    EEPROM.begin(EEPROM_SIZE_BYTES);

    PersistentSettings settings;
    EEPROM.get(0, settings);

    if (settings.magic == SETTINGS_MAGIC &&
        settings.autoOnOffsetMinutes <= MAX_SUNSET_OFFSET_MINUTES &&
        (settings.autoModeEnabled == 0 || settings.autoModeEnabled == 1)) {
        autoOnOffsetMinutes = settings.autoOnOffsetMinutes;
        autoMode = (settings.autoModeEnabled == 1);
        return;
    }

    autoOnOffsetMinutes = DEFAULT_SUNSET_OFFSET_MINUTES;
    autoMode = true;
    savePersistentSettingsIfNeeded();
}

void savePersistentSettingsIfNeeded() {
    PersistentSettings currentSettings;
    currentSettings.magic = SETTINGS_MAGIC;
    currentSettings.autoOnOffsetMinutes = autoOnOffsetMinutes;
    currentSettings.autoModeEnabled = autoMode ? 1 : 0;
    currentSettings.reserved = 0;

    PersistentSettings storedSettings;
    EEPROM.get(0, storedSettings);

    if (storedSettings.magic == currentSettings.magic &&
        storedSettings.autoOnOffsetMinutes == currentSettings.autoOnOffsetMinutes &&
        storedSettings.autoModeEnabled == currentSettings.autoModeEnabled) {
        return;
    }

    EEPROM.put(0, currentSettings);
    EEPROM.commit();
}

void primeTimeSources() {
    timeClient.begin();

    if (WiFi.status() == WL_CONNECTED) {
        requestSunsetTime();
        if (timeClient.forceUpdate()) {
            hasTimeSync = true;
            lastNtpSyncMs = millis();
        }
    }

    if (hasTimeSync) {
        lastProcessedDayId = getScheduleDayId();
        if (hasSunsetData) {
            lastSunsetDayId = getLocalDayId();
        }
    }
}

void refreshTimeClient() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (timeClient.update()) {
        hasTimeSync = true;
        lastNtpSyncMs = millis();
    }
}

void refreshSunsetIfNeeded() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    const unsigned long nowMs = millis();
    const bool retryExpired = (nowMs - lastSunsetAttemptMs) >= SUNSET_RETRY_INTERVAL_MS;
    const bool periodicRefresh = !hasSunsetData || (nowMs - lastSunsetSyncMs) >= SUNSET_REFRESH_INTERVAL_MS;
    const bool newDayRefresh = hasTimeSync && hasSunsetData && (lastSunsetDayId != getLocalDayId());

    if ((periodicRefresh || newDayRefresh) && retryExpired) {
        requestSunsetTime();
    }
}

bool requestSunsetTime() {
    lastSunsetAttemptMs = millis();

    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setTimeout(6000);

    HTTPClient http;
    if (!http.begin(secureClient, OPEN_METEO_URL)) {
        lastSunsetHttpCode = -1;
        Serial.println(F("Failed to open Open-Meteo request."));
        return false;
    }

    http.setTimeout(6000);
    const int httpCode = http.GET();
    lastSunsetHttpCode = httpCode;

    if (httpCode <= 0) {
        Serial.printf("Open-Meteo request failed: %d\n", httpCode);
        http.end();
        return false;
    }

    DynamicJsonDocument jsonDoc(1536);
    const DeserializationError error = deserializeJson(jsonDoc, http.getString());
    http.end();

    if (error) {
        Serial.print(F("Open-Meteo JSON parse failed: "));
        Serial.println(error.c_str());
        return false;
    }

    const String sunsetIso = jsonDoc["daily"]["sunset"][0].as<String>();
    const int separatorIndex = sunsetIso.indexOf('T');
    if (separatorIndex < 0 || sunsetIso.length() < separatorIndex + 6) {
        Serial.println(F("Open-Meteo sunset payload missing ISO time."));
        return false;
    }

    const String timePart = sunsetIso.substring(separatorIndex + 1, separatorIndex + 6);
    sunsetHour = static_cast<uint8_t>(timePart.substring(0, 2).toInt());
    sunsetMinute = static_cast<uint8_t>(timePart.substring(3, 5).toInt());

    utcOffsetInSeconds = jsonDoc["utc_offset_seconds"] | utcOffsetInSeconds;
    timezoneAbbreviation = jsonDoc["timezone_abbreviation"] | timezoneAbbreviation;
    timeClient.setTimeOffset(utcOffsetInSeconds);

    hasSunsetData = true;
    lastSunsetSyncMs = millis();
    if (hasTimeSync) {
        lastSunsetDayId = getLocalDayId();
    }

    Serial.print(F("Sunset refreshed: "));
    Serial.println(getSunsetTimeLabel());
    return true;
}

void handleClientConnection() {
    WiFiClient client = server.available();
    if (!client) {
        return;
    }

    client.setTimeout(HTTP_CONNECTION_KEEPALIVE_MS);
    const String requestLine = client.readStringUntil('\r');
    client.readStringUntil('\n');

    while (client.connected()) {
        const String headerLine = client.readStringUntil('\n');
        if (headerLine == "\r" || headerLine.length() == 0) {
            break;
        }
    }

    routeRequest(getRequestPath(requestLine), client);
    client.stop();
}

void routeRequest(const String& requestPath, WiFiClient& client) {
    const String pathOnly = getPathWithoutQuery(requestPath);

    if (pathOnly == "/toggle") {
        toggleManualOutput();
        sendRedirect(client);
        return;
    }

    if (pathOnly == "/on") {
        setManualOutput(true);
        sendRedirect(client);
        return;
    }

    if (pathOnly == "/off") {
        setManualOutput(false);
        sendRedirect(client);
        return;
    }

    if (pathOnly == "/auto") {
        setAutoModeEnabled(!autoMode);
        sendRedirect(client);
        return;
    }

    if (pathOnly == "/snooze") {
        int requestedMinutes = getQueryIntValue(requestPath, "minutes", 0);
        if (requestedMinutes <= 0) {
            requestedMinutes = SNOOZE_PRESETS[0];
        }
        startSnooze(static_cast<uint16_t>(requestedMinutes));
        sendRedirect(client);
        return;
    }

    if (pathOnly == "/clear-snooze") {
        clearSnooze();
        sendRedirect(client);
        return;
    }

    if (pathOnly == "/set-offset") {
        int requestedOffset = getQueryIntValue(requestPath, "minutes", autoOnOffsetMinutes);
        if (requestedOffset < 0) {
            requestedOffset = 0;
        }
        if (requestedOffset > static_cast<int>(MAX_SUNSET_OFFSET_MINUTES)) {
            requestedOffset = MAX_SUNSET_OFFSET_MINUTES;
        }
        setAutoOnOffsetMinutes(static_cast<uint16_t>(requestedOffset));
        sendRedirect(client);
        return;
    }

    if (pathOnly == "/restart") {
        client.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nRestarting..."));
        delay(250);
        ESP.restart();
        return;
    }

    sendPage(client);
}

void sendPage(WiFiClient& client) {
    const String currentTime = getCurrentTimeLabel();
    const String sunsetTime = getSunsetTimeLabel();
    const String autoStart = getAutoStartLabel();
    const String snoozeLabel = getSnoozeLabel();
    const String outputLabel = getOutputLabel();
    const String autoStatusLabel = getAutoStatusLabel();
    const String networkLabel = getNetworkLabel();
    const String ipAddressLabel = getIpAddressLabel();
    const String snoozeHint = activeSnoozeMinutes > 0
        ? String("Turns off when the timer ends, even if midnight passes first.")
        : String("No active timer.");
    const String offsetLabel = String(autoOnOffsetMinutes) + " min";

    client.print(F("HTTP/1.1 200 OK\r\n"));
    client.print(F("Content-Type: text/html; charset=utf-8\r\n"));
    client.print(F("Cache-Control: no-store\r\n"));
    client.print(F("Connection: close\r\n\r\n"));

    client.print(F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"));
    client.print(F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"));
    client.print(F("<meta http-equiv=\"refresh\" content=\""));
    client.print(UI_AUTO_REFRESH_SECONDS);
    client.print(F("\">"));
    client.print(F("<title>"));
    client.print(DEVICE_LABEL);
    client.print(F("</title><style>"));
    client.print(F(
        "body{margin:0;font-family:Arial,sans-serif;background:radial-gradient(circle at top,#1e293b 0,#0f172a 42%,#020617 100%);"
        "color:#e2e8f0;min-height:100vh;}"
        ".wrap{max-width:980px;margin:0 auto;padding:20px 16px 40px;}"
        ".hero,.panel,.metric{background:rgba(15,23,42,.82);border:1px solid rgba(148,163,184,.22);border-radius:22px;"
        "box-shadow:0 18px 40px rgba(2,6,23,.34);backdrop-filter:blur(8px);}"
        ".hero{padding:24px 22px;margin-bottom:16px;}"
        ".eyebrow{font-size:12px;letter-spacing:.18em;text-transform:uppercase;color:#94a3b8;}"
        ".headline{font-size:34px;font-weight:700;margin:8px 0 4px;}"
        ".sub{font-size:15px;color:#cbd5e1;margin:0;}"
        ".grid{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));margin:16px 0;}"
        ".metric{padding:16px 18px;}"
        ".metric .label{display:block;font-size:12px;text-transform:uppercase;letter-spacing:.14em;color:#94a3b8;margin-bottom:8px;}"
        ".metric .value{font-size:26px;font-weight:700;color:#f8fafc;}"
        ".metric .hint{margin-top:8px;font-size:13px;color:#cbd5e1;}"
        ".panel{padding:18px 18px 20px;margin-top:16px;}"
        ".panel h2{margin:0 0 14px;font-size:18px;color:#f8fafc;}"
        ".controls{display:grid;gap:10px;grid-template-columns:repeat(2,minmax(0,1fr));}"
        ".controls-secondary{display:flex;justify-content:flex-start;margin-top:10px;}"
        ".snooze-grid{display:grid;gap:10px;grid-template-columns:repeat(3,minmax(0,1fr));margin-top:10px;}"
        ".button{display:flex;align-items:center;justify-content:center;box-sizing:border-box;width:100%;min-width:0;padding:13px 10px;border-radius:16px;border:0;font-size:15px;font-weight:700;"
        "text-decoration:none;text-align:center;color:#f8fafc;line-height:1.2;background:linear-gradient(135deg,#0ea5e9,#2563eb);}"
        ".button.alt{background:linear-gradient(135deg,#334155,#475569);}"
        ".button.warn{background:linear-gradient(135deg,#f97316,#ea580c);}"
        ".button.good{background:linear-gradient(135deg,#14b8a6,#0f766e);}"
        ".button.bad{background:linear-gradient(135deg,#ef4444,#b91c1c);}"
        ".button.compact{padding:12px 10px;font-size:14px;}"
        ".button.restart{width:auto;min-width:170px;}"
        "form{margin:0;}"
        ".form-row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;}"
        ".field{flex:1 1 180px;background:#0f172a;border:1px solid rgba(148,163,184,.28);color:#f8fafc;border-radius:14px;padding:14px 16px;font-size:16px;}"
        ".debug-grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));}"
        ".debug-item{padding:14px 16px;border-radius:16px;background:rgba(30,41,59,.72);border:1px solid rgba(148,163,184,.18);}"
        ".debug-item strong{display:block;font-size:12px;text-transform:uppercase;letter-spacing:.12em;color:#94a3b8;margin-bottom:6px;}"
        ".debug-item span{font-size:15px;color:#f8fafc;}"
        ".badge{display:inline-block;padding:6px 10px;border-radius:999px;font-size:12px;font-weight:700;background:rgba(14,165,233,.16);color:#7dd3fc;margin-top:12px;}"
        "@media (max-width:600px){.headline{font-size:28px}.metric .value{font-size:23px}.button{font-size:14px;padding:12px 8px}}"));
    client.print(F("</style></head><body><div class=\"wrap\">"));

    client.print(F("<section class=\"hero\"><div class=\"eyebrow\">"));
    client.print(DEVICE_LABEL);
    client.print(F("</div>"));
    client.print(F("<div class=\"headline\">"));
    client.print(htmlEscaped(outputLabel));
    client.print(F("</div><p class=\"sub\">"));
    client.print(htmlEscaped(autoStatusLabel));
    client.print(F("</p><div class=\"badge\">Network: "));
    client.print(htmlEscaped(networkLabel));
    client.print(F(" &middot; IP: "));
    client.print(htmlEscaped(ipAddressLabel));
    client.print(F("</div></section>"));

    client.print(F("<section class=\"grid\">"));
    client.print(F("<div class=\"metric\"><span class=\"label\">Current Time</span><div class=\"value\">"));
    client.print(htmlEscaped(currentTime));
    client.print(F("</div><div class=\"hint\">"));
    client.print(htmlEscaped(timezoneAbbreviation));
    client.print(F("</div></div>"));

    client.print(F("<div class=\"metric\"><span class=\"label\">Detected Sunset</span><div class=\"value\">"));
    client.print(htmlEscaped(sunsetTime));
    client.print(F("</div><div class=\"hint\">Updated "));
    client.print(htmlEscaped(getRelativeAgeLabel(lastSunsetSyncMs)));
    client.print(F("</div></div>"));

    client.print(F("<div class=\"metric\"><span class=\"label\">Auto Turns On</span><div class=\"value\">"));
    client.print(htmlEscaped(autoStart));
    client.print(F("</div><div class=\"hint\">"));
    client.print(autoOnOffsetMinutes);
    client.print(F(" minutes before sunset</div></div>"));

    client.print(F("<div class=\"metric\"><span class=\"label\">Snooze</span><div class=\"value\">"));
    client.print(htmlEscaped(snoozeLabel));
    client.print(F("</div><div class=\"hint\">"));
    client.print(htmlEscaped(snoozeHint));
    client.print(F("</div></div></section>"));

    client.print(F("<section class=\"panel\"><h2>Main Control</h2><div class=\"controls\">"));
    client.print(F("<a class=\"button "));
    client.print(outputsOn ? F("bad") : F("good"));
    client.print(F("\" href=\"/toggle\">"));
    client.print(outputsOn ? F("Turn Off") : F("Turn On"));
    client.print(F("</a>"));
    client.print(F("<a class=\"button "));
    client.print(autoMode ? F("warn") : F("alt"));
    client.print(F("\" href=\"/auto\">"));
    client.print(autoMode ? F("Disable Auto") : F("Enable Auto"));
    client.print(F("</a>"));
    client.print(F("</div><div class=\"controls-secondary\">"));
    client.print(F("<a class=\"button alt compact restart\" onclick=\"return confirm('Restart Night LEDs?');\" href=\"/restart\">Restart</a>"));
    client.print(F("</div></section>"));

    client.print(F("<section class=\"panel\"><h2>Snooze Presets</h2><div class=\"snooze-grid\">"));
    for (uint8_t i = 0; i < SNOOZE_PRESET_COUNT; ++i) {
        client.print(F("<a class=\"button alt compact\" href=\"/snooze?minutes="));
        client.print(SNOOZE_PRESETS[i]);
        client.print(F("\">"));
        client.print(SNOOZE_PRESETS[i]);
        client.print(F("m</a>"));
    }
    client.print(F("<a class=\"button alt compact\" href=\"/clear-snooze\">Clear</a></div></section>"));

    client.print(F("<section class=\"panel\"><h2>Auto Schedule</h2>"));
    client.print(F("<form action=\"/set-offset\" method=\"get\"><div class=\"form-row\">"));
    client.print(F("<input class=\"field\" type=\"number\" name=\"minutes\" min=\"0\" max=\""));
    client.print(MAX_SUNSET_OFFSET_MINUTES);
    client.print(F("\" value=\""));
    client.print(autoOnOffsetMinutes);
    client.print(F("\" placeholder=\"Minutes before sunset\">"));
    client.print(F("<button class=\"button\" type=\"submit\">Save Offset</button>"));
    client.print(F("</div></form></section>"));

    client.print(F("<section class=\"panel\"><h2>Status &amp; Debug</h2><div class=\"debug-grid\">"));
    addMetric(client, F("Outputs"), getOutputLabel());
    addMetric(client, F("Auto Mode"), boolLabel(autoMode));
    addMetric(client, F("Auto Suppressed"), boolLabel(isAutoSuppressed()));
    addMetric(client, F("Soft AP Mode"), boolLabel(softApMode));
    addMetric(client, F("NTP Sync"), hasTimeSync ? getRelativeAgeLabel(lastNtpSyncMs) : F("Waiting"));
    addMetric(client, F("Sunset Sync"), hasSunsetData ? getRelativeAgeLabel(lastSunsetSyncMs) : F("Waiting"));
    addMetric(client, F("Current Time"), currentTime);
    addMetric(client, F("Sunset Time"), sunsetTime);
    addMetric(client, F("Auto Start"), autoStart);
    addMetric(client, F("Auto Reset"), F("03:00"));
    addMetric(client, F("Offset"), offsetLabel);
    addMetric(client, F("Snooze"), snoozeLabel);
    addMetric(client, F("WiFi"), networkLabel);
    addMetric(client, F("IP"), ipAddressLabel);
    addMetric(client, F("Open-Meteo HTTP"), String(lastSunsetHttpCode));
    addMetric(client, F("Uptime"), getUptimeLabel());
    client.print(F("</div></section></div></body></html>"));
}

void sendRedirect(WiFiClient& client, const char* location) {
    client.print(F("HTTP/1.1 303 See Other\r\nLocation: "));
    client.print(location);
    client.print(F("\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"));
}

void setOutputs(bool turnOn) {
    outputsOn = turnOn;
    digitalWrite(USB_1, turnOn ? HIGH : LOW);
    digitalWrite(USB_2, turnOn ? HIGH : LOW);
}

void setManualOutput(bool turnOn) {
    clearSnooze();
    setOutputs(turnOn);

    if (!turnOn) {
        suspendAutoUntilTomorrow();
    }
}

void toggleManualOutput() {
    setManualOutput(!outputsOn);
}

void startSnooze(uint16_t minutes) {
    clearSnooze();
    setOutputs(true);
    activeSnoozeMinutes = minutes;
    snoozeStartedAtMs = millis();
}

void clearSnooze() {
    activeSnoozeMinutes = 0;
    snoozeStartedAtMs = 0;
}

void updateSnoozeState() {
    if (activeSnoozeMinutes == 0) {
        return;
    }

    const unsigned long snoozeDurationMs = static_cast<unsigned long>(activeSnoozeMinutes) * 60UL * 1000UL;
    if ((millis() - snoozeStartedAtMs) >= snoozeDurationMs) {
        clearSnooze();
        setOutputs(false);
        suspendAutoUntilTomorrow();
        Serial.println(F("Snooze expired. Outputs turned off until the next schedule day."));
    }
}

void suspendAutoUntilTomorrow() {
    autoSuppressedToday = hasTimeSync;
    if (hasTimeSync) {
        lastProcessedDayId = getScheduleDayId();
    }
}

void handleDayTransition() {
    if (!hasTimeSync) {
        return;
    }

    const uint32_t currentDayId = getScheduleDayId();
    if (lastProcessedDayId == 0) {
        lastProcessedDayId = currentDayId;
        return;
    }

    if (currentDayId == lastProcessedDayId) {
        return;
    }

    lastProcessedDayId = currentDayId;
    autoSuppressedToday = false;

    Serial.println(F("Schedule day reset. Auto suppression cleared."));
}

void applyAutoMode() {
    if (!hasTimeSync) {
        return;
    }

    const uint32_t currentLocalDayId = getLocalDayId();

    if (autoMode && activeSnoozeMinutes == 0 && isInAutoOffWindow() && currentLocalDayId != lastAutoOffDayId) {
        if (outputsOn) {
            setOutputs(false);
            Serial.println(F("Auto mode turned outputs off."));
        }
        lastAutoOffDayId = currentLocalDayId;
    }

    if (!autoMode || !hasSunsetData || isAutoSuppressed() || !isInAutoWindow() || currentLocalDayId == lastAutoOnDayId) {
        return;
    }

    lastAutoOnDayId = currentLocalDayId;
    if (!outputsOn) {
        setOutputs(true);
        Serial.println(F("Auto mode turned outputs on."));
    }
}

uint32_t getLocalDayId() {
    return timeClient.getEpochTime() / 86400UL;
}

uint32_t getScheduleDayId() {
    const unsigned long resetOffsetSeconds = static_cast<unsigned long>(AUTO_RESET_HOUR) * 3600UL;
    return (timeClient.getEpochTime() - resetOffsetSeconds) / 86400UL;
}

bool isAutoSuppressed() {
    return autoSuppressedToday;
}

bool isInAutoWindow() {
    const int currentMinutes = getCurrentMinutesOfDay();
    const int autoStartMinutes = getAutoStartMinutesOfDay();
    return currentMinutes >= autoStartMinutes;
}

int getCurrentMinutesOfDay() {
    return (timeClient.getHours() * 60) + timeClient.getMinutes();
}

int getAutoStartMinutesOfDay() {
    int sunsetMinutes = (static_cast<int>(sunsetHour) * 60) + sunsetMinute;
    sunsetMinutes -= autoOnOffsetMinutes;
    if (sunsetMinutes < 0) {
        sunsetMinutes = 0;
    }
    return sunsetMinutes;
}

bool isInAutoOffWindow() {
    return getCurrentMinutesOfDay() < (static_cast<int>(AUTO_RESET_HOUR) * 60);
}

String getCurrentTimeLabel() {
    if (!hasTimeSync) {
        return F("--:--");
    }

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());
    return String(buffer);
}

String getSunsetTimeLabel() {
    if (!hasSunsetData) {
        return F("--:--");
    }

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02u:%02u", sunsetHour, sunsetMinute);
    return String(buffer);
}

String getAutoStartLabel() {
    if (!hasSunsetData) {
        return F("--:--");
    }

    const int autoStartMinutes = getAutoStartMinutesOfDay();
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", autoStartMinutes / 60, autoStartMinutes % 60);
    return String(buffer);
}

String getSnoozeLabel() {
    if (activeSnoozeMinutes == 0) {
        return F("Inactive");
    }

    const unsigned long elapsedMs = millis() - snoozeStartedAtMs;
    const unsigned long totalMs = static_cast<unsigned long>(activeSnoozeMinutes) * 60UL * 1000UL;
    const unsigned long remainingMs = (elapsedMs >= totalMs) ? 0UL : (totalMs - elapsedMs);
    const unsigned long remainingMinutes = (remainingMs + 59999UL) / 60000UL;

    return String(activeSnoozeMinutes) + " min (" + String(remainingMinutes) + " left)";
}

String getNetworkLabel() {
    if (WiFi.status() == WL_CONNECTED) {
        return F("WiFi");
    }

    if (softApMode) {
        return F("Access Point");
    }

    return F("Offline");
}

String getIpAddressLabel() {
    if (localIpAddress.length() > 0) {
        return localIpAddress;
    }

    return F("n/a");
}

String getUptimeLabel() {
    const unsigned long totalSeconds = millis() / 1000UL;
    const unsigned long hours = totalSeconds / 3600UL;
    const unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
    const unsigned long seconds = totalSeconds % 60UL;

    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%luh %02lum %02lus", hours, minutes, seconds);
    return String(buffer);
}

String getRelativeAgeLabel(unsigned long timestampMs) {
    if (timestampMs == 0) {
        return F("never");
    }

    const unsigned long ageSeconds = (millis() - timestampMs) / 1000UL;
    if (ageSeconds < 60UL) {
        return String(ageSeconds) + F("s ago");
    }

    const unsigned long ageMinutes = ageSeconds / 60UL;
    if (ageMinutes < 60UL) {
        return String(ageMinutes) + F("m ago");
    }

    const unsigned long ageHours = ageMinutes / 60UL;
    return String(ageHours) + F("h ago");
}

String getAutoStatusLabel() {
    if (!autoMode) {
        return F("Auto mode is disabled. Lights stay in their current state until you change them.");
    }

    if (!hasTimeSync || !hasSunsetData) {
        return F("Waiting for online time and sunset data.");
    }

    if (isAutoSuppressed()) {
        return F("Auto-on is paused until 03:00 because of manual off or snooze expiry.");
    }

    return String("Auto turns on at ") + getAutoStartLabel() + " and turns off at midnight.";
}

String getOutputLabel() {
    return outputsOn ? F("Lights are ON") : F("Lights are OFF");
}

String boolLabel(bool value) {
    return value ? F("Yes") : F("No");
}

String htmlEscaped(const String& value) {
    String escaped = value;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    escaped.replace("'", "&#39;");
    return escaped;
}

void setAutoModeEnabled(bool enabled) {
    if (autoMode == enabled) {
        return;
    }

    autoMode = enabled;
    if (enabled && hasTimeSync && isInAutoOffWindow()) {
        lastAutoOffDayId = getLocalDayId();
    }

    savePersistentSettingsIfNeeded();
}

void setAutoOnOffsetMinutes(uint16_t minutes) {
    if (autoOnOffsetMinutes == minutes) {
        return;
    }

    autoOnOffsetMinutes = minutes;
    savePersistentSettingsIfNeeded();
}

String getRequestPath(const String& requestLine) {
    const int firstSpace = requestLine.indexOf(' ');
    if (firstSpace < 0) {
        return "/";
    }

    const int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
        return "/";
    }

    return requestLine.substring(firstSpace + 1, secondSpace);
}

String getPathWithoutQuery(const String& requestPath) {
    const int queryIndex = requestPath.indexOf('?');
    if (queryIndex < 0) {
        return requestPath;
    }

    return requestPath.substring(0, queryIndex);
}

int getQueryIntValue(const String& requestPath, const char* key, int fallbackValue) {
    const String token = String(key) + "=";
    const int tokenIndex = requestPath.indexOf(token);
    if (tokenIndex < 0) {
        return fallbackValue;
    }

    int valueStart = tokenIndex + token.length();
    int valueEnd = requestPath.indexOf('&', valueStart);
    if (valueEnd < 0) {
        valueEnd = requestPath.length();
    }

    return requestPath.substring(valueStart, valueEnd).toInt();
}

void addMetric(WiFiClient& client, const String& label, const String& value) {
    client.print(F("<div class=\"debug-item\"><strong>"));
    client.print(htmlEscaped(label));
    client.print(F("</strong><span>"));
    client.print(htmlEscaped(value));
    client.print(F("</span></div>"));
}
