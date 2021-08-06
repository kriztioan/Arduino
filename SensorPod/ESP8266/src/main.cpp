/**
 *  @file    main.cpp
 *  @brief   ESP8266 WiFi Relay
 *  @author  KrizTioaN (christiaanboersma@hotmail.com)
 *  @date    2021-08-06
 *  @note    BSD-3 licensed
 *
 ***********************************************/

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClientSecure.h>

#include <TZ.h>
#include <coredecls.h>

#include <time.h>

uint16_t wifi_status = 425;
unsigned long wifi_timeout;

uint8_t NTPTimeValid = 0;
void NTPCallback() {
  NTPTimeValid = 1;
  settimeofday_cb((TrivialCB) nullptr);
}

const char *ssid = "WIFI SSID";
const char *password = "WIFI PASSWORD";
const char *hostname = "SensorPod";

WiFiEventHandler wifi_connected, wifi_disconnected;

void setup() {

  Serial.begin(19200);
  Serial.print("\r\nBooting EPS8266\r\n");
  Serial.printf("Chip-ID: %4x\r\n", ESP.getChipId());
  Serial.printf("CPU: %d MHz\r\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash: %d MiB\r\n", ESP.getFlashChipRealSize() / 1024 / 1024);
  Serial.printf("Speed: %d MHz\r\n", ESP.getFlashChipSpeed() / 1000000);
  Serial.printf("Core: %s\r\n", ESP.getCoreVersion().c_str());
  Serial.printf("SDK: %s\r\n", ESP.getSdkVersion());
  Serial.printf("Free: %d KiB\r\n", ESP.getFreeSketchSpace() / 1024);
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n", mac[5], mac[4],
                mac[3], mac[2], mac[1], mac[0]);
  Serial.println("Connecting to WiFi");

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);

  wifi_connected =
      WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &e) {
        wifi_status = 200;
        Serial.printf("Hostname: %s\r\n", WiFi.hostname().c_str());
        Serial.printf("Connected to %s using IP address %s\r\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        Serial.println("EPS8266 Ready");
      });

  wifi_disconnected = WiFi.onStationModeDisconnected(
      [](const WiFiEventStationModeDisconnected &e) {
        wifi_status = 410;
        wifi_timeout = millis();
        Serial.println("Connection lost\r\nreconnecting ...");
      });

  WiFi.begin(ssid, password);
  wifi_timeout = millis();

  Serial.println("Configuring NTP\r\nUsing timezone America/Los_Angeles");
  settimeofday_cb(NTPCallback);
  configTime(TZ_America_Los_Angeles, "pool.ntp.org");

  ArduinoOTA.onStart([]() {
    String type;

    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem"; // U_FS

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
  });

  ArduinoOTA.onEnd([]() {});

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});

  ArduinoOTA.onError([](ota_error_t error) {});

  ArduinoOTA.begin();
}

void relayURL() {

  String host = Serial.readStringUntil('\n');

  if (host.length() == 0) {
    Serial.println("Invalid host");
    Serial.write(0x04);
    return;
  }

  WiFiClient client;

  if (!client.connect(host, 80)) {
    Serial.println("Connection failed");
    Serial.write(0x04);
    return;
  }

  client.print("GET /index.php HTTP/1.0\r\nConnection: close\r\n\r\n");

  uint8_t pck_len = 56;

  unsigned long timeout = millis();

  while (client.connected() || client.available()) {
    yield();
    int available = client.available();
    if (!client.connected() && available < pck_len)
      pck_len = available;
    if (available >= pck_len) {
      for (uint8_t i = 0; i < pck_len; i++)
        Serial.write(client.read());
      if (pck_len < 56)
        break;
      while (!Serial.available()) {
        yield();
        if ((millis() - timeout) >= 5000ul) {
          Serial.println("ACK timed out");
          Serial.write(0x04);
          client.stop();
          return;
        }
      }
      if (Serial.read() != 0x06) {
        Serial.println("Invalid response");
        break;
      }
    } else if ((millis() - timeout) >= 5000ul) {
      Serial.println("Request timed out");
      break;
    }
  }

  Serial.write(0x04);

  client.stop();
}

void relayNTPBasedTime() {

  struct NTPResponse {
    uint8_t year;
    uint8_t month;
    uint8_t weekday;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t error;
  } ntp;

  if (NTPTimeValid) {
    time_t epoch = time(NULL);
    struct tm *tm_s = localtime(&epoch);
    ntp.second = (uint8_t)tm_s->tm_sec;
    ntp.minute = (uint8_t)tm_s->tm_min;
    ntp.hour = (uint8_t)tm_s->tm_hour;
    ntp.day = (uint8_t)tm_s->tm_mday;
    ntp.month = (uint8_t)tm_s->tm_mon + 1;
    ntp.year = (uint8_t)tm_s->tm_year - 100;
    ntp.weekday = (uint8_t)tm_s->tm_wday;
    ntp.error = 200;
  } else
    ntp.error = 202;

  Serial.write((char *)&ntp, sizeof(struct NTPResponse));
}

