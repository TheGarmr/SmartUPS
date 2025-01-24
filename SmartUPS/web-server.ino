void handleRootWebQuery(AsyncWebServerRequest *request)
{
  int httpCode = ac_input_is_present ? 200 : 505;
  String html = htmlTemplate;
  html.replace("%%VERSION%%", String(VERSION));
  html.replace("%%LOW_BATTERY%%", battery_is_low ? "warning" : "");
  html.replace("%%CHARGE%%", battery_is_charging ? "warning" : "");
  html.replace("%%DC_OUTPUT%%", dc_output_is_present ? "active" : "red");
  html.replace("%%AC_INPUT%%", ac_input_is_present ? "active" : "red");
  if (MQTT_IS_ENABLED)
  {
    html.replace("%%MQTT_CONNECTED%%", mqtt_client.connected() ? "active" : "");
  }
  request->send(httpCode, "text/html", html);
}

void handleRebootQuery(AsyncWebServerRequest *request)
{
  request->send(200, "text/plain", "rebooting");
  sendAvailabilityMessageToMqtt(false, true);
  AsyncWebServerRequest *req = request;
  req->onDisconnect([]()
                    { delay(500);
                        ESP.restart(); });
}
