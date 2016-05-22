#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <PubSubClient.h>

#include <Wire.h>

WiFiClient espClient;

PubSubClient mqtt(espClient);

#include <TimeLib.h>
#include <TimeAlarms.h>

WiFiUDP Udp;

unsigned int localPort = 8888;  // local port to listen for UDP packets
const int timeZone = 0;

const char* server = "raspberrypi";
const char* ssid     = "Mitchell";
const char* password = "easypassword";

const int addr = 0x39;

const uint8_t cool = 5;
const uint8_t fan = 6;
const uint8_t heat = 7;
const uint8_t coolOver = 2;
const uint8_t fanOver = 3;
const uint8_t heatOver = 4;

const uint8_t READY = 1, COOLON = 2, HEATON = 3, COOLING = 4, HEATING = 5, FANWAIT = 6, WAIT = 7, OFF = 8;
uint8_t state = READY;
uint8_t heatOn = 67, heatOff = 69, coolOff = 51, coolOn = 54;

unsigned long stateDelay, machineDelay;

float tempF = 70;
float dewF = 45;
float out = 70;

void callback(char* topic, byte* payload, unsigned int length) {
  String payloads;
  for (int i = 0; i < length; i++) {
    payloads += (char)payload[i];
  }

  if (strcmp(topic, "hvac/coolOn") == 0) {
    if (payloads == "?") {
      mqtt.publish("hvac/coolOn", String(coolOn).c_str());
    } else {
      int thisNumber = payloads.toInt();
      constrain(thisNumber, 40, 60);
      if (thisNumber > coolOff) {
        coolOn = thisNumber;
      }
    }
  }
  if (strcmp(topic, "hvac/coolOff") == 0) {
    if (payloads == "?") {
      mqtt.publish("hvac/coolOff", String(coolOff).c_str());
    } else {
      int thisNumber = payloads.toInt();
      constrain(thisNumber, 40, 60);
      if (thisNumber < coolOn) {
        coolOff = thisNumber;
      }
    }
  }
  if (strcmp(topic, "hvac/heatOn") == 0) {
    if (payloads == "?") {
      mqtt.publish("hvac/heatOn", String(heatOn).c_str());
    } else {
      int thisNumber = payloads.toInt();
      constrain(thisNumber, 60, 85);
      if (thisNumber < heatOff) {
        heatOn = thisNumber;
      }
    }
  }
  if (strcmp(topic, "hvac/heatOff") == 0) {
    if (payloads == "?") {
      mqtt.publish("hvac/heatOff", String(heatOff).c_str());
    } else {
      int thisNumber = payloads.toInt();
      constrain(thisNumber, 60, 85);
      if (thisNumber > heatOn) {
        heatOff = thisNumber;
      }
    }
  }
  if (strcmp(topic, "temp/tempF") == 0) {
    int thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
      tempF = .99 * tempF + .01 * thisNumber;
    }
  }
  if (strcmp(topic, "temp/dewF") == 0) {
    int thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
      dewF = .99 * dewF + .01 * thisNumber;
    }
  }
  if (strcmp(topic, "temp/289c653f03000027") == 0) {
    int thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
      out = thisNumber;
    }
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
    mqtt.subscribe("hvac/+");
    mqtt.subscribe("temp/tempF");
    mqtt.subscribe("temp/dewF");
    mqtt.subscribe("temp/289c653f03000027");
  }
}

void gpioWrite (uint8_t pin, uint8_t value) {
  uint8_t data;
  Wire.beginTransmission(addr);
  Wire.requestFrom(addr, 1);
  data = Wire.read();
  Wire.endTransmission();
  if (value == LOW)
  {
    data &= ~(1 << pin);
  }
  else
  {
    data |= (1 << pin);
  }
  Wire.beginTransmission(addr);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t gpioRead (uint8_t pin) {
  uint8_t data;
  Wire.beginTransmission(addr);
  Wire.requestFrom(addr, 1);
  data = Wire.read();
  Wire.endTransmission();
  return ((data >> pin) & 1);
}

void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2, 0);

  Wire.begin();

  Wire.beginTransmission(addr);
  Wire.write(255);
  Wire.endTransmission();


  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
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
  ArduinoOTA.setHostname("hvac");
  ArduinoOTA.begin();

  setSyncProvider(getNtpTime);
  Udp.begin(localPort);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }

  if (!mqtt.connected()) {
    mqttConnect();
  }

  if (millis() > machineDelay) {
    machineDelay = millis() + 15000;
    switch (state) {
      case READY:
        if (out >= 60) {
          mqtt.publish("hvac/state", "CoolReady");
          if (dewF >= coolOn) {
            state = COOLON;
          }
        } else {
          mqtt.publish("hvac/state", "HeatReady");
          if (tempF < heatOn) {
            state = HEATON;
          }
        }
        break;
      case COOLON:
        gpioWrite(cool, LOW);
        gpioWrite(fan, LOW);
        mqtt.publish("hvac/state", "CoolOn");
        stateDelay = millis() + 600000;
        state = COOLING;
        gpioWrite(coolOver, LOW);
        gpioWrite(fanOver, LOW);
        gpioWrite(heatOver, LOW);
        break;
      case HEATON:
        gpioWrite(heat, LOW);
        mqtt.publish("hvac/state", "HeatOn");
        stateDelay = millis() + 600000;
        state = HEATING;
        gpioWrite(coolOver, LOW);
        gpioWrite(fanOver, LOW);
        gpioWrite(heatOver, LOW);
        break;
      case COOLING:
        mqtt.publish("hvac/state", "Cooling");
        if (dewF < coolOff && millis() > stateDelay) {
          stateDelay = millis() + 180000;
          state = FANWAIT;
          gpioWrite(cool, HIGH);
        }
        break;
      case HEATING:
        mqtt.publish("hvac/state", "Heating");
        if (tempF >= heatOff && millis() > stateDelay) {
          stateDelay = millis() + 300000;
          state = WAIT;
          gpioWrite(heat, HIGH);
        }
        break;
      case FANWAIT:
        mqtt.publish("hvac/state", "FanWait");
        if (millis() > stateDelay) {
          stateDelay = millis() + 180000;
          state = WAIT;
          gpioWrite(fan, HIGH);
        }
        break;
      case WAIT:
        mqtt.publish("hvac/state", "Wait");
        if (millis() > stateDelay) {
          state = READY;
          gpioWrite(coolOver, HIGH);
          gpioWrite(fanOver, HIGH);
          gpioWrite(heatOver, HIGH);
        }
        break;
      case OFF:
        mqtt.publish("hvac/state", "Off");
        break;
    }
  }
  mqtt.loop();
  ArduinoOTA.handle();

}


/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  WiFi.hostByName(server, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
