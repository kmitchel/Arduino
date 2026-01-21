/*
 * hem_htu.ino - HTU21D Temperature & Humidity Sensor
 * 
 * Hardware: ESP8266 with HTU21D I2C sensor
 * Purpose: Reads temperature, humidity, calculates dew point and discomfort index
 * 
 * MQTT Topics:
 *   Publish: temp/tempF, temp/dewF, temp/rh, temp/di
 * 
 * Dependencies:
 *   - ESP8266WiFi
 *   - PubSubClient (MQTT)
 *   - Wire (I2C)
 *   - SparkFunHTU21D
 *   - ArduinoOTA
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SparkFunHTU21D.h>

WiFiClient espClient;
PubSubClient mqtt(espClient);
HTU21D htu;

const char* server = "raspberrypi";
const char* ssid = "Mitchell";
const char* password = "easypassword";

unsigned long lastTemp;

void callback(char* topic, byte* payload, unsigned int length) {
  String payloads;
  for (int i = 0; i < length; i++) {
    payloads += (char)payload[i];
  }
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    digitalWrite(2, !digitalRead(2));
  }
  digitalWrite(2, 1);
}

void mqttConnect() {
  mqtt.setServer(server, 1883);
  mqtt.setCallback(callback);
  if (mqtt.connect(WiFi.hostname().c_str())) {
    // Connected - no subscriptions needed for this sensor
  }
}

double dewPointFast(double celsius, double humidity) {
  double a = 17.271;
  double b = 237.7;
  double temp = (a * celsius) / (b + celsius) + log(humidity * 0.01);
  double Td = (b * temp) / (a - temp);
  return Td;
}

void setup() {
  Serial.begin(9600);
  pinMode(2, OUTPUT);
  digitalWrite(2, 0);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    digitalWrite(2, !digitalRead(2));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  wifiConnect();
  mqttConnect();
  ArduinoOTA.setHostname("htu");
  ArduinoOTA.begin();

  Wire.begin();
  htu.begin();
}

float rh, temp, comprh, dew;

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }

  if (!mqtt.connected()) {
    mqttConnect();
  }

  if (millis() - lastTemp > 15000) {
    lastTemp = millis();

    // Blocks for 110ms
    rh = htu.readHumidity();
    temp = htu.readTemperature();

    // Validate temperature range
    if (32 > temp && temp > 7) {
      // Compensate for temperature
      comprh = rh + (25 - temp) * -0.15;

      // Calculate dew point
      dew = dewPointFast(temp, comprh);

      // Calculate discomfort index: DI = T - 0.55 × (1 - 0.01H) × (T - 14.5)
      float di = temp - 0.55 * (1 - 0.01 * comprh) * (temp - 14.5);
      di = di * 9 / 5.0 + 32;

      // Convert to Fahrenheit
      float dewF = dew * 9 / 5.0 + 32;
      float tempF = temp * 9 / 5.0 + 32;

      mqtt.publish("temp/tempF", String(tempF).c_str());
      mqtt.publish("temp/dewF", String(dewF).c_str());
      mqtt.publish("temp/rh", String(comprh).c_str());
      mqtt.publish("temp/di", String(di).c_str());
    }
  }

  mqtt.loop();
  ArduinoOTA.handle();
}
