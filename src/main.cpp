#include <Arduino.h>

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#define REQUIRESNEW false
#define REQUIRESALARMS false
#include <OneWire.h>
#include <DallasTemperature.h>

#include <Adafruit_BME280.h>
#include <Adafruit_Si7021.h>

#include <FS.h>
#include <Ticker.h>

#include <PubSubClient.h>

// +++++++++++++++++++

#define DEFAULT_NODE_NAME "F42-NODE"
#define MQTT_SERVER "mqtt.thomo.de"

#define DEFAULT_ROOT_TOPIC "tmp"

#define FETCH_SENSORS_CYCLE_SEC 10

#define ONE_WIRE_PIN 13
#define I2C_SDA_PIN  14
#define I2C_SCL_PIN  12

#define CONFIG_HTML "/config.html"
#define CONFIG_FILE "/config.cfg"
#define MAX_SENSORS 10

//                              " 30 " 
#define SIZE_JSONK_ID          (1+30+1)

//                              " 7 " : x
#define SIZE_JSONKV_ENABLED    (1+7+1+1+1)
//                              " 8 " : " 30 " 
#define SIZE_JSONKV_LOCATION   (1+8+1+1+1+30+1)
//                              " 4 " : " 30 " 
#define SIZE_JSONKV_TYPE       (1+4+1+1+1+30+1)
//                              " 9 " : " 30 " 
#define SIZE_JSONKV_MEASURAND  (1+9+1+1+1+30+1)
//                              " 5 " : " 20 " 
#define SIZE_JSONKV_VALUE      (1+5+1+1+1+20+1)
//                              " 10 " : " 7 " 
#define SIZE_JSONKV_CORRECTION (1+10+1+1+1+7+1)
//                              "___________"   :{  "______________": x   ,   "____":"___________"   ,   "_____":"______"   ,   "____":"____________"   ,   "_____":"_______"   ,   "_____":"____________"   }   ,
#define SIZE_JSON_ONE_SENSOR   (SIZE_JSONK_ID + 2 + SIZE_JSONKV_ENABLED + 1 + SIZE_JSONKV_LOCATION + 1 + SIZE_JSONKV_TYPE + 1 + SIZE_JSONKV_MEASURAND + 1 + SIZE_JSONKV_VALUE + 1 + SIZE_JSONKV_CORRECTION + 1 + 1)
          
#define SIZE_JSONV_SENSORS     (SIZE_JSON_ONE_SENSOR * MAX_SENSORS)

//                              {"node"     : " 20 "}
//                              {"topic"    : " 20 "}
//                              {"altitude" : " 7 "}
//                              { "sensors" : { SIZE_JSONV_SENSORS } }
#define SIZE_JSON_BUFFER       (1+1+  7  +1+1+1+SIZE_JSONV_SENSORS+1+1)

#define PAYLOAD_BUFFER_SIZE (1024+20+20+MAX_SENSORS*40) 

#define MYDEBUG 0
#define debug_print(line) \
            do { if (MYDEBUG) Serial.print(line); } while (0)
#define debug_println(line) \
            do { if (MYDEBUG) Serial.println(line); } while (0)

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_PIN);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature dsSensors(&oneWire);
uint8_t oneWireDeviceCount = 0;

String bmeAddr = "";
Adafruit_BME280 bme; // I2C

String shAddr = "";
Adafruit_Si7021 sh = Adafruit_Si7021(); // I2C

// to hold device addresses
DeviceAddress* deviceAddresses = NULL;

WiFiManager wifiManager;

