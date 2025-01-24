#include "WiFiManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ElegantOTA.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define FPSerial Serial1

//========================PINS DEFINITIONS=============================================
const int AC_INPUT_PIN = 1;
const int DC_OUPUT_PIN = 2;
const int BATTERY_IS_CHARGING_PIN = 3;
const int BATTERY_IS_LOW_PIN = 4;
//=====================================================================================

//========================Variables====================================================
const char *VERSION = "1.0";                                             // Just a text version
const char *AP_SSID = "NetworkUPS";                                      // The SSID network that will appear when wifi isn't connected on startup
const char *AP_PASSWORD = "FNKCe59anSnA4NRurESf";                        // The password for AP SSID, you leave it blank
const char *AP_HOSTNAME = "NetworkUPS";                                  // Device's host name that will be used and MDNS (networkups.local)
const char *MQTT_SERVER_ADDRESS = "";                                    // The address for a MQTT server
const char *MQTT_STATE_TOPIC = "devices/networkups/state";               // This topic will be used to inform Home Assistant about state change
const char *MQTT_AVAILABILITY_TOPIC = "devices/networkups/availability"; // This topic will be used to keep Home Assistant informed about the device accessibility
const char *MQTT_USERNAME = "";
const char *MQTT_PASSWORD = "";
const int MQTT_SERVER_PORT = 1883;
const int LOOPS_DELAY = 500;
const int MINIMAL_ALLOWED_VALUE_FROM_PIN = 200;
const int COUNT_OF_READ_TIMES_FROM_PIN = 10;
const int PERIOD_TO_SEND_AVAILABILITY_MESSAGE_IN_MS = 20000;
const int PERIOD_TO_SEND_STATE_MESSAGE_IN_MS = 20000;
const bool AUTO_REBOOT_AFTER_OTA_UPDATE_ENABLED = true;
const bool MQTT_IS_ENABLED = false;                                      // Leave itfalse if you don't want to use MQTT
//=====================================================================================

//========================Variables====================================================
bool mqtt_connected_on_startup = false;
bool mqtt_connected = false;
bool battery_is_low = false;
bool battery_is_charging = false;
bool dc_output_is_present = false;
bool ac_input_is_present = false;
bool wifi_is_connected = false;

unsigned long lastAvailabilityMessageSentTime = 0;

WiFiManager wifiManager;
WiFiClient wifiClient;
AsyncWebServer async_web_server(80);
PubSubClient mqtt_client(wifiClient);

const char *htmlTemplate = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Network UPS - Home</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #222;
            color: #0f0;
            text-align: center;
            margin: 0;
            padding: 0;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            height: 100vh;
        }

        h1 {
            font-size: 2rem;
            color: #fff;
            margin-bottom: 20px;
            text-transform: uppercase;
        }

        .indicator {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: #111;
            border: 2px solid #666;
            padding: 10px 20px;
            margin: 10px;
            width: 300px;
            border-radius: 5px;
        }

        .indicator span {
            font-size: 1rem;
        }

        .indicator .status {
            width: 20px;
            height: 20px;
            background: #222;
            border-radius: 50%;
        }

        .indicator.active .status {
            background: #0f0;
        }

        .indicator.warning .status {
            background: #fc7f03;
        }

        .indicator.red .status {
            background: #fc7f03;
        }

        a {
            color: #0f0;
            text-decoration: none;
            border: 2px solid #0f0;
            padding: 10px 20px;
            margin-top: 20px;
            display: inline-block;
            border-radius: 5px;
            background: #111;
            transition: background 0.3s, color 0.3s;
        }

        a:hover {
            background: #0f0;
            color: #111;
        }
    </style>
</head>
<body>
    <h1>Network UPS - Home</h1><br>
    <h1>v%%VERSION%%</h1>
    <div class="indicator %%LOW_BATTERY%%" id="low-battery">
        <span>Battery is low</span>
        <div class="status"></div>
    </div>
    <div class="indicator %%CHARGE%%" id="charge">
        <span>Charging</span>
        <div class="status"></div>
    </div>
    <div class="indicator %%DC_OUTPUT%%" id="dc-output">
        <span>DC Output</span>
        <div class="status"></div>
    </div>
    <div class="indicator %%AC_INPUT%%" id="ac-input">
        <span>AC Input</span>
        <div class="status"></div>
    </div>
    <div class="indicator %%MQTT_CONNECTED%%" id="ac-input">
        <span>MQTT</span>
        <div class="status"></div>
    </div>
    <a href="/update">OTA Update</a>
</body>
</html>
)rawliteral";
//=====================================================================================

