#include <Arduino.h>

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#define REQUIRESNEW false
#define REQUIRESALARMS false
#include <OneWire.h>
#include <DallasTemperature.h>

#include "FS.h"
#include "Ticker.h"

// +++++++++++++++++++

#define DEFAULT_NODE_NAME "F42-NODE"

#define FETCH_SENSORS_CYCLE_SEC 10

#define ONE_WIRE_BUS 4

#define CONFIG_HTML "/config.html"
#define CONFIG_FILE "/config.cfg"
#define MAX_SENSORS 10

#define PAYLOAD_BUFFER_SIZE 1024+20+20+MAX_SENSORS*40 

#define DEBUG 0
#define debug_print(line) \
            do { if (DEBUG) Serial.print(line); } while (0)
#define debug_println(line) \
            do { if (DEBUG) Serial.println(line); } while (0)

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature dsSensors(&oneWire);

uint8_t deviceCount = 0;

// to hold device addresses
DeviceAddress* deviceAddresses = NULL;

WiFiManager wifiManager;

WiFiServer server(80);
WiFiClient client;

String configHtml = "";
String nodeName = DEFAULT_NODE_NAME;

typedef struct SensorData {
  String id;
  String type;
  String location;
  String topic;
  String measurand;
  String value;
} SensorData;

SensorData tmpSensor = {"", "", "", "", "", "?"};

SensorData sensors[MAX_SENSORS]; 

// ------------------------------------------------------------------------------------------------

void addr2hex(DeviceAddress da, char hex[17]) {
  snprintf(hex, 17, "%02X%02X%02X%02X%02X%02X%02X%02X", da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
}

void addSensor(String id, String type, String measurand) {
  uint8_t idx = 0;
  while (sensors[idx].id != "" && idx < MAX_SENSORS) {++idx;}
  if (idx < MAX_SENSORS) {
    sensors[idx].id = id;
    sensors[idx].type = type;
    sensors[idx].location = id;
    sensors[idx].topic = id;
    sensors[idx].measurand = measurand;
    sensors[idx].value = "?";
    debug_println("Add "+type+" sensor("+id+")");
  }
}

String getSensorLocation(String id) {
  uint8_t idx = 0;
  while (!sensors[idx].id.equals(id) && idx < MAX_SENSORS) {++idx;}
  return (idx < MAX_SENSORS) ? sensors[idx].location : ""; 
}

SensorData& getSensorData(String id) {
  uint8_t idx = 0;
  while (!sensors[idx].id.equals(id) && idx < MAX_SENSORS) {++idx;}
  return idx < MAX_SENSORS ? sensors[idx] : tmpSensor;
}

void printSensorData(String id) {
  SensorData sd = getSensorData(id);
  Serial.println(sd.type + " " + sd.location + "(" + sd.id + "): "+ sd.value);
}

uint16_t getPayload(char payload[]) {
  uint16_t index=0;
  while (client.connected() && client.available()) {
    if (client.available() && index < PAYLOAD_BUFFER_SIZE - 1) {
      int c = client.read();
      if (c >= 32) {
        payload[index++] = c;
      } else if (c == '\n') {
        payload[index++] = c;
      }   
    }
  }
  payload[index] = '\0';
  return index;
}

void printKV(String k, String v) {
  client.print("\"");
  client.print(k);
  client.print("\":\"");
  client.print(v);
  client.print("\"");
}

String findData(String line, String key) {
  int sIndex = line.indexOf(key+"=") + key.length() + 1;
  int eIndex = line.indexOf("&", sIndex);
  if (eIndex > 0) {
    return line.substring(sIndex, eIndex);
  } else {
    return line.substring(sIndex);
  }
} 

void saveConfig() {
  if (!SPIFFS.begin()) {
    Serial.println("Error while init SPIFFS.");
    return;
  }
  File f = SPIFFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println("File '"+String(CONFIG_FILE)+"' open to write failed");
  } else {
    Serial.print("Save config file ...");
    
    f.println("node=" + nodeName);
    debug_println("<- node='" + nodeName+"'");
    
    uint8_t idx = 0;
    while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
      f.println("sensor-" + sensors[idx].id + "=" + sensors[idx].location);
      debug_println("<- Sensor(" + sensors[idx].id + ")='" + sensors[idx].location+"'");
      ++idx;
    }
    
    f.close();

    Serial.println(" done");
  }

  SPIFFS.end();
}

