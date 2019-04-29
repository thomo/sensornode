# SensorNode

A ESP8266 based solution with support for

- DS18B20 1-Wire temperature sensor
- BME280 humidity, pressure, temperature sensor (I2C) - Note: temperature is measured in-chip, so it is usually too inaccurate to measure the environment temperature
- Si7021 humidity, temperatur sensor (I2C)

The software will determine which sensors are connected. Because of the fixed I2C addresses on the sensors only 1 BME and 1 Si7021 sensors are supported, but multiple 1-Wire sensors.

The software provides an configuration web page where the sensors can be enabled and an location can be specified.

The ESP will than read the sensors every 10 seconds (can be changed in `main.cpp` __FETCH_SENSORS_CYCLE_SEC__). The values are published via MQTT (`main.cpp` __MQTT_SERVER__). The used MQTT topic can be configured by the configuration web page (see below).

## Compile time configuration

There are some values which are only configureable on compile time (in `main.cpp`).

- DEFAULT_NODE_NAME (= F42-NODE) - used as WiFi SSID
- MQTT_SERVER (= mqtt.thomo.de) - the MQTT server
- ALTITUDE (= 282.0F) - your local altitude in meters, used to adjust the pressure measurement
- DEFAULT_ROOT_TOPIC (= "tmp") - the standard root topic
- FETCH_SENSORS_CYCLE_SEC (= 10) - fetch the sensor values every 10 seconds
- MAX_SENSORS (= 10) - number of supported sensors

## Runtime configuration

The runtime configuration is done by using a configuration web page served by the node. Just enter *http://\<the node ip\>*

![Configuration page](https://github.com/thomo/sensornode/raw/master/SensorNode_ConfigPage.png "Configuration page")

## MQTT Topic and Payload

The node will acquire the sensor measurements every X seconds - wether or not the sensor is enabled. It will than publish the value to the MQTT server with:

- topic: `<root topic>/<location>` - where each dot '.' in the root topic and in location is replaced by a slash. 
  
  E.g.: with root topic *homebase* and sensor location *ground.kitchen* the resulting topic is `homebase/ground/kitchen`. 
- payload: `<measurand>,location=<sensor location>,node=<node name>,sensor=<sensor type> value=<sensor value>`

  E.g.: for the selected DS18B20 sensor above the payload is `temperature,location=upstairs.workroom,node=f42,sensor=DS18B20 value=25.25`
