#include <Arduino.h>
#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ezTime.h>
#include <PubSubClient.h>

#define REQUIRESNEW false
#define REQUIRESALARMS false
#include <OneWire.h>
#include <DallasTemperature.h>

#include <Adafruit_BME280.h>
#include <Adafruit_Si7021.h>
#include <Adafruit_HTU21DF.h>

#include <FS.h>
#include <LittleFS.h>

#include <Ticker.h>

// +++++++++++++++++++

#ifndef SENSORNODE_VERSION
#define SENSORNODE_VERSION 1
#endif

#define DEFAULT_NODE_NAME "F42-NODE"
#define MQTT_SERVER "mqtt.thomo.de"

#define DEFAULT_ROOT_TOPIC "tmp"

// +++++++++++++++++++

#include "weather.h"
#include "disp.h"

// +++++++++++++++++++
#if SENSORNODE_VERSION > 1
#define ONE_WIRE_PIN 0
#define I2C_SDA_PIN  4
#define I2C_SCL_PIN  5
#else
#define ONE_WIRE_PIN 13
#define I2C_SDA_PIN  14
#define I2C_SCL_PIN  12
#endif


#define CONFIG_HTML "/config.html"
#define CONFIG_FILE "/config.cfg"
#define MAX_SENSORS 10

#define SIZE_JSON_ONE_SENSOR   (sizeof("'123456789012345678901234567890':{'e':1,'l':'123456789012345678901234567890','t':'123456789012345678901234567890','m':'123456789012345678901234567890','v':'12345678901234567890','c':'1234567','d':1},"))
#define SIZE_JSONV_SENSORS     (SIZE_JSON_ONE_SENSOR * MAX_SENSORS)
#define SIZE_JSON_BUFFER       (sizeof("{'sensors':{") + SIZE_JSONV_SENSORS + sizeof("}}"))
#define SIZE_WEBSENDBUFFER     (SIZE_JSON_BUFFER)

#define PAYLOAD_BUFFER_SIZE    (1024+20+20+MAX_SENSORS*40) 

#define FIXED_FONT 2

#define MYDEBUG 0
#define debug_print(line) \
            do { if (MYDEBUG) Serial.print(line); } while (0)
#define debug_println(line) \
            do { if (MYDEBUG) Serial.println(line); } while (0)
#define debug_printf(frmt, ...) \
            do { if (MYDEBUG) Serial.printf(frmt, __VA_ARGS__); } while (0)

// --- update cadence ---
uint16_t updateSensorsTimeout = 30;
uint16_t updateWeatherForecastTimeout = 5*60;

// --- SENSORS ---
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_PIN);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature dsSensors(&oneWire);
uint8_t oneWireDeviceCount = 0;

String bmeAddr = "";
Adafruit_BME280 bme; // I2C

String si70xxAddr = "";
Adafruit_Si7021 si70xx = Adafruit_Si7021(); // I2C

String htu21Addr = "";
Adafruit_HTU21DF htu21 = Adafruit_HTU21DF(); // I2C

// --- Display --- 
#if SENSORNODE_VERSION > 1
boolean hasDisplay = true;
#else
boolean hasDisplay = false;
#endif
TFT_eSPI tft = TFT_eSPI();

