#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstdint>

#include "esp32-hal-gpio.h"
#include "esp_err.h"

static constexpr auto SERIAL_BAUD = 115200;
static constexpr auto LED_PIN = 33;

static constexpr auto build_timestamp = __DATE__ "/" __TIME__;

enum PrefWifiMode {
    PREF_WIFI_STATION,
    PREF_WIFI_SOFTAP,
};

AsyncWebServer server{80};

WiFiClient   espClient;
PubSubClient mqtt_client{espClient};

String g_mqtt_server;
String g_mqtt_username;
String g_mqtt_password;

Preferences wifi_prefs;
Preferences mqtt_prefs;

struct Packet {
    uint8_t  version;
    uint8_t  slave_id;
    uint8_t  available2;
    uint8_t  available3;
    uint32_t temperature;
    uint32_t humidity;
    uint32_t pressure;
};

void setup_wifi() {
    // auto wifi_mode = wifi_prefs.getUInt("mode", PREF_WIFI_SOFTAP);
    // auto ssid = wifi_prefs.getString("ssid", "iotnet-master");
    // auto pass = wifi_prefs.getString("pass", "iotnet-master");

    auto wifi_mode = wifi_prefs.getUInt("mode", PREF_WIFI_STATION);
    auto ssid = wifi_prefs.getString("ssid", "WLL-Inatel");
    auto pass = wifi_prefs.getString("pass", "inatelsemfio");

    if (wifi_mode == PREF_WIFI_SOFTAP) {
        Serial.println("starting softap");
        Serial.print("ssid=");
        Serial.println(ssid);
        Serial.print("pass=");
        Serial.println(pass);

        WiFi.mode(WIFI_MODE_AP);
        if (!WiFi.softAP(ssid, pass)) {
            Serial.println("failed to start softap");
            while (true) {
            }
        }

        // esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_LR);
    } else if (wifi_mode == PREF_WIFI_STATION) {
        Serial.println("starting station");

        WiFi.mode(WIFI_MODE_APSTA);
        WiFi.begin(ssid, pass);

        // disconnect softap part
        WiFi.softAPdisconnect();
        // esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

        Serial.print("Connecting to WiFi ..");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print('.');
            delay(1000);
        }
        Serial.println(WiFi.localIP());
        Serial.print("channel:");
        Serial.println(WiFi.channel());
    } else {
        wifi_prefs.putUInt("mode", PREF_WIFI_SOFTAP);

        Serial.println("invalid preferences: wifi mode");
        return ESP.restart();
    }
}

void setup_espnow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("error initializing ESP-NOW");
        return ESP.restart();
    }

    esp_now_register_recv_cb(
        [](uint8_t const *, uint8_t const *incomingData, int len) {
            if (len != sizeof(Packet)) {
                Serial.println("wrong size");
                return;
            }

            Packet p;
            memcpy(&p, incomingData, len);

            String payload = "{\"version\":";
            payload += static_cast<int32_t>(p.version);

            payload += ",\"slave_id\":";
            if (p.version == 2) {
                payload += static_cast<int32_t>(p.slave_id);
            } else {
                payload += 0;
            }

            payload += ",\"temperature\":";
            payload += static_cast<float>(p.temperature) / 100;
            payload += ",\"humidity\":";
            payload += static_cast<float>(p.humidity) / 100;
            payload += ",\"pressure\":";
            payload += static_cast<float>(p.pressure) / 100;
            payload += "}";

            Serial.println(payload);

            if (mqtt_client.connected()) {
                mqtt_client.publish("esp32/sensor", payload.c_str());
            }
        });
}

void reconnect_mqtt() {
    if (mqtt_client.connected()) return;

    Serial.println("Reconnecting MQTT");

    g_mqtt_username = mqtt_prefs.getString("username", "emqx");
    g_mqtt_password = mqtt_prefs.getString("password", "public");

    if (!mqtt_client.connect("iotnet-master", g_mqtt_username.c_str(),
                             g_mqtt_password.c_str())) {
        Serial.println("failed to connect MQTT");
        return;
    }

    Serial.println("MQTT connected");
}

