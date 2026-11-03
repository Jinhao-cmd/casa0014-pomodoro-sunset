// network.ino - WiFi, MQTT, Vespera frames and the JSON status topic
//
// The timer never depends on the network: if WiFi or the broker drops out, the countdown
// carries on locally and the current state is sent again once the connection is back.

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

char vesperaTopic[64];
char statusTopic[64];
unsigned long lastNetworkAttemptMs = 0;
unsigned long lastStatusMs = 0;
const unsigned long NETWORK_RETRY_MS = 10000;
const unsigned long STATUS_EVERY_MS  = 10000;
const char* OFFLINE_MESSAGE = "{\"state\":\"offline\",\"state_id\":-1}";


bool connectMqtt() {
  char clientId[40];
  snprintf(clientId, sizeof(clientId), "pomodoro-%s-%04lx", LIGHT_ID, (unsigned long)random(0xffff));
  Serial.print("Connecting to MQTT as ");
  Serial.print(clientId);
  Serial.print("... ");

  // Last Will: if the board disappears, the broker publishes "offline" on our behalf
  if (mqtt.connect(clientId, SECRET_MQTTUSER, SECRET_MQTTPASS,
                   statusTopic, 0, true, OFFLINE_MESSAGE)) {
    Serial.println("connected");
    forceLightUpdate();
    publishStatus();
    return true;
  }
  Serial.print("failed, state = ");    // -2 = cannot reach broker, 4 = wrong username/password
  Serial.println(mqtt.state());
  return false;
}

void connectNetwork(unsigned long timeoutMs) {
  snprintf(vesperaTopic, sizeof(vesperaTopic), "student/CASA0014/luminaire/%s", LIGHT_ID);

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(512);     // a 216-byte frame plus the topic is close to the 256-byte default
  mqtt.setSocketTimeout(5);    // do not freeze for long if the broker cannot be reached

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    return;
  }

  lcdShow("Connecting WiFi", SECRET_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    delay(3000);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Vespera topic: ");
    Serial.println(vesperaTopic);
    Serial.print("Status topic:  ");
    Serial.println(statusTopic);
    connectMqtt();
  } else {
    Serial.println("WiFi not connected - running offline and retrying every 10 s");
  }
  lastNetworkAttemptMs = millis();
}

// Keeps MQTT alive and reconnects (at most every 10 s) without stopping the timer
void maintainNetwork(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (now - lastNetworkAttemptMs < NETWORK_RETRY_MS) {
    return;
  }
  lastNetworkAttemptMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost - reconnecting (the timer keeps running)");
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    if (WiFi.status() != WL_CONNECTED) {
      return;
    }
  }
  connectMqtt();
}

// Sends one 216-byte frame (R,G,B for each of the 72 pixels) to Vespera
bool publishFrame(const byte* data, unsigned int length) {
  if (!mqtt.connected()) {
    return false;
  }
  return mqtt.publish(vesperaTopic, data, length);
}

// Readable status for MQTT Explorer, and numbers Telegraf can store in InfluxDB
void publishStatus() {
  if (!mqtt.connected()) {
    return;
  }
  char json[300];
  snprintf(json, sizeof(json),
           "{\"state\":\"%s\",\"state_id\":%d,\"remaining_s\":%lu,\"focus_min\":%d,"
           "\"stars\":%d,\"completed\":%d,\"phone_lifts\":%d,\"phone_lifts_total\":%d,"
           "\"phone_docked\":%d,\"present\":%d,\"distance_cm\":%d,\"light\":%d,\"brightness\":%d}",
           STATE_NAMES[state], (int)state, (phaseRemainingMs + 999) / 1000, focusMinutes,
           cycleStars, completedTotal, interruptions, interruptionsTotal,
           phoneDocked ? 1 : 0, userPresent ? 1 : 0, distanceCm, lightLevel,
           (int)(outputBrightness() * 100));
  mqtt.publish(statusTopic, json, true);   // retained: anyone subscribing sees the latest status
  lastStatusMs = millis();
}

void publishStatusIfDue(unsigned long now) {
  if (now - lastStatusMs >= STATUS_EVERY_MS) {
    publishStatus();
  }
}
