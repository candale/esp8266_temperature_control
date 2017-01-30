# Temperature sensors tests

This is a project for testing the ESP8266 with different temperature sensors in the hope of creating some kind of thermostat.

Currently only DHT22 was used. The accuracy is far from perfect. 
The next step is using the MCP9808 and DS18B20.

The aim is to create some reliable and smooth temperature control using PID as a start and having as output either a relay or something that supports PWM.
The device may report its actions and may be configured through MQTT using the [MQTT RPC lib](https://github.com/candale/esp8266_mqtt_rpc).
