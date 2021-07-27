/*
 * Detection motion -> capture -> node-red
 *
 * PIR <-+-> ESP GPIO12
 *       |
 *       +--> MQTT --> Node-RED
 *       |     |          |
 *       +-----+--> http://[espIP]/capture
 * 
 */

#include <WiFi.h>
#include <WebServer.h>
#include <esp32cam.h>         // Simply manage OV2640 : https://github.com/yoursunny/esp32cam
#include <PubSubClient.h>     // MQTT : https://github.com/knolleary/pubsubclient
#include <soc/soc.h>          // Manage interrupt
#include <soc/rtc_cntl_reg.h> // Manage interrupt
#include "Credential.h"       // Your private credential WiFi

#define PIN_FLASH 4 // flah board linked on GPIO4
#define ESP_NAME "ESP32-Cam"
#define TOPIC_CAMERASHOT "cam/demo"
#define PIN_MOTION GPIO_NUM_12

WebServer server(80);
WiFiClient clientWiFi;
IPAddress espIP(192, 168, 0, 100); // Define static IP for dns, gatewaty & subnet put in Credential.h

WiFiClient espClient;
PubSubClient clientMQTT(mqttServer, mqttPort, espClient);

void capture()
{
  digitalWrite(PIN_FLASH, HIGH);
  delay(100);
  auto img = esp32cam::capture();
  delay(100);
  digitalWrite(PIN_FLASH, LOW);
  if (img == nullptr)
  {
    server.send(503, "", "");
    return;
  }

  Serial.printf("CAPTURE OK %dx%d %db\n", img->getWidth(), img->getHeight(), static_cast<int>(img->size()));

  server.setContentLength(img->size());
  server.send(200, "image/jpeg");
  clientWiFi = server.client();
  img->writeTo(clientWiFi);
}

void callback(char *topic, byte *payload, unsigned int length)
{
  if (strcmp(topic, TOPIC_CAMERASHOT) == 0)
  {
    capture();
  }
}

void connectWifi()
{
  // wifi
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin(ssid, password);

    if (WiFi.waitForConnectResult() != WL_CONNECTED)
      ;

    // WiFi connected -> start server
    server.on("/capture", capture);
    server.begin();
  }
  delay(100);
  // mqtt
  if (!clientMQTT.connected())
  {
    if (clientMQTT.connect(ESP_NAME, mqttUser, mqttPassword))
    {
      clientMQTT.subscribe(TOPIC_CAMERASHOT);
    }
    else
    {
      delay(5e3);
    }
  }
  delay(100);
}

void initCam()
{
  auto res = esp32cam::Resolution::find(640, 480);
  esp32cam::Config cfg;
  cfg.setPins(esp32cam::pins::AiThinker);
  cfg.setResolution(res);
  cfg.setJpeg(80);
  esp32cam::Camera.begin(cfg);
  delay(100);
}

// void detectsMovement()
static void IRAM_ATTR detectsMovement(void *arg)
{
  capture();
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(100);
  pinMode(PIN_FLASH, OUTPUT);
  digitalWrite(PIN_FLASH, LOW);
  // attachInterrupt : https://github.com/espressif/esp-who/issues/90
  esp_err_t err = gpio_isr_handler_add(PIN_MOTION, &detectsMovement, (void *)PIN_MOTION);
  if (err != ESP_OK)
  {
    Serial.printf("handler add failed with error 0x%x \r\n", err);
  }
  err = gpio_set_intr_type(PIN_MOTION, GPIO_INTR_POSEDGE);
  if(err != ESP_OK)
  {
    Serial.printf("set intr type failed with error 0x%x \r\n", err);
  }

  delay(100);
  // init cam
  initCam();
  // init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(ESP_NAME);
  WiFi.config(espIP, dns, gateway, subnet);
  delay(100);
  // init MQTT
  clientMQTT.setServer(mqttServer, mqttPort);
  clientMQTT.setCallback(callback);
  delay(100);
}

void loop()
{
  if (!clientMQTT.connected())
  {
    connectWifi();
  }
  server.handleClient();
  clientMQTT.loop();
}
