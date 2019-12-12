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
String serverReply;
String localIPaddress;
String formatedTime;

bool outStateLED_1 = false;
bool outStateLED_2 = false;

unsigned int luminosity = 768;

bool allowNtp = true;
bool allowPing = true;
bool autoMode = false;
bool pingResult = false;

unsigned long previousMillis = 0;

// unsigned long currentTime = millis();
unsigned long previousTime = 0; 
const long timeoutTime = 2000;

const int ntpInterval = 1000;
const int secondInterval = 1000;

// Metereological info for Geneva, CH
// Sunset time: object/daily/data/0/sunsetTime
String darkSkyUri = "https://darksky.net/forecast/46.2073,6.1499/si12/en.json";

const long utcOffsetInSeconds = 3600; // 1H (3600) for winter time / 2H (7200) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

WiFiServer server(80);
WiFiClient client;

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

    Serial.println("Connected to WiFi.");
    Serial.print("IP: ");
    localIPaddress = (WiFi.localIP()).toString();
    Serial.println(localIPaddress);
    
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

// // Handle HTML page calls
// void handle_OnConnect() {
//     digitalWrite(ESPLED, LOW);
//     server.send(200, "text/html", HTMLpresentData());
//     digitalWrite(ESPLED, HIGH);
// }

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
    // Serial.print("Temperature: ");
    // Serial.print(String(temperature));
    Serial.println();
}

bool pingStatus() {
    IPAddress ipOnePlus (192, 168, 1, 53);
    IPAddress ipXiaomi (192, 168, 1, 54);

    allowPing = false;

    bool pingRet;    
    pingRet = Ping.ping(ipOnePlus);

    if (pingRet) {
        return true;
    } else {
        pingRet = Ping.ping(ipXiaomi);

        if (pingRet) {
            return true;
        }
    }
    return false;
}

void refreshToRoot() {
    client.print("<HEAD>");
    client.print("<meta http-equiv=\"refresh\" content=\"0;url=/\">");
    client.print("</head>");
}

void handleCLientConnection() {
    String currentLine = "";                    // make a String to hold incoming data from the client
    unsigned long currentTime;
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime) { // loop while the client's connected
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
                        outStateLED_1 = true;
                        outStateLED_2 = true;
                        digitalWrite(USB_1, HIGH);
                        digitalWrite(USB_2, HIGH);
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /off") >= 0) {
                        Serial.println("LEDs off");
                        outStateLED_1 = false;
                        outStateLED_2 = false;
                        digitalWrite(USB_1, LOW);
                        digitalWrite(USB_2, LOW);
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /lumUp") >= 0) {
                        if (luminosity < 1024) {
                            luminosity++;
                            Serial.print("Luminosity up (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /lumDow") >= 0) {
                        if (luminosity > 0) {
                            luminosity--; 
                            Serial.print("Luminosity down (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /auto") >= 0) {
                        autoMode = !autoMode;
                        refreshToRoot();
                    }

                    // Display the HTML web page
                    client.println("<!DOCTYPE html><html>");
                    client.println("<meta http-equiv=\"refresh\" content=\"10\" >\n");
                    client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                    client.println("<link rel=\"icon\" href=\"data:,\">");
                    // CSS to style the on/off buttons 
                    // Feel free to change the background-color and font-size attributes to fit your preferences
                    client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
                    client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
                    client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
                    client.println(".button3 {background-color: #ff3300;}");
                    client.println(".button2 {background-color: #77878A;}</style></head>");


                    // Web Page Heading
                    client.println("<body><h1>XmasLEDs configuration</h1>");

                    client.println("<p><h3>Time: " + formatedTime + "</h3></p>");

                    client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
                    client.println("<tr>");
                    // client.println("<th>");
                    if (outStateLED_1) {
                        client.println("<p><a href=\"/off\"><button class=\"button\">ON</button></a></p>");
                    } else {
                        // client.println("<p><a href=\"/1/on\"><button class=\"button button3\">OFF</button></a></p>");
                        client.println("<p><a href=\"/on\"><button class=\"button button2\">OFF</button></a></p>");

                    }
                    // client.println("</th>");

                    // client.println("<th>");
                    // if (outStateLED_2) {
                    //     client.println("<p><a href=\"/2/off\"><button class=\"button\">ON</button></a></p>");
                    // } else {
                    //     client.println("<p><a href=\"/2/on\"><button class=\"button button2\">OFF</button></a></p>");
                    // }
                    // client.println("</th>");
                    client.println("</tr>");

                    client.println("<tr>");
                    // client.println("<td> colspan=\"2\">");
                    if (autoMode) {
                        client.println("<p><a href=\"/auto\"><button class=\"button\">Auto Mode</button></a></p>");
                    } else {
                        client.println("<p><a href=\"/auto\"><button class=\"button button2\">Auto Mode</button></a></p>");
                    }
                    // client.println("</td>");
                    client.println("</tr>");
                    client.println("</table>");

                    client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
                    client.println("<tr>");
                    client.println("<th>");
                    client.println("<p><a href=\"/lumUp\"><button class=\"button\">+</button></a></p>");
                    client.println("</th>");
                    client.println("<th>");
                    client.println("<p><a href=\"/lumDow\"><button class=\"button\">-</button></a></p>");
                    client.println("</th>");
                    client.println("</tr>");
                    client.println("</table>");

                    client.println("</body></html>");

                    // The HTTP response ends with another blank line
                    client.println();
                    // Break out of the while loop
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

    unsigned long currentTimeMillis = millis();

    // check current output pingStatus
    outStateLED_1 = digitalRead(USB_1);
    outStateLED_2 = digitalRead(USB_2);

    // pull the time
    if ((currentTimeMillis % ntpInterval == 0) && (allowNtp)) {
        // Serial.println("Pulling NTP...");
        pullNTPtime(false);
        allowNtp = false;
    }

    // debounce NTP
    if ((currentTimeMillis % 100 == 0) && (!allowNtp)) {
        allowNtp = true;
    }

    // timeClient.getHours()
    // timeClient.getMinutes();
    // timeClient.getSeconds();

    // turn ON/OFF leds (keep powerbank on)
    if (timeClient.getSeconds() % 2 == 0) {
        digitalWrite(ESPLED, LOW);
        digitalWrite(PCBLED, HIGH);
    }
    else {
        digitalWrite(ESPLED, HIGH);
        digitalWrite(PCBLED, LOW);
    }

    if (autoMode) {
        if (timeClient.getHours() > 17) {
            if (allowPing) {
                pingResult = pingStatus();
            }
            if (pingResult) {
                digitalWrite(USB_1, HIGH);
                digitalWrite(USB_2, HIGH);
                outStateLED_1 = true;
                outStateLED_2 = true;
            }
            else {
                digitalWrite(USB_1, LOW);
                digitalWrite(USB_2, LOW);
                outStateLED_1 = false;
                outStateLED_2 = false;
            }
        }
        else {
            // digitalWrite(USB_1, LOW);
            digitalWrite(USB_2, LOW);
            // outStateLED_1 = false;
            outStateLED_2 = false;
        }
    }

    // debounce PING
    if ((currentTimeMillis % 60000 == 0) && (!allowPing)) {
        allowPing = true;
    }


    // // handle HTTP connections
    // server.handleClient();

    client = server.available();                    // Listen for incoming clients

    if (client) {                                   // If a new client connects,
        Serial.println("New Client.");              // print a message out in the serial port
        
        handleCLientConnection();

        // Clear the header variable
        httpHeader = "";
        // Close the connection
        client.stop();
        Serial.println("Client disconnected.");
        Serial.println("");
    }
}