ESP8266WebServer espServer(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

#define SIZE_WEBSENDBUFFER (SIZE_JSON_BUFFER)
char webSendBuffer[SIZE_WEBSENDBUFFER] = "";

String configHtml = "";
String nodeName = DEFAULT_NODE_NAME;
String rootTopic = DEFAULT_ROOT_TOPIC;
float nodeAltitude = 282.0f;

struct SensorData {
  bool enabled;
  String id;
  String type;
  String location;
  String topic;
  String measurand;
  String value;
  float correction;
};

SensorData tmpSensor = { 
  .enabled =false, 
  .id="", 
  .type="", 
  .location="", 
  .topic="", 
  .measurand="", 
  .value="",
  .correction=0.0f
};

SensorData sensors[MAX_SENSORS]; 

// ------------------------------------------------------------------------------------------------

void addr2hex(DeviceAddress da, char hex[17]) {
  snprintf(hex, 17, "%02X%02X%02X%02X%02X%02X%02X%02X", da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
}

uint8_t numberOfSensors() {
  uint8_t idx = 0;
  while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {++idx;}
  return idx;
}

void addSensor(const String& id, const String& type, const String& measurand) {
  uint8_t idx = 0;
  while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {++idx;}
  if (idx < MAX_SENSORS) {
    sensors[idx].enabled = false;
    sensors[idx].id = id;
    sensors[idx].type = type;
    sensors[idx].location = id;
    sensors[idx].topic = id;
    sensors[idx].measurand = measurand;
    sensors[idx].value = "?";
    debug_println("Add "+type+" sensor("+id+")");
  }
}

String getSensorLocation(const String& id) {
  uint8_t idx = 0;
  while (!sensors[idx].id.equals(id) && idx < MAX_SENSORS) {++idx;}
  return (idx < MAX_SENSORS) ? sensors[idx].location : ""; 
}

SensorData& getSensorData(const String& id) {
  uint8_t idx = 0;
  while (!sensors[idx].id.equals(id) && idx < MAX_SENSORS) {++idx;}
  return idx < MAX_SENSORS ? sensors[idx] : tmpSensor;
}

void updateSensorTopic(const String& id) {
  getSensorData(id).topic = rootTopic + "." + getSensorData(id).location;
  getSensorData(id).topic.replace(".", "/");
  debug_println("-> Sensor("+id+").topic = '"+getSensorData(id).topic+"'");
}

void updateSensorTopics() {
  uint8_t idx = 0;
  debug_println("Update sensor topics with rootTopic: " + rootTopic);
  while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
    updateSensorTopic(sensors[idx].id);
    ++idx;
  }
}

void printSensorData(const String& id) {
  SensorData sd = getSensorData(id);
  Serial.println(sd.type + " " + sd.location + "(" + sd.id + "): "+ sd.value);
}

void setSensorDataValue(const String& id, float v) {
  SensorData &sd = getSensorData(id);
  sd.value = String(v + sd.correction, 2);
}

void fetchSensorValues() {
  // request to all devices on the bus
  dsSensors.requestTemperatures();

  for (uint8_t i = 0; i < oneWireDeviceCount; ++i) {
    char idbuf[17];
    float tempC = dsSensors.getTempC(deviceAddresses[i]);
    addr2hex(deviceAddresses[i], idbuf);
    setSensorDataValue(idbuf, tempC);
  }

  if (bmeAddr.length() > 0) {
    setSensorDataValue(bmeAddr+"t",bme.readTemperature());
    setSensorDataValue(bmeAddr+"h",bme.readHumidity());
    setSensorDataValue(bmeAddr+"p",bme.seaLevelForAltitude(nodeAltitude, bme.readPressure()));
  }

  if (shAddr.length() > 0) {
    setSensorDataValue(shAddr+"h",sh.readHumidity());
    setSensorDataValue(shAddr+"t",sh.readTemperature());
  }
}

uint16_t getPayload(char payload[]) {
  uint16_t index=0;
  while (espClient.connected() && espClient.available()) {
    if (index < PAYLOAD_BUFFER_SIZE - 1) {
      int c = espClient.read();
      if (c >= 32) {
        payload[index++] = (char) c;
      } else if (c == '\n') {
        payload[index++] = (char) c;
      }   
    }
  }
  payload[index] = '\0';
  return index;
}

void printKV(const String& k, const String& v) {
  espClient.print("\"");
  espClient.print(k);
  espClient.print("\":\"");
  espClient.print(v);
  espClient.print("\"");
}

String findData(const String& line, const String& key) {
  int sIndex = line.indexOf(key+"=");
  if (sIndex < 0) return "";
  sIndex += (key.length() + 1);

  String result;
  int eIndex = line.indexOf("&", sIndex);
  if (eIndex > 0) {
    result = line.substring(sIndex, eIndex);
  } else {
    result = line.substring(sIndex);
  }
  
  result.trim();
  result.toLowerCase();
  return result;
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
    Serial.print("Save config file ... ");
    debug_println("");

    String line = "node=" + nodeName;
    f.println(line);
    debug_println("<- " + line);

    line = "topic=" + rootTopic;
    f.println(line);
    debug_println("<- " + line);
    
    line = "altitude=" + String(nodeAltitude, 2);
    f.println(line);
    debug_println("<- " + line);
    
    uint8_t idx = 0;
    while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
      line = "sensor-" + sensors[idx].id + "=" + sensors[idx].location;
      f.println(line);
      debug_println("<- " + line);

      line = "sensor.enabled-" + sensors[idx].id + "=" + sensors[idx].enabled;
      f.println(line);
      debug_println("<- " + line);
      
      line = "sensor.correction-" + sensors[idx].id + "=" + sensors[idx].correction;
      f.println(line);
      debug_println("<- " + line);
      
      ++idx;
    }
    
    f.close();

    Serial.println("done.");
  }

  SPIFFS.end();
}

