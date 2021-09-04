/*
 * Detection motion -> capture -> node-red
 *
 * PIR <-+-> ESP GPIO12
 *       |
 *       +--> MQTT --> Node-RED
 *       |     |          |
 *       +-----+--> http://[espIP]/capture2
 * 
 * TODO : 
 * - how to archive the captures on the rpi
 * - view the stream
 * - implement OTA
 */

#include <WiFi.h>
#include <WebServer.h>
#include <esp32cam.h>         // Simply manage OV2640 : https://github.com/yoursunny/esp32cam
#include <PubSubClient.h>     // MQTT : https://github.com/knolleary/pubsubclient
#include <soc/soc.h>          // Manage interrupt
#include <soc/rtc_cntl_reg.h> // Manage interrupt
#include "Credential.h"       // Your private credential WiFi

#define PIN_FLASH 4    // flah board linked on GPIO4
#define LED_BUILTIN 33 // Onboard LED
#define ESP_NAME "ESP32-Cam"
#define TOPIC_CAMERASHOT "cam/demo"
#define TOPIC_CAMERARAZ "cam/demo/restart"
#define PIN_MOTION GPIO_NUM_12

WebServer server(80);
WiFiClient clientWiFi;
IPAddress espIP(192, 168, 0, 100); // Define static IP for dns, gatewaty & subnet put in Credential.h

WiFiClient espClient;
PubSubClient clientMQTT(mqttServer, mqttPort, espClient);

volatile long lastMovementDetected = 0; // Used to debounce PIR
volatile bool motionDetected = false;   // Set in ISR when PIR detected movement
const long motionDelay = 5000;          // Delay between captures

void blinkLED(int nb)
{
  for (int i = 0; i < nb; i++)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(60);
    digitalWrite(LED_BUILTIN, HIGH);

    if (i < nb - 1)
      delay(100);
  }
}

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
  else if (strcmp(topic, TOPIC_CAMERARAZ) == 0)
  {
    ESP.restart();
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

    blinkLED(2);
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
      delay(3e3);
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

static void IRAM_ATTR detectsMovement(void *arg)
{
  if (millis() - lastMovementDetected > motionDelay)
  {
    motionDetected = true;
    lastMovementDetected = millis();
  }
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(100);
  pinMode(PIN_FLASH, OUTPUT);
  digitalWrite(PIN_FLASH, LOW);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  // init cam
  initCam();
  // attachInterrupt :-> https://github.com/espressif/esp-who/issues/90
  esp_err_t err = gpio_isr_handler_add(PIN_MOTION, &detectsMovement, (void *)PIN_MOTION);
  if (err != ESP_OK)
  {
    Serial.printf("interrupt handler add failed with error 0x%x \r\n", err);
  }
  err = gpio_set_intr_type(PIN_MOTION, GPIO_INTR_NEGEDGE);
  if (err != ESP_OK)
  {
    Serial.printf("set intr type failed with error 0x%x \r\n", err);
  }
  delay(100);
  // init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(ESP_NAME);
  WiFi.config(espIP, dns, gateway, subnet);
  delay(100);
  // init MQTT
  clientMQTT.setServer(mqttServer, mqttPort);
  clientMQTT.setCallback(callback);
  delay(100);
  blinkLED(5);
}

void loop()
{
  if (!clientMQTT.connected())
  {
    connectWifi();
  }
  if (motionDetected)
  {
    clientMQTT.publish(TOPIC_CAMERASHOT, "Motion");
    motionDetected = false;
  }
  server.handleClient();
  clientMQTT.loop();
}
