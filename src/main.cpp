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

// +++++++++++++++++++

#define NODE_NAME "F42-NODE"
#define DEFAULT_LOCATION "laboratory"

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

String location = DEFAULT_LOCATION;

typedef struct {
  String id;
  String type;
  String name;
  String value;
} SensorStruct;

SensorStruct sensors[MAX_SENSORS] = {
  {"", "", "", ""}, 
  {"", "", "", ""}, 
  {"", "", "", ""}, 
  {"", "", "", ""}, 
  {"", "", "", ""},
  {"", "", "", ""}, 
  {"", "", "", ""}, 
  {"", "", "", ""}, 
  {"", "", "", ""}, 
  {"", "", "", ""}
};

// ------------------------------------------------------------------------------------------------

void addr2hex(DeviceAddress da, char hex[16]) {
  sprintf(hex, "%02X%02X%02X%02X%02X%02X%02X%02X", da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
}

// function to print the temperature for a device
void printTemperature(DeviceAddress da) {
  float tempC = dsSensors.getTempC(da);
  Serial.print("Temp C: ");
  Serial.print(tempC);
  Serial.print(" Temp F: ");
  Serial.print(DallasTemperature::toFahrenheit(tempC));
}

void addSensor(String id, String type) {
  uint8_t idx = 0;
  while (sensors[idx].id != "" && idx < MAX_SENSORS) {++idx;}
  if (idx < MAX_SENSORS) {
    sensors[idx].id = id;
    sensors[idx].type = type;
    sensors[idx].name = id;
    sensors[idx].value = "?";
    debug_println("Add "+type+" sensor("+id+")");
  }
}

String getSensorName(String id) {
  uint8_t idx = 0;
  while (!sensors[idx].id.equals(id) && idx < MAX_SENSORS) {++idx;}
  return (idx < MAX_SENSORS) ? sensors[idx].name : ""; 
}


void updateSensorName(String id, String name) {
  uint8_t idx = 0;
  while (!sensors[idx].id.equals(id) && idx < MAX_SENSORS) {++idx;}
  if (idx < MAX_SENSORS) {
    sensors[idx].name = name;
  }
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
    
    f.println("location=" + location);
    debug_println("<- location='" + location+"'");
    
    uint8_t idx = 0;
    while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
      f.println("sensor-" + sensors[idx].id + "=" + sensors[idx].name);
      debug_println("<- Sensor(" + sensors[idx].id + ")='" + sensors[idx].name+"'");
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
    } else if (payload.indexOf("GET /location HTTP") > -1) { 
      // GET /location
      client.println("HTTP/1.1 200 OK");
      client.println("Access-Control-Allow-Origin:null");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      client.print("{");
      printKV("location", location);
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
        printKV("name", sensors[idx].name); 
        client.print(", ");
        printKV("type", sensors[idx].type); 
        client.print(", ");
        printKV("value", sensors[idx].value); 
        client.print("} ");
        sep = ", ";
        ++idx;
      }
      client.println("}}");
    } else if (payload.indexOf("POST / HTTP") > -1) {
      debug_println(payload);
      // POST new sensor name
      bool needSave = false;
      String dataline = payload.substring(payload.indexOf("\n\n")+2);
      String newLocation = findData(dataline, "location"); 
      if (isNewValue(location, newLocation)) {
        location = newLocation;
        needSave = true;
      }
      String id = findData(dataline, "id"); 
      String newName = findData(dataline, "name");
      if (id.length() > 0 && isNewValue(getSensorName(id), newName)) {
        updateSensorName(id, newName);
        needSave = true;
      }
      
      uint16_t sIndex = payload.indexOf("Referer: http://")+16;
      uint16_t eIndex = payload.indexOf("\n", sIndex);
      String ref = payload.substring(sIndex, eIndex);
      client.println("HTTP/1.1 303 See Other");
      client.print("Location: http://");
      client.println(ref);

      if (needSave) saveConfig();
    } else {
      client.println("HTTP/1.0 404 Not Found");
    }
  }
  // give the web browser time to receive the data
  delay(1);
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
      if (line.indexOf("location=") >= 0) {
        location = line.substring(sizeof("location=")-1);
        debug_println("-> location='" + location+"'");
      } else if (line.indexOf("sensor-") >= 0) {        // Note: %- escape the minus
        int eq = line.indexOf("=");
        String id = line.substring(sizeof("sensor-")-1, eq);
        String name = line.substring(eq+1);
        updateSensorName(id, name);
        debug_println("-> Sensor("+id+")='"+name+"'");
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

void setup(void) {
  // start serial port
  Serial.begin(9600);

  // autoconnect timeout: 3min
  wifiManager.setConfigPortalTimeout(180);
  
  if(!wifiManager.autoConnect(NODE_NAME)) {
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
      char hexbuf[16];
      addr2hex(deviceAddresses[i], hexbuf);
      addSensor(hexbuf, "DS18B20");
    }
  }

  loadConfig();

  Serial.println("Run web server for configuration.");
  server.begin();
}

void loop(void)
{ 
  // listen for incoming clients
  client = server.available();
  if (client) {
    handleRequest();

    // close the connection:
    client.stop();    
  }

  // call dsSensors.requestTemperatures() to issue a global temperature 
  // request to all devices on the bus
  // Serial.print("Requesting temperatures...");
  // dsSensors.requestTemperatures();
  // Serial.println("DONE");

  // for (uint8_t i = 0; i < deviceCount; ++i) {
  //   printData(deviceAddresses[i]);
  // }
 }
