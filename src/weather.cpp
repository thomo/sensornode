#include "weather.h"
#include "secret.h"

String cityId = OPENWEATHERMAP_CITYID;
String apiKey = OPENWEATHERMAP_APIKEY;
String apiSrv = OPENWEATHERMAP_SRV;

WeatherClient::WeatherClient(WiFiClient* client) : _client(client) {
}

WeatherClient::~WeatherClient() {
}

bool WeatherClient::isValid() {
  return icon.length() > 0;
}

void WeatherClient::readWeatherData() {
  
  if (_client->connect(apiSrv, 80)) {
    _client->println("GET /data/2.5/weather?id="+cityId+"&appid="+apiKey);
    _client->println("Host: "+apiSrv);
    _client->println("User-Agent: ESP8266/1.0");
    _client->println("Connection: close");
    _client->println();
  } else {
    Serial.println("OpenWeatherApi connection failed!");
    return;
  }

  // reading sent data
  while(_client->connected() && !_client->available()) delay(1); //waits for data
  
  String payload = String("");
  // Serial.println("Waiting for data");

  while (_client->connected() && _client->available()) { 
    int c = _client->read();
    if (c >= 32) {
      payload += (char) c;
    } else if (c == '\n') {
      payload += (char) c;
    }
  }

  //Serial.printf("Got data: %s\n", payload.c_str());

  // example: {
  //   "coord":{"lon":x.xx,"lat":xx.xx},
  //   "weather":[{"id":804,"main":"Clouds","description":"overcast clouds","icon":"04n"}],
  //   "base":"stations",
  //   "main":{"temp":280.69,"feels_like":279.16,"temp_min":279.82,"temp_max":282.59,"pressure":1020,"humidity":86},
  //   "visibility":10000,
  //   "wind":{"speed":0.68,"deg":32},
  //   "clouds":{"all":92},
  //   "dt":1602956848,
  //   "sys":{"type":3,"id":2035315,"country":"DE","sunrise":1602914215,"sunset":1602952394},
  //   "timezone":7200,
  //   "id":city_id,
  //   "name":"xxxxxxxx",
  //   "cod":200
  // }
  
  int beginIdx = payload.indexOf("\"icon\":\"", payload.indexOf("\"weather\""));
  if (beginIdx > 0) {
    beginIdx += 8;
    icon = payload.substring(beginIdx, beginIdx+3) + ".bmp";
    beginIdx = payload.indexOf("\"temp\":", payload.indexOf("\"main\":"));
    beginIdx += 7;
    int endIdx = min(payload.indexOf(",",beginIdx), payload.indexOf("}",beginIdx));
    temperatur = payload.substring(beginIdx, endIdx).toFloat() - 273.15;
  } 
}

const String WeatherClient::getIcon() { 
  return icon;
}

const float WeatherClient::getTemperature() {
  return temperatur;
}