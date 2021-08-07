
const char *ssid = "your_ssid";
const char *password = "password";
// info provider -> cmd:ipconfig -> mine is
IPAddress dns(192, 168, 0, 1);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

const char *mqttServer = "@host";
const int mqttPort = 1883;
// if exist
const char *mqttUser = "user";
const char *mqttPassword = "password";