void setup_mqtt() {
    auto wifi_mode = wifi_prefs.getUInt("mode", PREF_WIFI_SOFTAP);
    if (wifi_mode == PREF_WIFI_SOFTAP) return;

    g_mqtt_server = mqtt_prefs.getString("server", "broker.emqx.io");
    auto mqtt_port = mqtt_prefs.getInt("port", 1883);

    Serial.printf("connecting to MQTT server %s:%d", g_mqtt_server.c_str(),
                  mqtt_port);
    Serial.println();

    mqtt_client.setServer(g_mqtt_server.c_str(), mqtt_port);
}

void setup_web_server() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        static constexpr auto content = R"(<!DOCTYPE html>
<html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Master</title>
    </head>
    <body>
        <h2>Build Stats</h2>
        <ul>
            <li>timestamp: %BTIME%</li>
        </ul>

        <h2>WiFi Stats</h2>
        <ul>
            <li>WiFi channel: %WIFICH%</li>
        </ul>

        <h2>WiFi Options</h2>
        <form action="/save-wifi" method="GET">
            <div>
                <select name="wifimode" required>
                    <label>WiFi mode:</label>
                    <option value="" disabled>select one</option>
                    <option value="station" %WIFIMODE-STA%>Station</option>
                    <option value="softap" %WIFIMODE-SAP%>Soft AP</option>
                </select>
            </div>

            <div>
                <label for="input-ssid">SSID:</label>
                <input id="input-ssid" name="ssid" type="text" value="%SSID%">
            </div>

            <div>
                <label for="input-pass">Password:</label>
                <input id="input-pass" name="pass" type="password">
            </div>

            <div>
                <label for="input-ip">IP address:</label>
                <input id="input-ip" name="ip" type="text" value="%IP%" readonly>
            </div>

            <div>
                <label for="input-gw">Default gateway:</label>
                <input id="input-gw" name="gw" type="text" value="%GW%" readonly>
            </div>

            <div>
                <label for="input-mask">Network mask:</label>
                <input id="input-mask" name="mask" type="text" value="%MASK%" readonly>
            </div>

            <div>
                <label for="input-mac">MAC:</label>
                <input id="input-mac" name="mac" type="text" value="%MAC%" readonly>
            </div>

            <button type="submit">
                save
            </button>
        </form>

        <h2>MQTT Options</h2>
        <form action="/save-mqtt" method="GET">
            <div>
                <label for="input-mqtt-server">MQTT server:</label>
                <input id="input-mqtt-server" name="mqtt-server" type="text" value="%MQTT-SERVER%">
            </div>

            <div>
                <label for="input-mqtt-port">MQTT port:</label>
                <input id="input-mqtt-port" name="mqtt-port" type="number" value="%MQTT-PORT%">
            </div>

            <button type="submit">
                save
            </button>
        </form>

        <form action="/reset" method="POST">
            <button type="submit">
                reset
            </button>
        </form>
    </body>