uint16_t getJSON(WiFiClient &client, const char *host, const char *path) {

  if (!client.connect(host, 443))
    return 1;

  client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ESP8622 "
                "(christiaanboersma@hotmail.com)\r\nAccept: "
                "application/json\r\nConnection: close\r\n\r\n",
                path, host);

  unsigned long timeout = millis();
  while (client.available() == 0)
    if ((millis() - timeout) >= 10000ul)
      return 2;

  char status[32] = {'\0'};

  client.readBytesUntil('\r', status, sizeof(status));

  if (strncmp(status + 9, "200", 3) == 0) {
    if (!client.find("\r\n\r\n"))
      return 9;
    return 200;
  }

  if (strncmp(status + 9, "303", 3) == 0) {
    if (!client.find("Location: "))
      return 4;
    if (!client.find("//"))
      return 5;
    char url_redirect[96];
    size_t url_redirect_len =
        client.readBytesUntil('\r', url_redirect, sizeof(url_redirect));
    if (url_redirect_len <= 0)
      return 6;
    char host_redirect[64], path_redirect[96], *p;
    p = strchr(url_redirect, '/');
    if (p == NULL)
      return 7;
    size_t host_redirect_len = p - url_redirect;
    memcpy(host_redirect, url_redirect, host_redirect_len);
    host_redirect[host_redirect_len] = '\0';
    size_t path_redirect_len = url_redirect_len - host_redirect_len;
    memcpy(path_redirect, p, path_redirect_len);
    path_redirect[path_redirect_len] = '\0';
    return getJSON(client, host_redirect, path_redirect);
  }

  return atoi(status + 9);
}

void relayUVIdx() {

  struct UVIResponse {
    uint16_t error;
    uint8_t uv;
    uint8_t padding;
  } uvi;

  WiFiClientSecure client;

  const char *host = "data.epa.gov",
             *path = "/efservice/getEnvirofactsUVDAILY/ZIP/94043/JSON";

  client.setInsecure();

  uvi.error = getJSON(client, host, path);
  if (uvi.error != 200) {
    Serial.write((char *)&uvi, sizeof(struct UVIResponse));
    client.stop();
    return;
  }

  StaticJsonDocument<128> doc;

  DeserializationError error = deserializeJson(doc, client);
  if (error) {
    uvi.error = 10;
    Serial.write((char *)&uvi, sizeof(struct UVIResponse));
    client.stop();
    return;
  }

  if (!doc[0]["UV_INDEX"]) {
    uvi.error = 11;
    Serial.write((char *)&uvi, sizeof(struct UVIResponse));
    client.stop();
    return;
  }

  uvi.uv = doc[0]["UV_INDEX"];

  client.stop();

  Serial.write((char *)&uvi, sizeof(struct UVIResponse));
}

void relayWeatherFC() {

  struct WFCResponse {
    float temperature;
    float dewpoint;
    float windDirection;
    float windSpeed;
    float windGust;
    float relativeHumidity;
    uint16_t error;
    uint8_t padding[2];
  } wfc;

  WiFiClientSecure client;

  const char *host = "api.weather.gov",
             *path = "/stations/LOAC1/observations/latest",
             *fingerprint =
                 "40 34 2C 79 4B 19 15 22 60 67 4D 4F 5B DE 60 5E B5 6D 09 55";

  client.setFingerprint(fingerprint);

  wfc.error = getJSON(client, host, path);
  if (wfc.error != 200) {
    Serial.write((char *)&wfc, sizeof(struct WFCResponse));
    client.stop();
    return;
  }

  StaticJsonDocument<128> doc;

  DeserializationError error;
  const char *keys[] = {
      "\"temperature\": ", "\"dewpoint\": ", "\"windDirection\": ",
      "\"windSpeed\": ",   "\"windGust\": ", "\"relativeHumidity\": "};
  for (int i = 0; i < 6; i++) {
    if (!client.find(keys[i])) {
      wfc.error = 6;
      Serial.write((char *)&wfc, sizeof(struct WFCResponse));
      client.stop();
      return;
    }
    error = deserializeJson(doc, client);
    if (error) {
      wfc.error = 7;
      Serial.write((char *)&wfc, sizeof(struct WFCResponse));
      client.stop();
      return;
    }
    switch (i) {
    case 0:
      wfc.temperature = doc["value"];
      break;
    case 1:
      wfc.dewpoint = doc["value"];
      break;
    case 2:
      wfc.windDirection = doc["value"];
      break;
    case 3:
      wfc.windSpeed = doc["value"];
      break;
    case 4:
      wfc.windGust = doc["value"];
      break;
    case 5:
      wfc.relativeHumidity = doc["value"];
    }
  }
  client.stop();

  Serial.write((char *)&wfc, sizeof(struct WFCResponse));
}

void postJSONMsg() {

  String host = Serial.readStringUntil('\n'),
         path = Serial.readStringUntil('\n'),
         json = Serial.readStringUntil('\n');

  if (host.length() == 0 || path.length() == 0 || json.length() == 0) {
    Serial.print(400);
    return;
  }

  WiFiClient client;

  if (!client.connect(host, 80)) {
    Serial.print(400);
    return;
  }

  client.printf("POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ESP8622 "
                "(christiaanboersma@hotmail.com)\r\nConnection: "
                "close\r\nAccept: application/json\r\nContent-Type: "
                "application/json\r\nContent-Length: %d\r\n\r\n%s",
                path.c_str(), host.c_str(), json.length(), json.c_str());

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if ((millis() - timeout) >= 10000ul) {
      Serial.print(504);
      return;
    }
  }

  char status[32] = {'\0'};

  client.readBytesUntil('\r', status, sizeof(status));

  Serial.print(atoi(status + 9));
}

void loop() {

  ArduinoOTA.handle();

  if (wifi_status != 200 && (millis() - wifi_timeout) > 60000ul) {
    Serial.println("Trying to connect to WiFi timed out ... restarting");
    ESP.restart();
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    if (command.length() > 0) {
      if (command == "WFC")
        relayWeatherFC();
      else if (command == "PST")
        postJSONMsg();
      else if (command == "UVI")
        relayUVIdx();
      else if (command == "NTP")
        relayNTPBasedTime();
      else if (command == "URL")
        relayURL();
      else if (command == "WIF")
        Serial.write((uint8_t *)&wifi_status, sizeof(wifi_status));
      else if (command == "RSP")
        Serial.write(0x06);
    }
  }
}
