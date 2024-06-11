#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstdint>

#include "esp32-hal-gpio.h"
#include "esp_err.h"

Adafruit_BME280  bme;  // use I2C interface
Adafruit_Sensor *bme_temp = bme.getTemperatureSensor();
Adafruit_Sensor *bme_pressure = bme.getPressureSensor();
Adafruit_Sensor *bme_humidity = bme.getHumiditySensor();

AsyncWebServer server{80};

static auto           WIFI_SSID = "iotnet-bme";
static auto           WIFI_PASS = "iotnet-bme";
static constexpr auto SERIAL_BAUD = 115200;
static constexpr auto LED_PIN = 2;
static constexpr auto WIFI_CHANNEL = 6;
static constexpr auto SLAVE_ID = 1;

// NOLINTNEXTLINE(modernize-avoid-c-arrays)
static constexpr uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF,
                                               0xFF, 0xFF, 0xFF};

struct Packet {
    uint8_t  version;
    uint8_t  slave_id;
    uint8_t  _available2;
    uint8_t  _available3;
    uint32_t temperature;
    uint32_t humidity;
    uint32_t pressure;
};

void setup() {
    Serial.begin(SERIAL_BAUD);
    Serial.println(F("BME280 Sensor event test"));

    pinMode(LED_PIN, OUTPUT);

    if (!Wire.begin(22, 23)) {
        Serial.println(F("Could not initialize I2C"));
        while (true) delay(10);
    }

    if (!bme.begin(0x76, &Wire)) {
        Serial.println(
            F("Could not find a valid BME280 sensor, check wiring!"));
        while (true) delay(10);
    }

    bme_temp->printSensorDetails();
    bme_pressure->printSensorDetails();
    bme_humidity->printSensorDetails();

    // WiFi.mode(WIFI_MODE_AP);
    if (!WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL)) {
        Serial.println("Failed to initialize soft AP");
        while (true) delay(10);
    }

    // WiFi.disconnect();
    // esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("error initializing ESP-NOW");
        return ESP.restart();
    }

    esp_now_register_send_cb(
        [](uint8_t const *mac_addr, esp_now_send_status_t status) {
            Serial.print("\r\nLast Packet Send Status:\t");
            Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success"
                                                          : "Delivery Fail");
        });

    // Register peer
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, broadcastAddress, 6);
    peer_info.channel = WIFI_CHANNEL;
    peer_info.encrypt = false;
    peer_info.ifidx = WIFI_IF_AP;

    // Add peer
    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/plain", "yay!");
    });

    ElegantOTA.begin(&server);
    server.begin();
}

auto sts = false;

void loop() {
    sensors_event_t temp_event;
    sensors_event_t pressure_event;
    sensors_event_t humidity_event;

    bme_temp->getEvent(&temp_event);
    bme_pressure->getEvent(&pressure_event);
    bme_humidity->getEvent(&humidity_event);

    Serial.print(F("Temperature = "));
    Serial.print(temp_event.temperature);
    Serial.println(" *C");

    Serial.print(F("Humidity = "));
    Serial.print(humidity_event.relative_humidity);
    Serial.println(" %");

    Serial.print(F("Pressure = "));
    Serial.print(pressure_event.pressure);
    Serial.println(" hPa");

    Serial.println();

    Packet p{
        .version = 2,
        .slave_id = SLAVE_ID,
        .temperature = static_cast<uint32_t>(temp_event.temperature * 100),
        .humidity =
            static_cast<uint32_t>(humidity_event.relative_humidity * 100),
        .pressure = static_cast<uint32_t>(pressure_event.pressure * 100),
    };

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&p, sizeof(p));
    if (result == ESP_OK) {
        Serial.println("Sent with success");
    } else {
        Serial.println("Error sending the data:");
        Serial.println(esp_err_to_name(result));
    }

    digitalWrite(LED_PIN, sts);
    sts = !sts;
    delay(1000);
}