void loop()
{
    ElegantOTA.loop();
    checkWifiConnectionAndReconnectIfLost();
    bool ac_was_present = ac_input_is_present;
    read_indicators_statuses();

    if (ac_was_present && !ac_input_is_present)
    {
        Serial.println("Electricity is turned off.");
        sendStateMessageToMqtt();
    }

    if (!ac_was_present && ac_input_is_present)
    {
        Serial.println("Electricity is turned on.");
        sendStateMessageToMqtt();
    }

    sendAvailabilityMessageToMqtt(true, false);
    delay(LOOPS_DELAY);
}

void checkWifiConnectionAndReconnectIfLost()
{
    wifi_is_connected = WiFi.status() == WL_CONNECTED;

    if (!wifi_is_connected)
    {
        Serial.println("WiFi connection lost, trying to reconnect...");
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.hostname(AP_HOSTNAME);
        WiFi.begin(AP_SSID, AP_PASSWORD);
        unsigned long startAttemptTime = millis();
        const unsigned long connectionTimeout = 10000;

        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < connectionTimeout)
        {
            delay(500);
            Serial.print(".");
        }

        wifi_is_connected = WiFi.status() == WL_CONNECTED;
    }
}

void read_indicators_statuses()
{
    int acInputAboveThreshold = 0;
    int dcOutputAboveThreshold = 0;
    int batteryIsCharginAboveThreshold = 0;
    int batteryIsLowAboveThreshold = 0;

    for (int i = 0; i < COUNT_OF_READ_TIMES_FROM_PIN; i++)
    {
        if (analogRead(AC_INPUT_PIN) < MINIMAL_ALLOWED_VALUE_FROM_PIN)
            acInputAboveThreshold++;
        if (analogRead(DC_OUPUT_PIN) < MINIMAL_ALLOWED_VALUE_FROM_PIN)
            dcOutputAboveThreshold++;
        if (analogRead(BATTERY_IS_CHARGING_PIN) < MINIMAL_ALLOWED_VALUE_FROM_PIN)
            batteryIsCharginAboveThreshold++;
        if (analogRead(BATTERY_IS_LOW_PIN) < MINIMAL_ALLOWED_VALUE_FROM_PIN)
            batteryIsLowAboveThreshold++;
    }

    ac_input_is_present = !(acInputAboveThreshold == COUNT_OF_READ_TIMES_FROM_PIN);
    dc_output_is_present = !(dcOutputAboveThreshold == COUNT_OF_READ_TIMES_FROM_PIN);
    battery_is_charging = !(batteryIsCharginAboveThreshold == COUNT_OF_READ_TIMES_FROM_PIN);
    battery_is_low = !(batteryIsLowAboveThreshold == COUNT_OF_READ_TIMES_FROM_PIN);
}

void sendStateMessageToMqtt()
{
    if (!MQTT_IS_ENABLED or !mqtt_connected_on_startup)
    {
        return;
    }

    if (!mqtt_client.connected())
    {
        reconnectToMqtt();
    }

    mqtt_client.publish(MQTT_STATE_TOPIC, getStateJson().c_str());
}

void sendAvailabilityMessageToMqtt(bool available, bool force)
{
    if (!MQTT_IS_ENABLED or !mqtt_connected_on_startup)
    {
        return;
    }

    if (!mqtt_client.connected())
    {
        reconnectToMqtt();
    }

    if (force)
    {
        mqtt_client.publish(MQTT_AVAILABILITY_TOPIC, available ? "online" : "offline");
    }
    else
    {
        unsigned long currentTime = millis();
        if (currentTime - lastAvailabilityMessageSentTime >= PERIOD_TO_SEND_AVAILABILITY_MESSAGE_IN_MS)
        {
            mqtt_client.publish(MQTT_AVAILABILITY_TOPIC, available ? "online" : "offline");
            lastAvailabilityMessageSentTime = currentTime;
        }
    }
}

void reconnectToMqtt()
{
    int retryCount = 0;
    const int maxRetries = 5;

    while (!mqtt_client.connected() && retryCount < maxRetries)
    {
        mqtt_connected = mqtt_client.connect("NetworkUps", MQTT_USERNAME, MQTT_PASSWORD);
        if (!mqtt_connected)
        {
            retryCount++;
            delay(1000);
        }
    }
}

String getStateJson()
{
    StaticJsonDocument<256> jsonDoc;
    jsonDoc["ac_input"] = ac_input_is_present ? "on" : "off";
    jsonDoc["battery_is_low"] = battery_is_low ? "on" : "off";
    jsonDoc["dc_output"] = dc_output_is_present ? "on" : "off";
    jsonDoc["battery_is_charging"] = battery_is_charging ? "on" : "off";
    jsonDoc["mqtt_connected"] = mqtt_connected_on_startup ? "on" : "off";
    String jsonString;
    serializeJson(jsonDoc, jsonString);
    return jsonString;
}