// --- Network ---
WiFiManager wifiManager;
ESP8266WebServer espServer(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Timezone myTZ;

WeatherClient wc = WeatherClient(&espClient);

char webSendBuffer[SIZE_WEBSENDBUFFER] = "";

String configHtml = "";
String nodeName = DEFAULT_NODE_NAME;
String rootTopic = DEFAULT_ROOT_TOPIC;
float nodeAltitude = 282.0f;

String showSensor = "";

struct SensorData {
  bool enabled;
  DeviceAddress addr;
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
  .addr = {0}, 
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

void update();
void fetchWeatherData();
void setupDisplay();
void updateDisplay();

Ticker timer1(update, updateSensorsTimeout * 1000);
Ticker timer2(fetchWeatherData, updateWeatherForecastTimeout * 1000);

void addr2hex(const uint8_t* da, char hex[17]) {
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
    float tempC = dsSensors.getTempC(sensors[i].addr);
    setSensorDataValue(sensors[i].id, tempC);
  }

  if (bmeAddr.length() > 0) {
    setSensorDataValue(bmeAddr+"t",bme.readTemperature());
    setSensorDataValue(bmeAddr+"h",bme.readHumidity());
    setSensorDataValue(bmeAddr+"p",bme.seaLevelForAltitude(nodeAltitude, bme.readPressure()));
  }

  if (si70xxAddr.length() > 0) {
    setSensorDataValue(si70xxAddr+"h",si70xx.readHumidity());
    setSensorDataValue(si70xxAddr+"t",si70xx.readTemperature());
  }

  if (htu21Addr.length() > 0) {
    setSensorDataValue(htu21Addr+"h",htu21.readHumidity());
    setSensorDataValue(htu21Addr+"t",htu21.readTemperature());
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

void writeConfigLine(File& f, const String& line) {
    f.println(line);
    debug_println("<- " + line);
}

void saveConfig() {
  if (!LittleFS.begin()) {
    Serial.println("Error while init LittleFS.");
    return;
  }
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println("File '"+String(CONFIG_FILE)+"' open to write failed");
  } else {
    Serial.print("Save config file ... ");
    debug_println("");

    writeConfigLine(f, "node=" + nodeName);
    writeConfigLine(f, "topic=" + rootTopic);
    writeConfigLine(f, "altitude=" + String(nodeAltitude, 2));
    writeConfigLine(f, "sto=" + String(updateSensorsTimeout, 10));
    writeConfigLine(f, "wfcto=" + String(updateWeatherForecastTimeout, 10));

    if (hasDisplay) {
      writeConfigLine(f, "hasDisplay");
    }
    
    if (showSensor.length() > 0) {
      writeConfigLine(f, "show=" + showSensor);
    }
    
    uint8_t idx = 0;
    while (sensors[idx].id.length() > 0 && idx < MAX_SENSORS) {
      writeConfigLine(f, "sensor-" + sensors[idx].id + "=" + sensors[idx].location);
      writeConfigLine(f, "sensor.enabled-" + sensors[idx].id + "=" + sensors[idx].enabled);
      writeConfigLine(f, "sensor.correction-" + sensors[idx].id + "=" + sensors[idx].correction);
      ++idx;
    }
    
    f.close();
    Serial.println("done.");
  }

  LittleFS.end();
}

bool isNewValue(const String& oldValue, const String& newValue) {
  return newValue.length() > 0 && !oldValue.equals(newValue);
}

void handleGetRoot() {
  espServer.send(200, "text/html", configHtml);   
}

void handleGetConfig() {
  snprintf(webSendBuffer, SIZE_WEBSENDBUFFER, 
    "{\"v\":%d,\"sf\":%d,\"ff\":%d,\"n\":\"%s\",\"t\":\"%s\",\"a\":\"%-.2f\",\"d\":%d}", 
    SENSORNODE_VERSION,
    updateSensorsTimeout,
    updateWeatherForecastTimeout,
    nodeName.c_str(),
    rootTopic.c_str(),
    nodeAltitude,
    hasDisplay
  );
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
            //     keys: e, l, t, m, v, c, s
            "%s\"%s\":{\"e\":%d,\"l\":\"%s\",\"t\":\"%s\",\"m\":\"%s\",\"v\":\"%s\",\"c\":\"%-.2f\",\"s\":%d}", 
            sep, 
            sensors[idx].id.c_str(), 
            sensors[idx].enabled, 
            sensors[idx].location.c_str(), 
            sensors[idx].type.c_str(), 
            sensors[idx].measurand.c_str(), 
            sensors[idx].value.c_str(),
            sensors[idx].correction,
            showSensor.equals(sensors[idx].id));
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

  newValue = findData(content, "sf");
  if (isNewValue(String(updateSensorsTimeout, 10), newValue)) {
    updateSensorsTimeout = max(1L,newValue.toInt());
    timer1.interval(updateSensorsTimeout * 1000);
    needSave = true;
  }

#if SENSORNODE_VERSION > 1
  newValue = findData(content, "ff");
  if (isNewValue(String(updateWeatherForecastTimeout, 10), newValue)) {
    updateWeatherForecastTimeout = newValue.toInt();
    timer2.interval(updateWeatherForecastTimeout * 1000);
    needSave = true;
  }

  newValue = findData(content, "hasDisplay");
  if (hasDisplay != (newValue.length() > 0)) {
    hasDisplay = newValue.length() > 0;
    needSave = true;
    setupDisplay();
  }
#else
  hasDisplay = false;
#endif

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

    key = "show";
    newValue = findData(content, key);
    if (isNewValue(showSensor, newValue)) {
      showSensor = newValue;
      needSave = true;
    }

    ++idx;
  }

  if (needSave) saveConfig();
  if (needSensorFetch) fetchSensorValues();
  
  if (needSave || needSensorFetch) updateDisplay();

  espServer.sendHeader("Location", "/");
  espServer.send(303);
}

