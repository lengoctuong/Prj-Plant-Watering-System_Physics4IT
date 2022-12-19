// DHT sensor library for ESPx
// PubSubClient

#include <WiFi.h>
#include "PubSubClient.h"

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqttServer = "broker.mqtt-dashboard.com";
int port = 1883;
String studentCode;

WiFiClient espClient;
PubSubClient client(espClient);

void wifiConnect() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println(" Connected!");
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print(topic);
  studentCode = "";

  for (int i = 0; i < length; ++i)
    studentCode += char(message[i]);

  Serial.print(":");
  Serial.println(studentCode);
}

void mqttReconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    if (client.connect("20127383")) {
      Serial.println(" connected");
      client.subscribe("20127383/quiz06_receive");
    }
    else {
      Serial.println((" try again in 5 seconds"));
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial.print("Connecting to WiFi");
  wifiConnect();

  client.setServer(mqttServer, port);
  client.setCallback(callback);

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (!client.connected())
    mqttReconnect();

  client.loop();
  
  if (studentCode == "20127383")
    client.publish("20127383/quiz06_send", "Lê Ngọc Tường");
  else if (studentCode == "20127484")
    client.publish("20127383/quiz06_send", "Nguyễn Tư Duy");
  else if (studentCode == "20127588")
    client.publish("20127383/quiz06_send", "Nguyễn Tấn Phát");
  else
    client.publish("20127383/quiz06_send", "[ERROR]_NotExist");
}
