#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define USE_SERIAL Serial

static const char ssid[] = "Crazy Engineer's";
static const char password[] = "Alita:Battle Angel";
MDNSResponder mdns;

static void writeRELAY(bool);
String inputVal = "";
const int relayPin  = 13;   //D7
//Relay is connected on D7 represented as a diode (To use the Relay you will 
//need to read the specs, some require +5V, so you might need an external 5V and a transistor
//but many relays are confotable with 3.3V)
const int switch_pin = 15; // D8
int waiting_time; //The amount of time to wait in seconds
String current_relay_status = "";
int switch_value; // This carries the switch value read from the switch

/*If the switch is pressed do exactly as the job description
The webserver is used to set the waiting time, also to control the 
relay when the switch is not pressed (ON, or of +3.3V)*/

ESP8266WiFiMulti WiFiMulti;

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);


static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
<title>DSCreative server</title>
<style>
"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }"
</style>
<script>
var websock;
function start() {
  websock = new WebSocket('ws://' + window.location.hostname + ':81/');
  websock.onopen = function(evt) { console.log('websock open'); };
  websock.onclose = function(evt) { console.log('websock close'); };
  websock.onerror = function(evt) { console.log(evt); };
  websock.onmessage = function(evt) {
    console.log(evt);
    var e = document.getElementById('relaystatus');
    if (evt.data === 'relayon') {
       e.style.color = 'black';
      e.style.background = 'blue';
    }
    else if (evt.data === 'submit') {
      e.style.color = 'green';
    }
    else if (evt.data === 'relayoff') {
     e.style.color = 'green';
     e.style.background = 'yellow';
    }
    else {
      console.log('unknown event');
    }
  };
}
function buttonclick(e) {
  websock.send(e.id);
}

</script>
</head>
<body onload="javascript:start();">
<h1 style="text-align:center;">DSCreative server</h1><b>
<p style="text-align:center;"><a href='/' > Go Back </a><b></p>

<p style="text-align:center;"><button id="relayon"  type="button" onclick="buttonclick(this);"
style="font-size : 20px; width: 50%; height: 100px;">On</button> </p>
<p style="text-align:center;"><button id="relayoff" type="button" onclick="buttonclick(this);"
style="font-size : 20px; width: 50%; height: 100px;">Off</button></p>

<div id="relaystatus" style="text-align:center;font-size : 20px; width: 100%; height: 100px;"><b>Switch Status</b></div>


 
</body>
</html>
)rawliteral";


static const char PROGMEM INDEX_HTML1[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
<title style="text-align:center;">DSCreative server</title>
<style>
"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }"
</style>
</head>
<body>
<h1 style="text-align:center;">DSCreative server</h1>
<form action="/action_page">
  <p style="text-align:center;">Waiting Time:</p><br>
  <p style="text-align:center;"><input type="text" name="timer" value="0" style="font-size : 20px; width: 50%; height: 100px;text-align:center;background-color:yellow;"></p>
  <br>
  <p style="text-align:center;"><input type="submit" value="Submit" style="font-size:20px;height:100px;width:50%;background-color:green;"></p>
</form>
</body>
</html>
)rawliteral";

// Current RELAY status
bool RELAYStatus;

// Commands sent through Web Socket
const char RELAYON[] = "relayon";
const char RELAYOFF[] = "relayoff";
const char readValues[] = "submit";

void handleForm() {
        String my_timer = server.arg("timer"); 
 
        Serial.print("Timer:");
        Serial.println(my_timer);

        //Check for bugs here
        waiting_time = my_timer.toInt();        
 
        server.send_P(200, "text/html", INDEX_HTML);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  USE_SERIAL.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  switch (type) {
  case WStype_DISCONNECTED:
    USE_SERIAL.printf("[%u] Disconnected!\r\n", num);
    break;
  case WStype_CONNECTED:
  {
               IPAddress ip = webSocket.remoteIP(num);
               USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
               // Send the current RELAY status
               if (RELAYStatus) {
                 webSocket.sendTXT(num, RELAYON, strlen(RELAYON));
               }
               else {
                 webSocket.sendTXT(num, RELAYOFF, strlen(RELAYOFF));
               }
  }
    break;
  case WStype_TEXT:
    USE_SERIAL.printf("[%u] get Text: %s\r\n", num, payload);

    if (strcmp(RELAYON, (const char *)payload) == 0) {
      writeRELAY(false);
    }
    else if (strcmp(RELAYOFF, (const char *)payload) == 0) {
      writeRELAY(true);
    }
    
    else {
      USE_SERIAL.println("Unknown command");
    }
    // send data to all connected clients
    webSocket.broadcastTXT(payload, length);
    break;
  case WStype_BIN:
    USE_SERIAL.printf("[%u] get binary length: %u\r\n", num, length);
    hexdump(payload, length);

    // echo data back to browser
    webSocket.sendBIN(num, payload, length);
    break;
  default:
    USE_SERIAL.printf("Invalid WStype [%d]\r\n", type);
    break;
  }
}

void handleRoot()
{
  server.send_P(200, "text/html", INDEX_HTML1);
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


static void writeRELAY(bool RELAYon)
{
  RELAYStatus = RELAYon;
  //Wait for some time according to mytimer (in seconds
  //The switch is always high, so when it is low it should turn on the light
  
  
  
  if (RELAYon) {
        digitalWrite(relayPin, 0);
        delay(1000*waiting_time); //The delay is in seconds
        digitalWrite(relayPin, 1);
   }
  else {
    digitalWrite(relayPin, 1);
    delay(1000*waiting_time); //The delay is in seconds
    digitalWrite(relayPin, 0);
    
  }
  
}

void setup()
{
  pinMode(relayPin, OUTPUT);
  writeRELAY(false);

//  THis is just to help in prototyping can be removed
// No Ground on one side of Wemos and we need one
  digitalWrite(16, LOW);

  USE_SERIAL.begin(115200);

  //Serial.setDebugOutput(true);

  USE_SERIAL.println();
  USE_SERIAL.println();
  USE_SERIAL.println();

  for (uint8_t t = 4; t > 0; t--) {
    USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\r\n", t);
    USE_SERIAL.flush();
    delay(1000);
  }

//  WiFiMulti.addAP(ssid, password);
//
//  while (WiFiMulti.run() != WL_CONNECTED) {
//    Serial.print(".");
//    delay(100);
//  }

  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  USE_SERIAL.print("AP IP address: ");
  USE_SERIAL.println(myIP);

  USE_SERIAL.println("");
  USE_SERIAL.print("Connected to ");
  USE_SERIAL.println(ssid);
  USE_SERIAL.print("IP address: ");
  USE_SERIAL.println(WiFi.localIP());

  if (mdns.begin("DSC_server", WiFi.localIP())) {
    USE_SERIAL.println("MDNS responder started");
    mdns.addService("http", "tcp", 80);
    mdns.addService("ws", "tcp", 81);
  }
  else {
    USE_SERIAL.println("MDNS.begin failed");
  }
  USE_SERIAL.print("Connect to http://DSC_server.local or http://");
  USE_SERIAL.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/action_page", handleForm); //form action is handled here
  server.onNotFound(handleNotFound);

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  //initialize the switch value
  switch_value = 0;
  //initialize waiting time
  waiting_time = 0;
}

void loop()
{
  bool a;
  //check the switch value
  switch_value = digitalRead(switch_pin);
  
  if(digitalRead(relayPin)==1) a = true;
  else if(digitalRead(relayPin)==0) a = false;

  //Start timer and inverse the operation on switch press
  if(switch_value==1) writeRELAY(a);
  
  webSocket.loop();
  server.handleClient();
}