bool isNewValue(String oldValue, String newValue) {
  return newValue.length() > 0 && !oldValue.equals(newValue);
}

void handleRequest() {
  char payloadBuffer[PAYLOAD_BUFFER_SIZE];
  uint16_t payloadSize = getPayload(payloadBuffer);

  if (payloadSize > 0) {
    String payload(payloadBuffer);
    if (payload.indexOf("GET / HTTP") > -1) {
      // send a standard http response header
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");  // the connection will be closed after completion of the response
      client.println();
      client.println(configHtml);
    } else if (payload.indexOf("GET /node HTTP") > -1) { 
      // GET /node
      client.println("HTTP/1.1 200 OK");
      client.println("Access-Control-Allow-Origin:null");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      client.print("{");
      printKV("node", nodeName);
      client.println("}");
    } else if (payload.indexOf("GET /sensors HTTP") > -1) {
      // GET /sensors
      client.println("HTTP/1.1 200 OK");
      client.println("Access-Control-Allow-Origin:null");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      client.print("{\"sensors\": {");
      String sep = "";
      uint8_t idx = 0;
      while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
        client.print(sep);
        client.print("\"");
        client.print(sensors[idx].id); 
        client.print("\":{");
        printKV("location", sensors[idx].location); 
        client.print(", ");
        printKV("type", sensors[idx].type); 
        client.print(", ");
        printKV("measurand", sensors[idx].measurand); 
        client.print(", ");
        printKV("value", sensors[idx].value); 
        client.print("} ");
        sep = ", ";
        ++idx;
      }
      client.println("}}");
    } else if (payload.indexOf("POST / HTTP") > -1) {
      debug_println(payload);
      // POST new node name and/or a new sensor location
      bool needSave = false;
      String dataline = payload.substring(payload.indexOf("\n\n")+2);
      String newNodeName = findData(dataline, "node"); 
      newNodeName.trim();
      if (isNewValue(nodeName, newNodeName)) {
        nodeName = newNodeName;
        needSave = true;
      }
      String id = findData(dataline, "id"); 
      String newLocation = findData(dataline, "location");
      newLocation.trim();
      newLocation.toLowerCase();
      if (id.length() > 0 && isNewValue(getSensorLocation(id), newLocation)) {
        getSensorData(id).location = newLocation;
        newLocation.replace(".", "/");
        getSensorData(id).topic = newLocation;
        needSave = true;
      }

      if (needSave) saveConfig();
      
      uint16_t sIndex = payload.indexOf("Referer: http://")+16;
      uint16_t eIndex = payload.indexOf("\n", sIndex);
      String ref = payload.substring(sIndex, eIndex);
      client.println("HTTP/1.1 303 See Other");
      client.print("Location: http://");
      client.println(ref);
    } else {
      client.println("HTTP/1.0 404 Not Found");
    }
  }
  // give the web browser time to receive the data
  delay(20);
}

void loadConfigHtml() {
  File f = SPIFFS.open(CONFIG_HTML, "r");
  if (!f) {
    Serial.println("File '"+String(CONFIG_HTML)+"' not found/open failed");
  } else {
    while(f.available()) {
      configHtml = configHtml + f.readString();
    }
    f.close();
  }
}

