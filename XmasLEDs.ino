#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266Ping.h>
#include <ArduinoJson.h>
#include "secrets.h"

#define PCBLED D0 // 16 , LED_BUILTIN
#define ESPLED D4 // 2

#define USB_1 D7
#define USB_2 D8

char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char apiKey[] = THINGSP_WR_APIKEY;

char otaAuthPin[] = OTA_AUTH_PIN;

// ~~~~ Constants and variables
String httpHeader;
String localIPaddress;
String formatedTime;

bool showDebugs = true;

bool manuallyOn = false;
bool manuallyOff = false;
bool outputState = false;
bool outStateBefore = false;
bool outChangeReport = false;

unsigned int luminosity = 1024;

bool allowPing = true;
bool allowThSp = true;
bool autoOff = true;
bool autoMode = true;
bool restoreAuto = false;
bool lastPing = true;
bool movementReported = false;
bool extPingResult = false;
bool wifiAvailable = false;
bool connectionLost = false;
bool generalError = false;

unsigned long lastNTPtime = 0;
unsigned long lastPingTime = 0;
unsigned long lastPingTimeExt = 0;
unsigned long lastThSpTime = 0;
unsigned long lastPCBledTime = 0;
unsigned long movReportedTime = 0;

unsigned int snoozeMinutes = 0;
unsigned long snoozeTime = 0;
unsigned long connectionTime = 0;
unsigned long connectionLostTime = 0;
const long connectionKeepAlive = 2000;

const int ntpInterval = 2000;
const int pingInterval = 300000;
const int thingSpeakInterval = 300000;
const int internetCheckInterval = 120000;

// Meteorological info for Geneva, CH
// Sunset time: object/daily/data/0/sunsetTime
String darkSkyUri = "https://darksky.net/forecast/46.2073,6.1499/si12/en.json";
unsigned int sunsetTime = 17;

const char* thinkSpeakAPIurl = "api.thingspeak.com"; // "184.106.153.149" or api.thingspeak.com

const long utcOffsetInSeconds = 3600; // 1H (3600) for winter time / 2H (7200) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

WiFiServer server(80);
WiFiClient client;
WiFiClient clientThSp;
HTTPClient rjdMonitor;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);



void setup() {
    pinMode(PCBLED, OUTPUT);
    pinMode(ESPLED, OUTPUT);
    pinMode(USB_1, OUTPUT);
    pinMode(USB_2, OUTPUT);

    digitalWrite(PCBLED, HIGH);
    digitalWrite(ESPLED, HIGH);
    digitalWrite(USB_1, LOW);
    digitalWrite(USB_2, LOW);

    Serial.begin(115200);
    delay(100);

    WiFiManager wifiManager;
    //wifiManager.resetSettings();
    wifiManager.setConfigPortalTimeout(180);  // 180 sec timeout for WiFi configuration
    wifiManager.autoConnect(defaultSSID, defaultPASS);
    
    server.begin();
    Serial.println("HTTP server starter on port 80.");

    timeClient.begin();

    handleOTA();
    delay(100);

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        wifiAvailable = false;
        Serial.println("Failed to connect on WiFi network!");
        Serial.println("Operating offline.");
    }
    else {
        wifiAvailable = true;
        Serial.println("Connected to WiFi.");
        Serial.print("IP: ");
        localIPaddress = (WiFi.localIP()).toString();
        Serial.println(localIPaddress);
    }

    delay(500);
}

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

void ledStatusReport(bool currentState) {
    if (currentState) {
        rjdMonitor.begin("http://192.168.1.30/BedLedOn");
    }
    else {
        rjdMonitor.begin("http://192.168.1.30/BedLedOff");
    }

    int httpCode = rjdMonitor.GET();
    // if (httpCode > 0) { //Check the returning code
    //   String payload = rjdMonitor.getString();       //Get the request response payload
    //   Serial.println(payload);                       //Print the response payload
    // }
    rjdMonitor.end();
}

