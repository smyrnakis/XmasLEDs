#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266Ping.h>
#include "secrets.h"

#define PCBLED D0 // 16 , LED_BUILTIN
#define ESPLED D4 // 2
// #define LEDS_1 D1
// #define LEDS_2 D2

#define LEDS_1 D7
#define LEDS_2 D8

char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char apiKey[] = THINGSP_WR_APIKEY;

char otaAuthPin[] = OTA_AUTH_PIN;

// ~~~~ Constants and variables
String httpHeader;
String serverReply;
String localIPaddress;
String formatedTime;

String outStateLED_1 = "off";
String outStateLED_2 = "off";

unsigned int luminosity = 768;

bool allowNtp = true;
bool autoMode = false;
bool pingOk = false;

unsigned long previousMillis = 0;

// unsigned long currentTime = millis();
unsigned long previousTime = 0; 
const long timeoutTime = 2000;

const int ntpInterval = 2000;
const int secondInterval = 1000;

// Network Time Protocol
const long utcOffsetInSeconds = 3600; // 1H (3600) for winter time / 2H (7200) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

WiFiServer server(80);
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

                    if (httpHeader.indexOf("GET /1/on") >= 0) {
                        Serial.println("LEDs_1 on");
                        outStateLED_1 = "on";
                        digitalWrite(LEDS_1, HIGH);
                        refreshToRoot();
                        // digitalWrite(ESPLED, LOW);
                    }
                    else if (httpHeader.indexOf("GET /1/off") >= 0) {
                        Serial.println("LEDs_1 off");
                        outStateLED_1 = "off";
                        digitalWrite(LEDS_1, LOW);
                        refreshToRoot();
                        // digitalWrite(ESPLED, HIGH);
                    }
                    else if (httpHeader.indexOf("GET /2/on") >= 0) {
                        Serial.println("LEDs_2 on");
                        outStateLED_2 = "on";
                        digitalWrite(LEDS_2, HIGH);
                        refreshToRoot();
                        // digitalWrite(PCBLED, LOW);
                    }
                    else if (httpHeader.indexOf("GET /2/off") >= 0) {
                        Serial.println("LEDs_2 off");
                        outStateLED_2 = "off";
                        digitalWrite(LEDS_2, LOW);
                        refreshToRoot();
                        // digitalWrite(PCBLED, HIGH);
                    }
                    else if (httpHeader.indexOf("GET /lum/up") >= 0) {
                        if (luminosity < 1024) {
                            luminosity++;
                            Serial.print("Luminosity up (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /lum/do") >= 0) {
                        if (luminosity > 0) {
                            luminosity--; 
                            Serial.print("Luminosity down (");
                            Serial.print(luminosity);
                            Serial.println(")");
                        }
                        refreshToRoot();
                    }
                    else if (httpHeader.indexOf("GET /auto") >= 0) {
                        autoMode = true;
                        refreshToRoot();
                    }

                    // Display the HTML web page
                    client.println("<!DOCTYPE html><html>");
                    // client.println("<meta http-equiv=\"refresh\" content=\"5\" >\n");
                    client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                    client.println("<link rel=\"icon\" href=\"data:,\">");
                    // CSS to style the on/off buttons 
                    // Feel free to change the background-color and font-size attributes to fit your preferences
                    client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
                    client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
                    client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
                    client.println(".button2 {background-color: #77878A;}</style></head>");

                    // Web Page Heading
                    client.println("<body><h1>XmasLEDs configuration</h1>");

                    client.println("<p><h3>Time: " + formatedTime + "</h3></p>");

                    client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
                    client.println("<tr>");
                    client.println("<th>");

                    // Display current state, and ON/OFF buttons for LEDs_1 (D1) 
                    // client.println("<p>LED_1 - State " + outStateLED_1 + "</p>");
                    // If the outStateLED_1 is off, it displays the ON button       
                    if (outStateLED_1=="off") {
                        client.println("<p><a href=\"/1/on\"><button class=\"button button2\">OFF</button></a></p>");
                    } else {
                        client.println("<p><a href=\"/1/off\"><button class=\"button\">ON</button></a></p>");
                    }

                    client.println("</th>");
                    client.println("<th>");

                    // Display current state, and ON/OFF buttons for LEDs_2 (D2)
                    // client.println("<p>LED_2 - State " + outStateLED_2 + "</p>");
                    // If the outStateLED_2 is off, it displays the ON button       
                    if (outStateLED_2=="off") {
                        client.println("<p><a href=\"/2/on\"><button class=\"button button2\">OFF</button></a></p>");
                    } else {
                        client.println("<p><a href=\"/2/off\"><button class=\"button\">ON</button></a></p>");
                    }

                    client.println("</th>");
                    client.println("</tr>");
                    client.println("<tr>");
                    client.println("<th>");
                    client.println("<p><a href=\"/lum/up\"><button class=\"button\">+</button></a></p>");
                    client.println("</th>");
                    client.println("<th>");
                    client.println("<p><a href=\"/lum/do\"><button class=\"button\">-</button></a></p>");
                    client.println("</th>");
                    client.println("</tr>");
                    client.println("<tr>");
                    client.println("<th>colspan=\"2\">");
                    client.println("<p><a href=\"/lum/do\"><button class=\"button\">Auto Mode</button></a></p>");
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

    unsigned long currentTimeFunc = millis();

    // pull the time
    if ((currentTimeFunc % ntpInterval == 0) && (allowNtp)) {
        // Serial.println("Pulling NTP...");
        pullNTPtime(false);
        allowNtp = false;
    }

    // debounce per second
    if (currentTimeFunc % secondInterval == 0) {
        // debounce for NTP calls
        allowNtp = true;
    }

    // // handle HTTP connections
    // server.handleClient();

    client = server.available();                    // Listen for incoming clients

    if (autoMode) {
        pingOk = pingStatus();
        
    }

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