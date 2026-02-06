#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SparkFunHTU21D.h>

WiFiClient espClient;
PubSubClient mqtt(espClient);
HTU21D htu;

const char* server = "192.168.1.2";
const char* ssid = "Mitchell";
const char* password = "easypassword";

unsigned long lastTemp;
float rh, temp, comprh, dew;

void callback(char* topic, byte* payload, unsigned int length) {
  String payloads;
  for (int i = 0; i < length; i++) {
    payloads += (char)payload[i];
  }
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("htu");
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
  }
}

double dewPointFast(double celsius, double humidity) {
  double a = 17.271;
  double b = 237.7;
  double temp = (a * celsius) / (b + celsius) + log(humidity * 0.01);
  double Td = (b * temp) / (a - temp);
  return Td;
}

// ==========================================
// HELPER: Calculate Indoor "Feels Like" Temp
// ==========================================
// Based on Australian Apparent Temperature (Steadman) for Indoor/No Wind
// Returns Celsius
float calculateFeelsLike(float tempC, float humidityRH) {
  // 1. Calculate Vapor Pressure (hPa)
  // e = (rh/100) * 6.105 * exp((17.27 * T) / (237.7 + T))
  float vaporPressure = (humidityRH / 100.0) * 6.105 * exp((17.27 * tempC) / (237.7 + tempC));

  // 2. Calculate Apparent Temp (C)
  // AT = Ta + 0.33 * e - 4.0 (The -4.0 is a baseline adjustment for lack of radiation/wind)
  float apparentTempC = tempC + (0.33 * vaporPressure) - 4.0;

  return apparentTempC;
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

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }

  if (!mqtt.connected()) {
    mqttConnect();
  }

  if (millis() - lastTemp > 15000) {
    lastTemp = millis();

    // Blocks for 110ms.
    rh = htu.readHumidity();
    temp = htu.readTemperature();

    // Make sure the returned humidity value is valid.
    if (32 > temp && temp > 7) {
      // Compensate for temperature.
      comprh = rh + (25 - temp) * -0.15;

      dew = dewPointFast(temp, comprh);

      // Calculate Feels Like Temp (returns C)
      float diC = calculateFeelsLike(temp, comprh);

      // Convert to F
      float dewF = dew * 9 / 5.0 + 32;
      float tempF = temp * 9 / 5.0 + 32;
      float di = diC * 9 / 5.0 + 32;

      mqtt.publish("temp/tempF", String(tempF).c_str());
      mqtt.publish("temp/dewF", String(dewF).c_str());
      mqtt.publish("temp/rh", String(comprh).c_str());
      mqtt.publish("temp/di", String(di).c_str());
    }
  }

  mqtt.loop();
  ArduinoOTA.handle();
}