void thingSpeakRequest() {
    if (clientThSp.connect(thinkSpeakAPIurl,80)) {
        String postStr = apiKey;
        postStr +="&field6=";
        postStr += String(digitalRead(USB_1));
        postStr += "\r\n\r\n";

        clientThSp.print("POST /update HTTP/1.1\n");
        clientThSp.print("Host: api.thingspeak.com\n");
        clientThSp.print("Connection: close\n");
        clientThSp.print("X-THINGSPEAKAPIKEY: " + (String)apiKey + "\n");
        clientThSp.print("Content-Type: application/x-www-form-urlencoded\n");
        clientThSp.print("Content-Length: ");
        clientThSp.print(postStr.length());
        clientThSp.print("\n\n");
        clientThSp.print(postStr);
        clientThSp.stop();
        Serial.println("Data uploaded to thingspeak!");
    }
    else {
        Serial.println("ERROR: could not upload data to thingspeak!");
        clientThSp.stop();
    }
    lastThSpTime = millis();
}

void pullNTPtime(bool printData) {
    // Serial.println("Pulling NTP...");
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
    lastNTPtime = millis();
}

bool pingStatus(bool pingExternal) {

    bool pingRet;
    // digitalWrite(ESPLED, LOW);

    if (pingExternal) {
        IPAddress ipThingSpeak (184, 106, 153, 149);
        IPAddress ipGoogle (8, 8, 8, 8);

        pingRet = Ping.ping(ipThingSpeak);

        if (!pingRet) {
            pingRet = Ping.ping(ipGoogle);
        }

        lastPingTimeExt = millis();
    }
    else {
        digitalWrite(ESPLED, LOW);

        IPAddress ipOnePlus (192, 168, 1, 5);
        IPAddress ipXiaomi (192, 168, 1, 6);

        bool pingResOnePlus = false;
        bool pingResXiaomi = false;

        pingRet = Ping.ping(ipXiaomi);
        if (!pingRet) {
            pingRet = Ping.ping(ipOnePlus);
        }

        if (pingRet) {
            lastPing = true;
        }
        else {
            if (lastPing) {
                pingRet = true;
            }
            lastPing = false;
        }

        // 6/2/2019
        // pingResXiaomi = Ping.ping(ipXiaomi);
        // if (!pingResXiaomi) {
        //     pingResOnePlus = Ping.ping(ipOnePlus);
        // }

        // if (pingResOnePlus || pingResXiaomi) {
        //     // lastPing = true;
        //     pingRet = true;
        // }
        // else {
        //     // lastPing = false;
        //     pingRet = false;
        // }

        // if (pingRet) {
        //     lastPing = true;
        // }
        // else {
        //     if (lastPing) {
        //         lastPing = false;
        //         pingRet = true;
        //     }
        // }

        // 4/2/2019
        // if (Ping.ping(ipOnePlus)) {
        //     lastPing = true;
        //     pingRet = true;
        // } else {
        //     pingRet = lastPing;
        //     lastPing = false;
        // }

        // if (!pingRet) {
        //     if (Ping.ping(ipXiaomi)) {
        //         lastPing = true;
        //         pingRet = true;
        //     } else {
        //         pingRet = lastPing;
        //         lastPing = false;
        //     }
        // }

        allowPing = false;
        lastPingTime = millis();
    }
    digitalWrite(ESPLED, HIGH);
    return pingRet;
}

void ledHandler(bool error) {
    if (error) {
        if (millis() - lastPCBledTime >= 100) {
            digitalWrite(PCBLED, !digitalRead(PCBLED)); 
            lastPCBledTime = millis();
        }
    }
    else if (digitalRead(USB_1) && digitalRead(USB_2)) {
        // if (millis() - lastPCBledTime >= 1000) {
        //     digitalWrite(PCBLED, !digitalRead(PCBLED)); 
        //     lastPCBledTime = millis();
        // }
    }
    else if (digitalRead(USB_1) ^ digitalRead(USB_2)) {
        if (millis() - lastPCBledTime >= 1000) {
            digitalWrite(PCBLED, !digitalRead(PCBLED));
            lastPCBledTime = millis();
        }
    }
    else if (connectionLost || !wifiAvailable) {
        if (millis() - lastPCBledTime >= 500) {
            digitalWrite(PCBLED, !digitalRead(PCBLED)); 
            lastPCBledTime = millis();
        }
    }
    else {
        digitalWrite(PCBLED, HIGH);
    }
}