bool isNewValue(const String& oldValue, const String& newValue) {
  return newValue.length() > 0 && !oldValue.equals(newValue);
}

void handleGetRoot() {
  espServer.send(200, "text/html", configHtml);   
}

void handleGetNode() {
  snprintf(webSendBuffer, SIZE_WEBSENDBUFFER, "{\"node\":\"%s\"}", nodeName.c_str());
  espServer.send(200, "application/json", webSendBuffer);   
}

void handleGetTopic() {
  snprintf(webSendBuffer, SIZE_WEBSENDBUFFER, "{\"topic\":\"%s\"}", rootTopic.c_str());
  espServer.send(200, "application/json", webSendBuffer);   
}

void handleGetAltitude() {
  snprintf(webSendBuffer, SIZE_WEBSENDBUFFER, "{\"altitude\":\"%-.2f\"}", nodeAltitude);
  espServer.send(200, "application/json", webSendBuffer);   
}

void handleGetSensors() {
  strcpy(webSendBuffer, "{\"sensors\":{");

  char sep[2];
  sep[0]='\0';
  sep[1]='\0';

  char oneSensorBuf[SIZE_JSON_ONE_SENSOR];
  uint8_t idx = 0;
  while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
    snprintf(oneSensorBuf, SIZE_JSON_ONE_SENSOR, 
            "%s\"%s\":{\"enabled\":%d,\"location\":\"%s\",\"type\":\"%s\",\"measurand\":\"%s\",\"value\":\"%s\",\"correction\":\"%-.2f\"}", 
            sep, 
            sensors[idx].id.c_str(), 
            sensors[idx].enabled, 
            sensors[idx].location.c_str(), 
            sensors[idx].type.c_str(), 
            sensors[idx].measurand.c_str(), 
            sensors[idx].value.c_str(),
            sensors[idx].correction
            );
    sep[0]=',';
    strncat(webSendBuffer, oneSensorBuf, SIZE_WEBSENDBUFFER - strlen(webSendBuffer));
    ++idx;
  }
  strncat(webSendBuffer, "}}", SIZE_WEBSENDBUFFER - strlen(webSendBuffer));
  espServer.send(200, "application/json", webSendBuffer);   
}

