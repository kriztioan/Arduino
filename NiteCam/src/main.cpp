/**
 *  @file    main.cpp
 *  @brief   ESP8266 + VC0706 Night Camera
 *  @author  KrizTioaN (christiaanboersma@hotmail.com)
 *  @date    2021-08-06
 *  @note    BSD-3 licensed
 *
 ***********************************************/

#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include <TZ.h>
#include <coredecls.h>

ESP8266WebServer server(80);

void VC0706_init() {

  uint8_t res[128];

  uint8_t rst[] = {0x56, 0x00, 0x26, 0x00};
  uint8_t ver[] = {0x56, 0x00, 0x11, 0x00};
  uint8_t pxl[] = {0x56, 0x00, 0x31, 0x05, 0x04, 0x01, 0x00, 0x19, 0x00};
  uint8_t cmp[] = {0x56, 0x00, 0x31, 0x05, 0x01, 0x12, 0x04, 0x80};

  Serial.begin(115200);

  Serial.write(rst, sizeof(rst));
  Serial.readBytes(res, 128);

  Serial.write(ver, sizeof(ver));
  Serial.readBytes(res, 16);

  Serial.write(pxl, sizeof(pxl));
  Serial.readBytes(res, 5);

  Serial.write(cmp, sizeof(cmp));
  Serial.readBytes(res, 5);
}

void VC0706Status() {

  uint8_t res[128];

  uint8_t rst[] = {0x56, 0x00, 0x26, 0x00};
  uint8_t ver[] = {0x56, 0x00, 0x11, 0x00};
  uint8_t pxl[] = {0x56, 0x00, 0x31, 0x05, 0x04, 0x01, 0x00, 0x19, 0x00};
  // uint8_t cmp[] = { 0x56, 0x00, 0x31, 0x05, 0x01, 0x12, 0x04, 0x80 };
  uint8_t mtn[] = {0x56, 0x00, 0x38, 0x00};

  Serial.begin(115200);

  Serial.write(rst, sizeof(rst));
  Serial.readBytes(res, 128);

  uint8_t r1[4];
  memcpy(r1, res, 4);

  Serial.write(ver, sizeof(ver));
  Serial.readBytes(res, 16);

  uint8_t r2[4];
  memcpy(r2, res, 4);

  char vers[12];
  memcpy(vers, res + 5, 11);
  vers[11] = '\0';

  Serial.write(pxl, sizeof(pxl));
  Serial.readBytes(res, 5);

  uint8_t r3[4];
  memcpy(r3, res, 4);

  Serial.write(pxl, sizeof(pxl));
  Serial.readBytes(res, 5);

  uint8_t r4[4];
  memcpy(r4, res, 4);

  Serial.write(mtn, sizeof(mtn));
  Serial.readBytes(res, 6);

  uint8_t r5 = res[5];

  char buf[192];
  snprintf(
      buf, 192,
      "reset: 0x%X 0x%X 0x%X 0x%X\nversion: 0x%X 0x%X 0x%X 0x%X (%s)\nsize: "
      "0x%X 0x%X 0x%X 0x%X\ncompression: 0x%X 0x%X 0x%X 0x%X\nmotion: 0x%X\n",
      r1[0], r1[1], r1[2], r1[3], r2[0], r2[1], r2[2], r2[3], vers, r3[0],
      r3[1], r3[2], r3[3], r4[0], r4[1], r4[2], r4[3], r5);

  server.send(200, "text/plain", buf);
}

void JPEGPicture() {

  uint8_t res[128];

  uint8_t takephoto[] = {0x56, 0x00, 0x36, 0x01, 0x00};
  uint8_t bufflen[] = {0x56, 0x00, 0x34, 0x01, 0x00};
  uint8_t readphoto[] = {0x56, 0x00, 0x32, 0x0C, 0x00, 0x0A, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x0,  0x10, 0x00, 0x00, 0x64};
  uint8_t rsm[] = {0x56, 0x00, 0x36, 0x01, 0x02};

  Serial.write(rsm, sizeof(rsm));
  Serial.readBytes(res, 5);

  if (res[3] != 0x0) {
    server.send(500, "text/plain", "Failed to move to next frame");
    return;
  }

  Serial.write(takephoto, sizeof(takephoto));
  Serial.readBytes(res, 5);

  if (res[3] != 0x0) {
    server.send(500, "text/plain", "Failed to take picture");
    return;
  }

  Serial.write(bufflen, sizeof(bufflen));

  Serial.readBytes(res, 9);

  int32_t bytes = res[5];
  bytes <<= 8;
  bytes |= res[6];
  bytes <<= 8;
  bytes |= res[7];
  bytes <<= 8;
  bytes |= res[8];

  if (res[3] != 0x0) {
    server.send(500, "text/plain", "Picture is -zero- bytes in size");
    return;
  }

  server.sendHeader("Refresh", "30");
  server.setContentLength(bytes);
  server.send(200, "image/jpeg", "");

  WiFiClient client = server.client();

  Serial.setTimeout(200);

  uint16_t addr = 0, n;
  uint8_t buf[517];
  while (bytes > 0) {
    n = _min(512, bytes);
    readphoto[8] = addr >> 8;
    readphoto[9] = addr & 0xFF;
    readphoto[12] = n >> 8;
    readphoto[13] = n & 0xFF;
    Serial.write(readphoto, sizeof(readphoto));
    if (Serial.readBytes(buf, n + 5) != (n + 5U))
      continue;
    client.write(buf + 5, n);
    addr += n;
    bytes -= n;
  }
  Serial.readBytes(buf, 517);

  Serial.setTimeout(1000);

  rsm[4] = 0x03;
  Serial.write(rsm, sizeof(rsm));
  Serial.readBytes(buf, 5);
}

void setup() {

  const char *ssid = "WIFI SSID";
  const char *password = "WIFI PASSWORD";

  WiFi.hostname("NiteCam");

  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }

  configTime(TZ_America_Los_Angeles, "pool.ntp.org");

  ArduinoOTA.setHostname("NiteCam");

  ArduinoOTA.onStart([]() {
    String type;

    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem";
  });

  ArduinoOTA.onEnd([]() {});

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});

  ArduinoOTA.onError([](ota_error_t error) {});

  ArduinoOTA.begin();

  server.on("/status", VC0706Status);

  server.on("/", JPEGPicture);

  server.onNotFound(JPEGPicture);

  VC0706_init();

  delay(2000);

  server.begin();
}

void loop() {

  ArduinoOTA.handle();

  server.handleClient();
}