void refreshToRoot() {
    client.print("<HEAD>");
    client.print("<meta http-equiv=\"refresh\" content=\"0;url=/\">");
    client.print("</head>");
}

void sendOk() {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println("Connection: close");
    client.println();
    refreshToRoot();
}

void handleClientConnection() {
    String currentLine = "";                    // make a String to hold incoming data from the client
    unsigned long currentTime;
    currentTime = millis();
    connectionTime = currentTime;
    while (client.connected() && currentTime - connectionTime <= connectionKeepAlive) { // loop while the client's connected
        currentTime = millis();         
        if (client.available()) {                   // if there's bytes to read from the client,
            char c = client.read();                 // read a byte, then
            Serial.write(c);                        // print it out the serial monitor
            httpHeader += c;
            if (c == '\n') {                        // if the byte is a newline character
                // if the current line is blank, you got two newline characters in a row.
                // that's the end of the client HTTP request, so send a response:
                if (currentLine.length() == 0) {
                    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                    // and a content-type so the client knows what's coming, then a blank line:
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-type:text/html");
                    client.println("Connection: close");
                    client.println();

                    if (httpHeader.indexOf("GET /on") >= 0) {
                        Serial.println("LEDs on");
                        manuallyOn = true;
                        manuallyOff = false;
                        // allowPing = true;         // 2020/01/26 : to check first and maybe enable
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /off") >= 0) {
                        Serial.println("LEDs off");
                        manuallyOn = false;
                        manuallyOff = true;
                        snoozeMinutes = 0;
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /snooze") >= 0) {
                        switch (snoozeMinutes) {
                            case 0:
                                snoozeMinutes = 5;
                                break;
                            case 5:
                                snoozeMinutes = 10;
                                break;
                            case 10:
                                snoozeMinutes = 30;
                                break;
                            case 30:
                                snoozeMinutes = 60;
                                break;
                            case 60:
                                snoozeMinutes = 0;
                                break;
                        default:
                            break;
                        }
                        if (snoozeMinutes) {
                            snoozeTime = millis();
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /lumUp") >= 0) {
                        if (luminosity <= 1014) {
                            luminosity += 10;
                            Serial.print("Luminosity up (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /lumDow") >= 0) {
                        if (luminosity >= 10) {
                            luminosity -= 10; 
                            Serial.print("Luminosity down (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /autoMode") >= 0) {
                        autoMode = !autoMode;
                        // if (autoMode) {
                        //     autoOff = true;
                        //     manuallyOn = false;
                        //     manuallyOff = false;
                        // }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /detectHome") >= 0) {
                        autoOff = !autoOff;
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /movement") >= 0) {
                        Serial.println("Movement reported");
                        movementReported = true;
                        movReportedTime = millis();
                        // refreshToRoot();
                        // sendOk();
                    }

                    // Display the HTML web page
                    client.println("<!DOCTYPE html><html>");
                    client.println("<meta http-equiv=\"refresh\" content=\"10\" >\n");
                    client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                    client.println("<link rel=\"icon\" href=\"data:,\">");
                    client.println("<title>XmasLEDs</title>");
                    // CSS style
                    client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
                    client.println("body {color: white; background: black;}");
                    client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
                    client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
                    client.println(".button3 {background-color: #ff3300;}");
                    client.println(".button2 {background-color: #77878A;}</style></head>");


                    // Web Page Heading
                    client.println("<body><h1>XmasLEDs configuration</h1>");
                    client.println("<p><h3>Time: " + formatedTime + "</h3></p>");

                    client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
                    client.println("<tr>");
                    if (digitalRead(USB_1) || digitalRead(USB_2)) {
                        client.println("<th><p><a href=\"/off\"><button class=\"button\">ON</button></a></p></th>");
                    } else {
                        client.println("<th><p><a href=\"/on\"><button class=\"button button2\">OFF</button></a></p></th>");
                    }
                    switch (snoozeMinutes) {
                        case 0:
                            client.println("<th><p><a href=\"/snooze\"><button class=\"button button2\">SNOOZE OFF</button></a></p></th>");
                            break;
                        case 5:
                            client.println("<th><p><a href=\"/snooze\"><button class=\"button\">SNOOZE 5'</button></a></p></th>");
                            break;
                        case 10:
                            client.println("<th><p><a href=\"/snooze\"><button class=\"button\">SNOOZE 10'</button></a></p></th>");
                            break;
                        case 30:
                            client.println("<th><p><a href=\"/snooze\"><button class=\"button\">SNOOZE 30'</button></a></p></th>");
                            break;
                        case 60:
                            client.println("<th><p><a href=\"/snooze\"><button class=\"button\">SNOOZE 60'</button></a></p></th>");
                            break;
                    default:
                        break;
                    }
                    client.println("</tr>");
                    client.println("<tr>");
                    if (autoMode) {
                        client.println("<td><p><a href=\"/autoMode\"><button class=\"button\">Auto Mode</button></a></p></td>");
                    } else {
                        client.println("<td><p><a href=\"/autoMode\"><button class=\"button button2\">Auto Mode</button></a></p></td>");
                    }
                    if (autoOff) {
                        client.println("<td><p><a href=\"/detectHome\"><button class=\"button\">Auto OFF</button></a></p></td>");
                    } else {
                        client.println("<td><p><a href=\"/detectHome\"><button class=\"button button2\">Auto OFF</button></a></p></td>");
                    }

                    client.println("</tr>");
                    // client.println("</table>");

                    // client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
                    client.println("<tr>");
                    client.println("<td>");
                    client.println("<p><a href=\"/lumUp\"><button class=\"button\">+</button></a></p>");
                    client.println("</td>");
                    client.println("<td>");
                    client.println("<p><a href=\"/lumDow\"><button class=\"button\">-</button></a></p>");
                    client.println("</td>");
                    client.println("</tr>");
                    client.println("</table>");

                    client.println("<p></p>");
                    client.println("<p>current millis: " + String(millis()) + "</p>");
                    client.println("<p>manuallyOn: " + String(manuallyOn) + "</p>");
                    client.println("<p>manuallyOff: " + String(manuallyOff) + "</p>");
                    client.println("<p>autoMode: " + String(autoMode) + "</p>");
                    client.println("<p>restoreAuto: " + String(restoreAuto) + "</p>");
                    client.println("<p>autoOff: " + String(autoOff) + "</p>");
                    client.println("<p>luminosity: " + String(luminosity) + "</p>");
                    client.println("<p>snoozeMinutes: " + String(snoozeMinutes) + "</p>");
                    client.println("<p>movementReported: " + String(movementReported) + "</p>");
                    unsigned long tempSec;
                    tempSec = ((millis() - movReportedTime) / 1000);
                    client.println("<p>movReportedTime: -" + String(tempSec) + "</p>");
                    client.println("<p>allowPing: " + String(allowPing) + "</p>");
                    client.println("<p>lastPing: " + String(lastPing) + "</p>");
                    tempSec = ((millis() - lastPingTime) / 1000);
                    client.println("<p>lastPingTime: -" + String(tempSec) + "</p>");
                    tempSec = ((millis() - lastPingTimeExt) / 1000);
                    client.println("<p>lastPingTimeExt: -" + String(tempSec) + "</p>");
                    client.println("<p>wifiAvailable: " + String(wifiAvailable) + "</p>");
                    client.println("<p>connectionLost: " + String(connectionLost) + "</p>");
                    client.println("<p>connectionLostTime: " + String(connectionLostTime) + "</p>");

                    client.println("</body></html>");

                    // The HTTP response ends with another blank line
                    client.println();
                    break;
                } else { // if you got a newline, then clear currentLine
                    currentLine = "";
                }
            } else if (c != '\r') {  // if you got anything else but a carriage return character,
                currentLine += c;      // add it to the end of the currentLine
            }
        }
    }
}


void loop(){
    ArduinoOTA.handle();

    // check Internet connectivity
    if (millis() > lastPingTimeExt + internetCheckInterval) {
        extPingResult = pingStatus(true);
        Serial.print("\r\nPing status: ");
        Serial.println((String)extPingResult);
        Serial.println("\r\n");

        connectionLost = !extPingResult;

        if ((!extPingResult) && (!connectionLost)) {
            Serial.println("\r\nWARNING: no Internet connectivity!\r\n");
            connectionLostTime = millis();
            connectionLost = true;
        }
    }

    // reboot if no Internet for 5 minutes
    if ((millis() > connectionLostTime + 300000) && connectionLost) {
        if (!extPingResult) {
            Serial.println("No Internet connection since 5 minutes. Rebooting in 5 sec...");
            delay(5000);
            ESP.restart();
        }
    }

    // reboot device if no WiFi for 5 minutes (3' wifiManager timeout + 2') (1h : 3600000)
    if ((millis() > 300000) && (!wifiAvailable)) {
        Serial.println("No WiFi connection since 2 minutes. Rebooting in 5 sec...");
        delay(5000);
        ESP.restart();
    }

    // pull the time
    if ((millis() > lastNTPtime + ntpInterval) && wifiAvailable) {
        pullNTPtime(false);
    }

    // handle incoming connections
    client = server.available();
    if (client) {
        Serial.println("New client connection.");
        handleClientConnection();
        // Clear the header variable
        httpHeader = "";
        client.stop();
        Serial.println("Client disconnected.");
        Serial.println("");
    }

    // debounce PING
    if (millis() > lastPingTime + pingInterval) {
        allowPing = true;
    }

    // debounce MOVEMENT detected (every 30 minutes) // 1800000 = 30' | 600k = 10'
    if (millis() > movReportedTime + 600000) {
        movementReported = false;
    }


    if (manuallyOff) {
        outputState = false;
        manuallyOn = false;
        autoMode = false;
    }
    else if (manuallyOn) {
        outputState = true;
        manuallyOff = false;

        if (autoOff) {
            if (!movementReported) {
                if (allowPing) {
                    outputState = pingStatus(false);
                    if (!outputState) {
                        manuallyOn = false;
                    }
                }
            }
        }
    }

    if (autoMode && !manuallyOn) {
        if (timeClient.getHours() >= sunsetTime) {
            if (movementReported) {
                outputState = true;
            }
            else {
                if (allowPing) {
                    outputState = pingStatus(false);
                }
            }
        }
        else {
            outputState = false;
            manuallyOff = false;
        }
    }

    if (snoozeMinutes && outputState) {
        if (millis() > (snoozeTime + (snoozeMinutes * 60000))) {
            outputState = false;
            manuallyOn = false;
            snoozeMinutes = 0;
            if (autoMode) {
                autoMode = false;
                restoreAuto = true;
            }
        }
    }

    if (restoreAuto) {
        if (timeClient.getHours() < sunsetTime) {
            autoMode = true;
            restoreAuto = false;
        }
    }

    // reflect output changes
    digitalWrite(USB_1, outputState);
    digitalWrite(USB_2, outputState);


    if (outStateBefore != outputState) {
        outChangeReport = true;
        outStateBefore = outputState;
    }

    // // update Thingspeak
    // if (((millis() > lastThSpTime + thingSpeakInterval) || outChangeReport) && wifiAvailable) {
    //     thingSpeakRequest();
    //     outChangeReport = false;
    // }

    // report status to rjdMonitor
    if (((millis() > lastThSpTime + 30000) || outChangeReport) && wifiAvailable) {
        ledStatusReport(outputState);
        outChangeReport = false;
    }


    // status leds
    ledHandler(generalError);
}