

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <PubSubClient.h>

WiFiClient espClient;

PubSubClient mqtt(espClient);

#include <TimeLib.h>

WiFiUDP Udp;

#include <Wire.h>
#include <SparkFunHTU21D.h>
HTU21D htu;

unsigned int localPort = 8888;  // local port to listen for UDP packets
const int timeZone = 0;

const char* server = "raspberrypi";
const char* ssid     = "Mitchell";
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
  }
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime() {
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

double dewPointFast(double celsius, double humidity)
{
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

  Udp.begin(localPort);
  setSyncProvider(getNtpTime);

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

    
    //Blocks for 110ms.
    rh = htu.readHumidity();
    temp = htu.readTemperature();

    //Make sure the returned humidity value is valid. This needs to be smarter.
    if (32 > temp && temp > 7) {

      //Compensate for temperature.
      comprh = rh + (25 - temp) * -0.15;

      // //Compute dew point. Lots of floating point. Taken from Markus Schatzl's Sensirion lib.
      // float k;
      // k = log(comprh/100) + (17.62 * temp) / (243.12 + temp);
      // dew =  243.12 * k / (17.62 - k);

      //dew = dewPointFast(temp, comprh);


dew = dewPointFast(temp, comprh);



//DI=T−0.55×(1−0.01H)×(T−14.5)

float di = temp - 0.55 * (1 - 0.01 * comprh) * (temp - 14.5);

di = di * 9 / 5.0 + 32;
      //Convert to F
      float dewF = dew * 9 / 5.0 + 32;
      float tempF = temp * 9 / 5.0 + 32;

//float di = 0.55 * tempF + 0.2 * dewF + 17.5;      

      mqtt.publish("temp/tempF", String(tempF).c_str());
      mqtt.publish("temp/dewF", String(dewF).c_str());
      mqtt.publish("temp/rh", String(comprh).c_str());
      mqtt.publish("temp/di", String(di).c_str());

    }

  }

  mqtt.loop();
  ArduinoOTA.handle();
}
