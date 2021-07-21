/*
 * Detection motion -> capture -> node-red
 *
 * PIR <-+-> ESP GPIO2
 *       |
 *       +--> MQTT --> Node-RED
 *             |          |
 *             +--> http://[espIP]/capture
 * 
 */

#include <esp32cam.h>         // Simply manage OV2640 : https://github.com/yoursunny/esp32cam
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>     // MQTT
#include <soc/soc.h>          // Manage interrupt
#include <soc/rtc_cntl_reg.h> // Manage interrupt
#include "Credential.h"       // your private credential WiFi

#define PIN_FLASH 4 // flah board linked on GPIO4
#define ESP_NAME "ESP32-Cam"
#define TOPIC_CAMERASHOT "cam/demo/shot"

WebServer server(80);
IPAddress espIP(192, 168, 0, 100); // Define static IP for dns, gatewaty & subnet put in Credential.h

WiFiClient espClient;
PubSubClient clientMQTT(mqttServer, mqttPort, espClient);
char message_buff[100];

void handleJpeg()
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
  WiFiClient clientWiFi = server.client();
  img->writeTo(clientWiFi);
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived: [");
  Serial.print(topic);
  Serial.println("]"); // Prints out any topic that has arrived and is a topic that we subscribed to.

  int i;
  for (i = 0; i < length; i++)
  {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0'; // We copy payload to message_buff because we can't make a string out of a byte based payload.

  String msgString = String(message_buff); // Converting our payload to a string so we can compare it.
  if (strcmp(topic, TOPIC_CAMERASHOT) == 0)
  {
    handleJpeg();
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
    server.on("/capture", handleJpeg);
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
    { // failed with state
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

static void IRAM_ATTR motionDetect(void *arg)
{
  clientMQTT.publish(TOPIC_CAMERASHOT, "", false);
}

  
void addExternalInterrupt()
{
  // PIR Motion Sensor
  esp_err_t err = gpio_install_isr_service(0);
  err = gpio_isr_handler_add(GPIO_NUM_2, &motionDetect, (void *)2);
  if (err != ESP_OK)
  {
    Serial.printf("handler add failed with error 0x%x \r\n", err);
  }
  err = gpio_set_intr_type(GPIO_NUM_2, GPIO_INTR_POSEDGE); // arduino rising mode
  if (err != ESP_OK)
  {
    Serial.printf("set intr type failed with error 0x%x \r\n", err);
  }
  delay(100);
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(100);
  pinMode(PIN_FLASH, OUTPUT);
  addExternalInterrupt();
  // init cam
  initCam();
  // init wifi
  WiFi.mode(WIFI_STA);
  WiFi.config(espIP, dns, gateway, subnet);
  WiFi.setHostname(ESP_NAME);
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
