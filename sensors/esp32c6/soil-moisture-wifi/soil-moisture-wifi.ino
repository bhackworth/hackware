#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <NetworkClient.h>
#include <HTTPClient.h>
#include <math.h>

#include "arduino_secrets.h"

#define FW_VERSION "0.1.3"

// WiFi, HTTP settings
#ifndef WL_MAC_ADDR_LENGTH
#define WL_MAC_ADDR_LENGTH 6
#endif
uint8_t macAddress[WL_MAC_ADDR_LENGTH];

typedef struct {
  char *host;
  uint16_t port;
} server_t;

server_t servers[] = {
  { "192.168.1.11", 8200 },
  { "192.168.1.61", 8234 },
  { "homeassistant", 8234 }
};

#define MILLI (1000)
#define MICRO (1000 * 1000)

int secondsToSleep = 15; // * 60;

// Soil moisture settings
int dhtPin = 0;
int moisturePin = 1;
char sensorName[7];

int highestSeen = 0;
int lowestSeen = 4095;

// These calibrated with 3.3v on ESP32-C6
int superWet = 1650;
int superDry = 2150;
int dryThreshold = 40;  // percent

int ledPin = LED_BUILTIN;
int ledON = LOW;
int ledOFF = HIGH;

int needToCalibrate = 0;

typedef struct {
  int voltage;
  float moisture;
  float temperature;
  float humidity;
} sample_t;

sample_t sample = { 0 };

DHT dht(dhtPin, DHT11);

void setup() {
  Serial.begin(115200);

  dht.begin();
  pinMode(moisturePin, INPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, ledOFF);

  Serial.printf("Connecting to %s\n", SECRET_SSID);
  int status = WiFi.begin(SECRET_SSID, SECRET_PASS);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(100);
  }

  WiFi.macAddress(macAddress);
  snprintf(sensorName, sizeof(sensorName)/sizeof(char),
      "%02X%02X%02X", macAddress[3], macAddress[4], macAddress[5]);
  printWifiStatus();
}

int sampleDiff(sample_t a, sample_t b) {
  return (a.moisture != b.moisture);
}

void send_sample(sample_t sample) {
  WiFiClient net;
  HTTPClient client;

  for (int idx = 0; idx < (sizeof(servers)/sizeof(servers[0])); idx++) {
      server_t server = servers[idx];
      Serial.printf("\nConnecting to %s:%d\n", server.host, server.port);
      if (client.begin(net, server.host, server.port, "/api/samples")) {
        JsonDocument json;
        json["voltage"] = sample.voltage;
        json["moisture"] = sample.moisture;
        json["temperature"] = sample.temperature;
        json["humidity"] = sample.humidity;
        json["sensor"] = sensorName;
        json["fw-version"] = FW_VERSION;

        String data;
        serializeJson(json, data);

        Serial.print("Sending ");
        Serial.println(data);

        client.addHeader("Host", server.host);
        client.addHeader("Content-type", "text/json");
        client.addHeader("Connection", "close");

        int responseCode = client.POST(data);
        Serial.printf("%d: ", responseCode);
        Serial.println(client.getString());
      } else {
        Serial.printf("couldn't connect to %s:%d\n", server.host, server.port);
      }
  }
}

void calibrate() {
  digitalWrite(ledPin, ledON);
  int startTime = millis();
  int endTime = startTime + 20000;
  Serial.println("Insert the probe into water and remove");

  while (millis() < endTime) {
    int analogVolts = analogReadMilliVolts(moisturePin);

    if (analogVolts > 0) {
      highestSeen = max(highestSeen, analogVolts);
      lowestSeen = min(lowestSeen, analogVolts);
    }
    delay(50);
  }
  Serial.printf("Range of values is %d to %d\n", lowestSeen, highestSeen);
  superWet = lowestSeen;
  superDry = highestSeen;
  digitalWrite(ledPin, ledOFF);
}
void sample_and_send() {
  sample_t newSample = { 0 };

  digitalWrite(ledPin, ledON);

  newSample.voltage = analogReadMilliVolts(moisturePin);
  newSample.moisture = 100.0 - ((float)(newSample.voltage - superWet) * 100.0 / (float)(superDry - superWet));
  newSample.moisture = max(newSample.moisture, 0.0F);
  newSample.moisture = min(newSample.moisture, 100.0F);

  newSample.temperature = dht.readTemperature();
  newSample.humidity = dht.readHumidity();

  // print out the values you read:
  Serial.printf("Volts: %d, %.2f%% wet, %s\n",
                newSample.voltage,
                newSample.moisture,
                (newSample.moisture < dryThreshold) ? "DRY" : "WET");

  if (true || sampleDiff(newSample, sample)) {
    send_sample(newSample);
    memcpy(&sample, &newSample, sizeof(newSample));
  }
  digitalWrite(ledPin, ledOFF);
}
/* -------------------------------------------------------------------------- */
void loop() {
/* -------------------------------------------------------------------------- */
  if (needToCalibrate) {
    calibrate();
    needToCalibrate = 0;
  }
  sample_and_send();

  delay(secondsToSleep * MILLI);
}

void printWifiStatus() {
  Serial.printf("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Signal strength: %l dBm\n", WiFi.RSSI());
  Serial.printf("Sensor: %s\n", sensorName);
}