void loadConfigFile() {
  File f = SPIFFS.open(CONFIG_FILE, "r");
  if (!f) {
    Serial.println("File '"+String(CONFIG_FILE)+"' not found/open failed - use default values");
  } else {
    Serial.print("Load config file ...");
    uint8_t idx = 0;
    while(f.available() && idx < MAX_SENSORS) {
      String line = f.readStringUntil('\n');
      line.remove(line.length()-1); // remove CR 
      debug_println("line: '"+line+"'");

      if (line.indexOf("node=") >= 0) {
        nodeName = line.substring(sizeof("node=")-1);
        debug_println("-> node='"+nodeName+"'");
      
      } else if (line.indexOf("sensor-") >= 0) {        
        int eq = line.indexOf("=");
        String id = line.substring(sizeof("sensor-")-1, eq);
        String location = line.substring(eq+1);
        getSensorData(id).location = location;
        debug_println("-> Sensor("+id+")='"+location+"'");
        location.replace(".", "/");
        getSensorData(id).topic = location;
      }
      ++idx;
    }
    f.close();
    Serial.println(" - done");
  }
}

void loadConfig() {
  if (!SPIFFS.begin()) {
    Serial.println("Error while init SPIFFS.");
    return;
  }

  loadConfigHtml();
  loadConfigFile();

  SPIFFS.end();
}

void fetchAndSendSensorValues() {
  // request to all devices on the bus
  dsSensors.requestTemperatures();

  for (uint8_t i = 0; i < deviceCount; ++i) {
    char idbuf[17];
    float tempC = dsSensors.getTempC(deviceAddresses[i]);
    addr2hex(deviceAddresses[i], idbuf);
    getSensorData(idbuf).value = String(tempC, 2);
  }
  
  // measurand + location + node + sensor + value + fix + null
  // 20 + 30 + 30 + 20 + 20 + ",location=,node=,sensor= value=" + 1 => 100 + 23 + 1 = 144
  char dataLine[144]; 

  uint8_t idx = 0;
  while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
    sprintf(dataLine,"%s,location=%s,node=%s,sensor=%s value=%s", 
      sensors[idx].measurand.c_str(), 
      sensors[idx].location.c_str(), 
      nodeName.c_str(), 
      sensors[idx].type.c_str(), 
      sensors[idx].value.c_str());

    Serial.print(sensors[idx].topic);
    Serial.print(" ");
    Serial.println(dataLine);
    ++idx;
  }
  // cmd = "mosquitto_pub -h mqtt.thomo.de -m '" + db_line + "' -t f42/ground/bathroom"

  // printSensorData(idbuf);
}

Ticker timer1(fetchAndSendSensorValues, FETCH_SENSORS_CYCLE_SEC * 1000);

void setup(void) {
  for(uint8_t i=0; i<MAX_SENSORS; ++i) {
    sensors[i].id = "";
    sensors[i].type = "";
    sensors[i].location = "";
    sensors[i].measurand = "";
    sensors[i].value = "";
  }

  // start serial port
  Serial.begin(9600);

  // autoconnect timeout: 3min
  wifiManager.setConfigPortalTimeout(180);
  
  if(!wifiManager.autoConnect(DEFAULT_NODE_NAME)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  } 

  IPAddress myAddress = WiFi.localIP();
  Serial.println(myAddress);

  // Start up the library
  dsSensors.begin();
  deviceCount = dsSensors.getDeviceCount();

  // locate devices on the bus
  debug_print("Found ");
  debug_print(deviceCount);
  debug_println(" devices.");

  deviceAddresses = (DeviceAddress*) calloc(deviceCount, sizeof(DeviceAddress));

  // search for devices on the bus and assign based on an index.
  for (uint8_t i = 0; i < deviceCount; ++i) {
    if (!dsSensors.getAddress(deviceAddresses[i], i)) {
      Serial.print("Unable to find address for Device "); 
      Serial.print(i, DEC);
    } else {
      char hexbuf[17];
      addr2hex(deviceAddresses[i], hexbuf);
      addSensor(hexbuf, "DS18B20", "temperature");
    }
  }

  loadConfig();

  Serial.println("Run web server for configuration.");
  server.begin();

  timer1.start();
}

void loop(void) { 
  // listen for incoming clients
  client = server.available();
  if (client) {
    handleRequest();

    // close the connection:
    client.stop();    
  }

  timer1.update();
 }
