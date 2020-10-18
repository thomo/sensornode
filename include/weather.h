#ifndef _weather_h_
#define _weather_h_

#include <ESP8266WiFi.h>

#define OPENWEATHERMAP_SRV "api.openweathermap.org"


// get Weather data from openweathermap.org
// sent request for data
class WeatherClient {

    public:
        WeatherClient(WiFiClient* client);
        virtual ~WeatherClient();

        void readWeatherData();

        const String getIcon();
        const float getTemperature();
        bool isValid();

    protected:
        WiFiClient* _client;
        String icon;
        float temperatur;
};

#endif