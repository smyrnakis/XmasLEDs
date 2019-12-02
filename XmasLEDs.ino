#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "secrets.h"

#define PCBLED D0 // 16 , LED_BUILTIN
#define ESPLED D4 // 2
#define LEDS_1 D1
#define LEDS_2 D2

char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char apiKey[] = THINGSP_WR_APIKEY;

char otaAuthPin[] = OTA_AUTH_PIN;

// ~~~~ Constants and variables
String httpHeader;
String serverReply;
String localIPaddress;
String formatedTime;

bool allowNtp = true;

unsigned long previousMillis = 0;

const int ntpInterval = 2000;
const int secondInterval = 1000;

// Network Time Protocol
const long utcOffsetInSeconds = 3600; // 1H (3600) for winter time / 2H (7200) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

ESP8266WebServer server(80);
WiFiClient client;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);



void setup() {
  pinMode(PCBLED, OUTPUT);
  pinMode(ESPLED, OUTPUT);
  pinMode(LEDS_1, OUTPUT);
  pinMode(LEDS_2, OUTPUT);

  digitalWrite(PCBLED, HIGH);
  digitalWrite(ESPLED, HIGH);
  digitalWrite(LEDS_1, LOW);
  digitalWrite(LEDS_1, LOW);

  Serial.begin(115200);
  delay(100);

  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(180);  // 180 sec timeout for WiFi configuration
  wifiManager.autoConnect(defaultSSID, defaultPASS);

  Serial.println("Connected to WiFi.");
  Serial.print("IP: ");
  localIPaddress = (WiFi.localIP()).toString();
  Serial.println(localIPaddress);

  server.on("/", handle_OnConnect);
  server.on("/about", handle_OnConnectAbout);
  server.onNotFound(handle_NotFound);
  
  server.begin();
  Serial.println("HTTP server starter on port 80.");

  timeClient.begin();

  // while (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //   Serial.println("Connection Failed! Rebooting...");
  //   delay(5000);
  //   ESP.restart();
  // }

  // handle OTA updates
  handleOTA();

  delay(100);
}


// OTA code update
void handleOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("XmasLEDs");

  ArduinoOTA.setPassword((const char *)otaAuthPin);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

// Handle HTML page calls
void handle_OnConnect() {
  digitalWrite(ESPLED, LOW);
  server.send(200, "text/html", HTMLpresentData());
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectAbout() {
  digitalWrite(ESPLED, LOW);
  server.send(200, "text/plain", "A smart X-MAS LED lights automation! (C)2019 Apostolos Smyrnakis");
  digitalWrite(ESPLED, HIGH);
}

void handle_NotFound(){
  digitalWrite(ESPLED, LOW);
  server.send(404, "text/html", HTMLnotFound());
  digitalWrite(ESPLED, HIGH);
}

// HTML pages structure
String HTMLpresentData(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<meta http-equiv=\"refresh\" content=\"5\" >\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>RJD Monitor</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>RJD Monitor</h1>\n";
  
  ptr +="<p>Local IP: ";
  ptr += (String)localIPaddress;
  ptr +="</p>";

  ptr += "<p>Timestamp: ";
  ptr +=(String)formatedTime;
  ptr += "</p>";

  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

String HTMLnotFound(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>RJD Monitor</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>You know this 404 thing ?</h1>\n";
  ptr +="<p>What you asked can not be found... :'( </p>";
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

// Get the time
void pullNTPtime(bool printData) {
  timeClient.update();
  formatedTime = timeClient.getFormattedTime();

  if (printData) {
    // Serial.print(daysOfTheWeek[timeClient.getDay()]);
    // Serial.print(", ");
    // Serial.print(timeClient.getHours());
    // Serial.print(":");
    // Serial.print(timeClient.getMinutes());
    // Serial.print(":");
    // Serial.println(timeClient.getSeconds());
    Serial.println(timeClient.getFormattedTime()); // format time like 23:05:00
  }
}

// Serial print data
void serialPrintAll() {
  Serial.println(timeClient.getFormattedTime());
//   Serial.print("Temperature: ");
//   Serial.print(String(temperature));
  Serial.println();
}


void loop(){
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();

    // pull the time
    if ((currentMillis % ntpInterval == 0) && (allowNtp)) {
        // Serial.println("Pulling NTP...");
        pullNTPtime(false);
        allowNtp = false;
    }

    // debounce per second
    if (currentMillis % secondInterval == 0) {
        // debounce for NTP calls
        allowNtp = true;
    }

    // handle HTTP connections
    server.handleClient();
}