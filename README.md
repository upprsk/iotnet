# IoT Network

A network for IoT devices built on top of ESP-NOW supporting a _possibly infinite_
number of devices, without causing slowdown on traditional Wi-Fi networks.

## ESP-NOW

Protocol created by [Espressif](https://www.espressif.com/) for use with its
microcontrollers. Documentation about API and format can be found in
[the official docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html).

Because of its connectionless nature, the overhead on power and processing time
is greatly reduced. This also increases range, with distances reported going as
long as 200 meters without packet drops with open space.

The main problem is the reduced payload size, of just 250 bytes and 20 peers.
However, for most IoT devices and applications, that is more than enough.

## Programs

This repository contains 2 [platformio](https://platformio.org/) projects, one
for a slave ESP32 device with a [BME-280 sensor module](https://makeradvisor.com/tools/bme280-sensor-module/)
(temperature, humidity and pressure) and another for a master ESP32. The master
connects to the Wi-Fi and re-publishes data received from the slave for MQTT.

```mermaid
flowchart LR

slave(sensor) -- ESP-NOW --> master(master) -- Wi-Fi --> MQTT
```
