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

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature dsSensors(&oneWire);

uint8_t deviceCount = 0;

// to hold device addresses
DeviceAddress* deviceAddresses = NULL;

WiFiManager wifiManager;

WiFiServer server(80);

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
    Serial.print("Add sensor: type: ");
    Serial.print(type);
    Serial.print(" Id: 0x");
    Serial.println(id);
  }
}

void updateSensorName(String id, String name) {
  uint8_t idx = 0;
  while (!sensors[idx].id.equals(id) && idx < MAX_SENSORS) {++idx;}
  if (idx < MAX_SENSORS) {
    Serial.print("Set name ");
    Serial.print(sensors[idx].id);
    Serial.print(" = ");
    Serial.println(name);
    sensors[idx].name = name;
  }
}

uint16_t getPayload(WiFiClient &client, char payload[]) {
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

void printKV(WiFiClient &client, String k, String v) {
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
  } 
  return line.substring(sIndex);
} 

void saveConfig() {
  if (!SPIFFS.begin()) {
    Serial.println("Error while init SPIFFS.");
    return;
  }
  File f = SPIFFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.print(CONFIG_FILE);
    Serial.println(" open failed");
  } else {
    Serial.println("Save config file");
    f.println("location=" + location);
    Serial.println("Location: " + location);
    
    uint8_t idx = 0;
    while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
      f.println("sensor-" + sensors[idx].id + "=" + sensors[idx].name);
      Serial.println("Sensor: " + sensors[idx].id + "= " + sensors[idx].name);
      ++idx;
    }
    
    f.close();
  }

  SPIFFS.end();
}

void handleRequest(WiFiClient &client) {
  char payloadBuffer[PAYLOAD_BUFFER_SIZE];

  uint16_t payloadSize = getPayload(client, payloadBuffer);
  // Serial.println(payloadBuffer);

  if (payloadSize > 0) {
    String payload(payloadBuffer);
    if (payload.indexOf("GET / HTTP") > -1) {
      // send a standard http response header
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");  // the connection will be closed after completion of the response
      client.println();
      client.println("<!DOCTYPE HTML>");
      client.println(configHtml);
    } else if (payload.indexOf("GET /location HTTP") > -1) { 
      // GET /location
      client.println("HTTP/1.1 200 OK");
      client.println("Access-Control-Allow-Origin:null");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      client.print("{");
      printKV(client, "location", location);
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
        printKV(client, "name", sensors[idx].name); 
        client.print(", ");
        printKV(client, "type", sensors[idx].type); 
        client.print(", ");
        printKV(client, "value", sensors[idx].value); 
        client.print("} ");
        sep = ", ";
        ++idx;
      }
      client.println("}}");
    } else if (payload.indexOf("POST / HTTP") > -1) {
      // POST new sensor name
      String dataline = payload.substring(payload.indexOf("\n\n")+2);
      location = findData(dataline, "location"); 
      String newName = findData(dataline, "name"); 
      String id = findData(dataline, "id"); 
      if (id.length() > 0 && newName.length() > 0) {
        updateSensorName(id, newName);
      }

      saveConfig();

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
  delay(1);
}

void loadConfigHtml() {
  File f = SPIFFS.open(CONFIG_HTML, "r");
  if (!f) {
    Serial.print(CONFIG_HTML);
    Serial.println(" not found/open failed");
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
    Serial.print(CONFIG_FILE);
    Serial.println(" not found/open failed - use default values");
  } else {
    uint8_t idx = 0;
    while(f.available() && idx < MAX_SENSORS) {
      String line = f.readString();
      if (line.indexOf("location=") >= 0) {
        location = line.substring(sizeof("location="));
      } else if (line.indexOf("sensor%-") >= 0) {        // Note: %- escape the minus
        int eq = line.indexOf("=");
        updateSensorName(line.substring(sizeof("sensor%-"), eq), line.substring(eq));
      }
      ++idx;
    }
    f.close();
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
  Serial.print("Found ");
  Serial.print(deviceCount, DEC);
  Serial.println(" devices.");

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
  WiFiClient client = server.available();
  if (client) {
    handleRequest(client);

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