// POST new node name and/or a new sensor location
void handlePostRoot() {
  String content = espServer.arg("plain");
  debug_println("Request content: '" + content + "'");

  bool needSave = false;
  bool needSensorFetch = false;
  String newValue;

  newValue = findData(content, "node"); 
  if (isNewValue(nodeName, newValue)) {
    nodeName = newValue;
    needSave = true;
  }

  newValue = findData(content, "topic");
  if (isNewValue(rootTopic, newValue)) {
    rootTopic = newValue;
    updateSensorTopics();
    needSave = true;
  }

  newValue = findData(content, "altitude");
  if (isNewValue(String(nodeAltitude, 2), newValue)) {
    nodeAltitude = newValue.toFloat();
    needSensorFetch = true;
    needSave = true;
  }

  uint8_t idx = 0;
  while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
    String key = "loc-" + sensors[idx].id;
    newValue = findData(content, key);
    if (isNewValue(sensors[idx].location, newValue)) {
      sensors[idx].location = newValue;
      updateSensorTopic(sensors[idx].id);
      needSave = true;
    }

    key = "en-" + sensors[idx].id;
    newValue = findData(content, key);
    bool newEnabled = newValue.equals("on");
    needSave = needSave || (sensors[idx].enabled == newEnabled);
    sensors[idx].enabled = newEnabled;

    key = "cor-" + sensors[idx].id;
    newValue = findData(content, key);
    if (isNewValue(String(sensors[idx].correction, 2), newValue)) {
      sensors[idx].correction = newValue.toFloat();
      needSave = true;
      needSensorFetch = true;
    }

    ++idx;
  }

  if (needSave) saveConfig();
  if (needSensorFetch) fetchSensorValues();
  
  espServer.sendHeader("Location", "/");
  espServer.send(303);
}

void handleError() {
  espServer.send(404, "text/plain", "404: Not found");
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
    Serial.print("Load config file ... ");
    debug_println("");
    uint8_t idx = 0;
    while(f.available() && idx < MAX_SENSORS) {
      String line = f.readStringUntil('\n');
      line.remove(line.length()-1); // remove CR f
      debug_println("cfgline: '"+line+"'");

      if (line.indexOf("node=") >= 0) {
        nodeName = line.substring(sizeof("node=")-1);
        debug_println("-> node='"+nodeName+"'");
      } else if (line.indexOf("topic=") >= 0) {
        rootTopic = line.substring(sizeof("topic=")-1);
        debug_println("-> rootTopic='"+rootTopic+"'");
        updateSensorTopics();
      } else if (line.indexOf("altitude=") >= 0) {
        String altitude = line.substring(sizeof("altitude=")-1);
        nodeAltitude = altitude.toFloat();
        debug_println("-> altitude='"+altitude+"'");
      } else if (line.indexOf("sensor.enabled-") >= 0) {        
        int eq = line.indexOf("=");
        String id = line.substring(sizeof("sensor.enabled-") - 1, eq);
        String enabled = line.substring(eq+1);
        getSensorData(id).enabled = enabled.toInt();
        debug_println("-> Sensor("+id+").enabled="+enabled);
        idx = numberOfSensors();
      } else if (line.indexOf("sensor.correction-") >= 0) {        
        int eq = line.indexOf("=");
        String id = line.substring(sizeof("sensor.correction-") - 1, eq);
        String correction = line.substring(eq+1);
        getSensorData(id).correction = correction.toFloat();
        debug_println("-> Sensor("+id+").correction="+correction);
        idx = numberOfSensors();
      } else if (line.indexOf("sensor-") >= 0) {        
        int eq = line.indexOf("=");
        String id = line.substring(sizeof("sensor-") - 1, eq);
        String location = line.substring(eq + 1);
        getSensorData(id).location = location;
        debug_println("-> Sensor("+id+").location='"+location+"'");
        updateSensorTopic(id);
        idx = numberOfSensors();
      }
      debug_println("-- #Sensors: " + String(idx) + " ----------------------------------");
    }
    f.close();

    Serial.println("done.");
  }
}

void loadConfig() {
  debug_println("loadConfig:");
  if (SPIFFS.begin()) {
    debug_println("Init SPIFFS - successful.");
  } else {
    Serial.println("Error while init SPIFFS.");
    return;
  }

  loadConfigHtml();
  loadConfigFile();

  SPIFFS.end();
}

void sendMQTTData() {
  // measurand + location + node + sensor + value + fix + null
  // 20 + 30 + 30 + 20 + 20 + ",location=,node=,sensor= value=" + 1 => 100 + 23 + 1 = 144
  char dataLine[144]; 

  uint8_t idx = 0;
  while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
    if (sensors[idx].enabled) {
      sprintf(dataLine,"%s,location=%s,node=%s,sensor=%s value=%s", 
        sensors[idx].measurand.c_str(), 
        sensors[idx].location.c_str(), 
        nodeName.c_str(), 
        sensors[idx].type.c_str(), 
        sensors[idx].value.c_str());

      mqttClient.publish(sensors[idx].topic.c_str(), dataLine);
      debug_print("MQTT: " + sensors[idx].topic + " ");
      debug_println(dataLine);
      Serial.print(".");
    }

    ++idx;
  }
}

void mqttReconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection ... ");
    // Attempt to connect
    if (mqttClient.connect(nodeName.c_str())) {
      Serial.println("success.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(". Try again in 5 seconds.");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setupOneWireSensors() {
  // Start up the library
  dsSensors.begin();
  oneWireDeviceCount = dsSensors.getDeviceCount();

  // locate devices on the bus
  Serial.print("Found ");
  Serial.print(oneWireDeviceCount);
  Serial.println(" OneWire devices.");

  deviceAddresses = (DeviceAddress*) calloc(oneWireDeviceCount, sizeof(DeviceAddress));

  // search for devices on the bus and assign based on an index.
  for (uint8_t i = 0; i < oneWireDeviceCount; ++i) {
    if (!dsSensors.getAddress(deviceAddresses[i], i)) {
      Serial.print("Unable to find address for Device "); 
      Serial.print(i, DEC);
    } else {
      char hexbuf[17];
      addr2hex(deviceAddresses[i], hexbuf);
      addSensor(hexbuf, "DS18B20", "temperature");
    }
  }
}

void setupI2CSensors() {
  Wire.begin(I2C_SDA_PIN,I2C_SCL_PIN);
  if (bme.begin(0x76)) {  
    bmeAddr = "76";
  } else if (bme.begin(0x77)) {
    bmeAddr = "77";
  } else {
    Serial.println("No BME280 sensor found.");
  }

  if (bmeAddr.length() > 0) {
    Serial.println("Found BME280 sensor on 0x" + bmeAddr);
    addSensor(bmeAddr+"h", "BME280", "humidity");
    addSensor(bmeAddr+"p", "BME280", "pressure");
    addSensor(bmeAddr+"t", "BME280", "temperature");
  }

  if (sh.begin()) {
    String model = "";
    switch(sh.getModel()) {
      case SI_7013:
        model="Si7013"; break;
      case SI_7020:
        model="Si7020"; break;
      case SI_7021:
        model="Si7021"; break;
      default:
        model="Si70xx";
    }
    Serial.println("Found "+model+" sensor!");
    shAddr = "40";
    addSensor(shAddr+"h", model, "humidity");
    addSensor(shAddr+"t", model, "temperature");
  }
}

void mqttUpdate() {
  fetchSensorValues();
  sendMQTTData();
}

Ticker timer1(mqttUpdate, FETCH_SENSORS_CYCLE_SEC * 1000);

void setup(void) {
  for(uint8_t i=0; i<MAX_SENSORS; ++i) {
    sensors[i].enabled = false;
    sensors[i].id = "";
    sensors[i].type = "";
    sensors[i].location = "";
    sensors[i].topic=""; 
    sensors[i].measurand = "";
    sensors[i].value = "";
    sensors[i].correction = 0.0f;
  }

  // start serial port
  Serial.begin(115200);

  // autoconnect timeout: 3min
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setDebugOutput(false);

  Serial.print("Try WIFI connect ... ");
  if(!wifiManager.autoConnect(DEFAULT_NODE_NAME)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  } 

  IPAddress myAddress = WiFi.localIP();
  Serial.print("done. IP: ");
  Serial.println(myAddress);

  setupOneWireSensors();
  setupI2CSensors();

  loadConfig();

  espServer.on("/", HTTP_GET, handleGetRoot);
  espServer.on("/node", HTTP_GET, handleGetNode);
  espServer.on("/sensors", HTTP_GET, handleGetSensors);
  espServer.on("/topic", HTTP_GET, handleGetTopic);
  espServer.on("/altitude", HTTP_GET, handleGetAltitude);

  espServer.on("/", HTTP_POST, handlePostRoot);

  espServer.onNotFound(handleError);

  Serial.println("Run web server for configuration.");
  espServer.begin();

  mqttClient.setServer(MQTT_SERVER, 1883);
  
  timer1.start();
}

void loop(void) { 
  espServer.handleClient();

  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();

  timer1.update();
 }