void handleError() {
  espServer.send(404, "text/plain", "404: Not found");
}

void loadConfigHtml() {
  configHtml = "";
  File f = LittleFS.open(CONFIG_HTML, "r");
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
  File f = LittleFS.open(CONFIG_FILE, "r");
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
      } else if (line.indexOf("sto=") >= 0) {
        updateSensorsTimeout = line.substring(sizeof("sto=")-1).toInt();
        debug_printf("-> sto='%d'\n", updateSensorsTimeout);
      } else if (line.indexOf("wfcto=") >= 0) {
        updateWeatherForecastTimeout = line.substring(sizeof("wfcto=")-1).toInt();
        debug_printf("-> wfcto='%d'\n", updateWeatherForecastTimeout);
      } else if (line.indexOf("hasDisplay") >= 0) {
#if SENSORNODE_VERSION > 1
        hasDisplay = true;
        debug_println("-> hasDisplay");
#else
        hasDisplay = false;
#endif
      } else if (line.indexOf("show") >= 0) {
        showSensor = line.substring(sizeof("show=")-1);
        debug_println("-> show='" + showSensor + "'");
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
  if (LittleFS.begin()) {
    debug_println("Init LittleFS - successful.");
  } else {
    Serial.println("Error while init LittleFS.");
    return;
  }

  loadConfigHtml();
  loadConfigFile();

  LittleFS.end();
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
  Serial.printf("Found %d OneWire devices.\n", oneWireDeviceCount);

  DeviceAddress addr;
  // search for devices on the bus and assign based on an index.
  for (uint8_t i = 0; i < oneWireDeviceCount; ++i) {
    if (!dsSensors.getAddress(addr, i)) {
      Serial.print("Unable to find address for Device "); 
      Serial.print(i, DEC);
    } else {
      char hexbuf[17];
      addr2hex(addr, hexbuf);
      addSensor(hexbuf, "DS18B20", "temperature");
      for (uint8_t b = 0; b < 8; ++b) {
        sensors[i].addr[b] = addr[b];
      }
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

  if (si70xx.begin()) {
    String model = "";
    switch(si70xx.getModel()) {
      case SI_7013:
        model="Si7013"; break;
      case SI_7020:
        model="Si7020"; break;
      case SI_7021:
        model="Si7021"; break;
      case SI_Engineering_Samples:
        model="Si70SS"; break;
      default:
        model="Si70xx";
    }
    Serial.println("Found "+model+" sensor!");
    si70xxAddr = "40";
    addSensor(si70xxAddr+"h", model, "humidity");
    addSensor(si70xxAddr+"t", model, "temperature");
  } else {
    Serial.println("No Si70xx sensor found.");
  }

  if (si70xxAddr.length() == 0 && htu21.begin()) { // si70xx and htu21 have the same i2c addr 0x40
    Serial.println("Found HTU21 sensor!");
    htu21Addr = "40";
    addSensor(htu21Addr+"h", "HTU21", "humidity");
    addSensor(htu21Addr+"t", "HTU21", "temperature");
  } else {
    Serial.println("No HTU21 sensor found.");
  }
}

void setupDisplay() {
  if (hasDisplay) {
    tft.init();
    tft.setRotation(1);

    tft.fillScreen(TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(TFT_BLACK);
  }
}

void fetchWeatherData() {
  if (hasDisplay) {
    wc.readWeatherData();
  }
}

void updateDisplay() {
  if (hasDisplay) {
    char inVal[9] = "---.-°C"; // -xx.x°C
    if (showSensor.length() > 0) {
      SensorData sd = getSensorData(showSensor);
      float val = ::atof(sd.value.c_str());
      if (sd.measurand == "temperature") {
        sprintf(inVal, "%-.1f°C", val);
      } else if (sd.measurand == "humidity") {
        sprintf(inVal, "%-.1f%%", val);
      } else {
        sprintf(inVal, "???");
      }
    }
    
    String outIcon = "wait.bmp";
    char outTemp[9] = "---.-°C"; // -xx.x°C
    if (wc.isValid()) {
      sprintf(outTemp, "%-.1f°C", wc.getTemperature());
      outIcon = wc.getIcon();
    }

    char timebuf[6];
    sprintf(timebuf, "%02d:%02d", myTZ.hour(), myTZ.minute());
    char datebuf[7];
    sprintf(datebuf, "%02d.%02d.", myTZ.day(), myTZ.month());
    display(tft, inVal, outTemp, outIcon.c_str(), timebuf, datebuf);
  }
}

void update() {
  fetchSensorValues();
  sendMQTTData();
  updateDisplay();
}

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
  Serial.println();

  setupOneWireSensors();
  setupI2CSensors();

  loadConfig();

  setupDisplay();

  // autoconnect timeout: 3min
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setDebugOutput(false);

  Serial.print("Try WIFI connect ... ");
  if (hasDisplay) {
    tft.drawString("Try WIFI connect ... ", 10, 5, FIXED_FONT);
  }
  if(!wifiManager.autoConnect(DEFAULT_NODE_NAME)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  } 

  IPAddress myAddress = WiFi.localIP();
  Serial.println("done. IP: " + myAddress.toString());
  if (hasDisplay) {
    tft.drawString("done. IP: "+ myAddress.toString(), 10, 5+12, FIXED_FONT);
  }

  if (hasDisplay) {
    tft.drawString("Sync with NTP ...", 10, 5+12+12, FIXED_FONT);
  }
  waitForSync();
	myTZ.setLocation(F("de"));

  espServer.on("/", HTTP_GET, handleGetRoot);
  espServer.on("/config", HTTP_GET, handleGetConfig);
  espServer.on("/sensors", HTTP_GET, handleGetSensors);

  espServer.on("/", HTTP_POST, handlePostRoot);

  espServer.onNotFound(handleError);

  Serial.println("Run web server for configuration.");
  espServer.begin();

  mqttClient.setServer(MQTT_SERVER, 1883);
  
  timer1.start();
  timer2.start();

  fetchWeatherData();

  // ArduinoOTA.setPort(8266);
  // ArduinoOTA.setPassword("admin");
  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      // U_FS
      type = "filesystem";
      LittleFS.end();
    }
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    } 
  });

  ArduinoOTA.begin(false);
}

void loop(void) { 
  espServer.handleClient();

  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();

  timer1.update(); 
  timer2.update(); 

  ArduinoOTA.handle();
 }
