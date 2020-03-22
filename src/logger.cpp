#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>

#include "elrs_eeprom.h"

#define INVERTED_SERIAL // Comment this out for non-inverted serial
#define USE_WIFI_MANAGER // Comment this out to host an access point rather than use the WiFiManager

ELRS_EEPROM eeprom;

const char *ssid = "ESP8266 Access Point"; // The name of the Wi-Fi network that will be created
const char *password = "password";   // The password required to connect to it, leave blank for an open network

MDNSResponder mdns;

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

uint8_t socketNumber;

String inputString = "";
bool stringComplete = false;

uint16_t eppromPointer = 0;

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>

<head>
    <meta name="viewport" content="width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
    <title>TX Log Messages</title>
    <style>
        body {
            background-color: #1E1E1E;
            font-family: Arial, Helvetica, Sans-Serif;
            Color: #69cbf7;
        }

        textarea {
            background-color: #252525;
            Color: #C5C5C5;
            border-radius: 5px;
            border: none;
        }
    </style>
    <script>
        var websock;
        function start() {
            websock = new WebSocket('ws://' + window.location.hostname + ':81/');
            websock.onopen = function (evt) { console.log('websock open'); };
            websock.onclose = function(e) {
              console.log('Socket is closed. Reconnect will be attempted in 1 second.', e.reason);
              setTimeout(function() {
                start();
              }, 1000);
            };
            websock.onerror = function (evt) { console.log(evt); };
            websock.onmessage = function (evt) {
                console.log(evt);

                var d = new Date();
                var n = d.toISOString();
                document.getElementById("logField").value += n + ' ' + evt.data + '\n';
                document.getElementById("logField").scrollTop = document.getElementById("logField").scrollHeight;
            };
        }

        function saveTextAsFile() {
            var textToWrite = document.getElementById('logField').innerHTML;
            var textFileAsBlob = new Blob([textToWrite], { type: 'text/plain' });
            var fileNameToSaveAs = "tx_log.txt";

            var downloadLink = document.createElement("a");
            downloadLink.download = fileNameToSaveAs;
            downloadLink.innerHTML = "Download File";
            if (window.webkitURL != null) {
                // Chrome allows the link to be clicked without actually adding it to the DOM.
                downloadLink.href = window.webkitURL.createObjectURL(textFileAsBlob);
            } else {
                // Firefox requires the link to be added to the DOM before it can be clicked.
                downloadLink.href = window.URL.createObjectURL(textFileAsBlob);
                downloadLink.onclick = destroyClickedElement;
                downloadLink.style.display = "none";
                document.body.appendChild(downloadLink);
            }

            downloadLink.click();
        }

        function destroyClickedElement(event) {
            // remove the link from the DOM
            document.body.removeChild(event.target);
        }
    </script>
</head>

<body onload="javascript:start();">
    <center>
        <h1>TX Log Messages</h1>
        The following command can be used to connect to the websocket using curl, which is a lot faster over the terminal than Chrome. Alternatively, you can use the textfield below to view messages.
        <br><br>
        <textarea id="curlCmd" rows="40" cols="100" style="margin: 0px; height: 170px; width: 968px;">
curl --include \
     --output - \
     --no-buffer \
     --header "Connection: Upgrade" \
     --header "Upgrade: websocket" \
     --header "Host: example.com:80" \
     --header "Origin: http://example.com:80" \
     --header "Sec-WebSocket-Key: SGVsbG8sIHdvcmxkIQ==" \
     --header "Sec-WebSocket-Version: 13" \
     http://<ipaddr>:81/
    </textarea>
    <br><br>
        <textarea id="logField" rows="40" cols="100" style="margin: 0px; height: 621px; width: 968px;">BEGIN LOG
</textarea>
        <br><br>
        <button type="button" onclick="saveTextAsFile()" value="save" id="save">Save to file...</button>
        <br><br>
    </center>
</body>

</html>
)rawliteral";

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\r\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        socketNumber = num;
      }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\r\n", num, payload);
      // send data to all connected clients
      webSocket.broadcastTXT(payload, length);
      break;
    case WStype_BIN:
      Serial.printf("[%u] get binary length: %u\r\n", num, length);
      hexdump(payload, length);

      // echo data back to browser
      webSocket.sendBIN(num, payload, length);
      break;
    default:
      Serial.printf("Invalid WStype [%d]\r\n", type);
      break;
  }
}

void handleRoot()
{
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup()
{
  #ifdef INVERTED_SERIAL
    Serial.begin(250000, SERIAL_8N1, SERIAL_FULL, 1, true); // inverted serial
  #else
    Serial.begin(250000);  // non-inverted serial
  #endif

  #ifdef USE_WIFI_MANAGER
    WiFiManager wifiManager;
    Serial.println("Starting ESP WiFiManager captive portal...");
    wifiManager.autoConnect("ESP WiFiManager");
  #else
    Serial.println("Starting ESP softAP...");
    WiFi.softAP(ssid, password);
    Serial.print("Access Point \"");
    Serial.print(ssid);
    Serial.println("\" started");

    Serial.print("IP address:\t");
    Serial.println(WiFi.softAPIP());
  #endif

  if (mdns.begin("espWebSock", WiFi.localIP())) {
    Serial.println("MDNS responder started");
    mdns.addService("http", "tcp", 80);
    mdns.addService("ws", "tcp", 81);
  }
  else {
    Serial.println("MDNS.begin failed");
  }
  Serial.print("Connect to http://espWebSock.local or http://");
  #ifdef USE_WIFI_MANAGER
    Serial.println(WiFi.localIP());
  #else
    Serial.println(WiFi.softAPIP());
  #endif

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    inputString += inChar;
    if (inChar == '\n') {
      stringComplete = true;
      return;
    }
  }
}

void loop() {
  serialEvent();
  if (stringComplete) {
    String line = inputString;
    inputString = "";
    stringComplete = false;

    // for (uint16_t i = 0; i < line.length(); ++i) {
    //   eppromPointer++;
    //   eeprom.WriteByte(eppromPointer, line[i]);
    //   if (eppromPointer == RESERVED_EEPROM_SIZE) {
    //     eppromPointer = 0;
    //   }
    // }

    webSocket.broadcastTXT(line);
  }
  server.handleClient();
  webSocket.loop();
}