</html>)";

        req->send_P(
            200, "text/html", content, [](String const &label) -> String {
                if (label == "IP") return WiFi.localIP().toString();
                if (label == "GW") return WiFi.gatewayIP().toString();
                if (label == "MASK") return WiFi.subnetMask().toString();
                if (label == "MAC") return WiFi.macAddress();

                if (label == "SSID")
                    return wifi_prefs.getString("ssid", "sensor-master");
                if (label == "PASS")
                    return wifi_prefs.getString("pass", "sensor-master");

                if (label == "BTIME") return build_timestamp;

                if (label == "WIFICH") return String{WiFi.channel()};

                if (label == "MQTT-SERVER")
                    return mqtt_prefs.getString("server", "broker.emqx.io");
                if (label == "MQTT-PORT")
                    return String{mqtt_prefs.getInt("port", 1883)};

                if (label == "WIFIMODE-STA") {
                    auto mode = wifi_prefs.getUInt("mode", PREF_WIFI_SOFTAP);
                    if (mode == PREF_WIFI_STATION) return "selected";
                    return "";
                }

                if (label == "WIFIMODE-SAP") {
                    auto mode = wifi_prefs.getUInt("mode", PREF_WIFI_SOFTAP);
                    if (mode == PREF_WIFI_SOFTAP) return "selected";
                    return "";
                }

                return "ERROR";
            });
    });

    server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
        Serial.println("reset ESP32");
        ESP.restart();

        req->redirect("/");
    });

    server.on("/save-mqtt", HTTP_GET, [](AsyncWebServerRequest *req) {
        auto mqtt_server_s = req->getParam("mqtt-server");
        if (mqtt_server_s == nullptr)
            return req->send(400, "text/plain", "invalid mqtt-server");
        mqtt_prefs.putString("server", mqtt_server_s->value());

        auto mqtt_port_s = req->getParam("mqtt-port");
        if (mqtt_port_s == nullptr)
            return req->send(400, "text/plain", "invalid mqtt-port");
        auto mqtt_port = mqtt_port_s->value().toInt();
        if (mqtt_port != 0) {
            mqtt_prefs.putInt("port", static_cast<int>(mqtt_port));
        }

        req->redirect("/");
    });

    server.on("/save-wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
        auto wifimode_s = req->getParam("wifimode");
        if (wifimode_s == nullptr) {
            Serial.println("missing wifimode");
            return req->send(400, "text/plain", "missing wifimode");
        }
        if (wifimode_s->value() == "station") {
            wifi_prefs.putUInt("moe", PREF_WIFI_STATION);
        } else if (wifimode_s->value() == "softap") {
            wifi_prefs.putUInt("mode", PREF_WIFI_SOFTAP);
        } else {
            Serial.print("invalid wifimode: ");
            Serial.println(wifimode_s->value());
            return req->send(400, "text/plain", "invalid wifimode");
        }

        auto ssid_s = req->getParam("ssid");
        if (ssid_s == nullptr)
            return req->send(400, "text/plain", "invalid ssid");
        wifi_prefs.putString("ssid", ssid_s->value());

        auto pass_s = req->getParam("pass");
        if (pass_s == nullptr)
            return req->send(400, "text/plain", "invalid pass");
        wifi_prefs.putString("pass", pass_s->value());

        req->redirect("/");
    });

    ElegantOTA.begin(&server);
    server.begin();
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    Serial.println(F("BME280 Sensor event test"));

    if (!wifi_prefs.begin("wifi-prefs")) {
        Serial.println("error initializing wifi preferences");
        return ESP.restart();
    }

    wifi_prefs.clear();

    if (!mqtt_prefs.begin("mqtt-prefs")) {
        Serial.println("error initializing mqtt preferences");
        return ESP.restart();
    }

    // pinMode(12, INPUT_PULLUP);
    // if (digitalRead(12) == 0) {
    //     Serial.println("clearing preferences, release pin");
    //     while (digitalRead(12) == 0) {
    //     }
    //
    //     mqtt_prefs.clear();
    //     wifi_prefs.clear();
    //
    //     ESP.restart();
    //
    //     while (true) {
    //     }
    // }

    pinMode(LED_PIN, OUTPUT);

    setup_wifi();
    setup_espnow();
    setup_mqtt();
    setup_web_server();
}

auto sts = false;

void loop() {
    Serial.println("loop()");

    auto wifi_mode = wifi_prefs.getUInt("mode", PREF_WIFI_SOFTAP);
    if (!mqtt_client.connected() && wifi_mode != PREF_WIFI_SOFTAP) {
        reconnect_mqtt();
    }

    mqtt_client.loop();

    digitalWrite(LED_PIN, sts);
    sts = !sts;

    delay(1000);
}
