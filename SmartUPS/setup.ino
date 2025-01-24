void setup()
{
    Serial.begin(115200);
    setupPins();
    setupWifi();
    setupOTA();
    setupWebServer();
    read_indicators_statuses();
    setupMqtt();
    Serial.println(String(AP_HOSTNAME) + " started.");
}

void setupPins()
{
    pinMode(AC_INPUT_PIN, INPUT);
    pinMode(DC_OUPUT_PIN, INPUT);
    pinMode(BATTERY_IS_CHARGING_PIN, INPUT);
    pinMode(BATTERY_IS_LOW_PIN, INPUT);
}

void setupWebServer()
{
    async_web_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                        { handleRootWebQuery(request); });

    async_web_server.on("/version", HTTP_GET, [](AsyncWebServerRequest *request)
                        { request->send(200, "text/plain", String(VERSION)); });

    async_web_server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
                        { handleRebootQuery(request); });

    async_web_server.on("/binary_sensor", HTTP_GET, [](AsyncWebServerRequest *request)
                        { request->send(200, "application/json", getStateJson()); });

    async_web_server.begin();
}

void setupWifi()
{
    wifiManager.setHostname(AP_HOSTNAME);
    wifiManager.setTimeout(30);
    wifiManager.setConnectRetries(10);
    wifiManager.setConnectTimeout(90);
    if (wifiManager.getWiFiIsSaved())
    {
        wifiManager.setEnableConfigPortal(false);
    }
    wifi_is_connected = wifiManager.autoConnect(AP_SSID, AP_PASSWORD);
    if (!wifi_is_connected)
    {
        delay(1000);
        Serial.println("failed to connect to wifi and timeout occurred");
        ESP.restart();
    }
    if (MDNS.begin(AP_HOSTNAME))
    {
        Serial.println("MDNS responder started");
    }
}

void setupOTA()
{
    ElegantOTA.begin(&async_web_server);
    ElegantOTA.onStart([]()
                       { sendAvailabilityMessageToMqtt(false, true); });
    ElegantOTA.onEnd([](bool success)
                     { sendAvailabilityMessageToMqtt(success, true); });
    ElegantOTA.setAutoReboot(AUTO_REBOOT_AFTER_OTA_UPDATE_ENABLED);
}

void onOTAEnd(bool success)
{
    sendAvailabilityMessageToMqtt(success, true);
}

void setupMqtt()
{
    if (MQTT_IS_ENABLED)
    {
        mqtt_client.setServer(MQTT_SERVER_ADDRESS, MQTT_SERVER_PORT);
        mqtt_connected_on_startup = mqtt_client.connect("NetworkUps", MQTT_USERNAME, MQTT_PASSWORD);
        mqtt_connected = mqtt_connected_on_startup;
        sendAvailabilityMessageToMqtt(true, true);
        sendStateMessageToMqtt();
    }
}