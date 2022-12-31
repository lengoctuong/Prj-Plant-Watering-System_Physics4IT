#include "DHTesp.h"
#include <LiquidCrystal.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include "ThingSpeak.h"

// Process Define
#define NEXT_BUT_PIN            19
#define OPTION_BUT_PIN          21
#define UP_BUT_PIN              22
#define WATERING_BUT_PIN        23
#define DHT_PIN                 18
#define LDR_PIN                 35
#define LCD_PIN                 14
#define SERVO_PIN               25
#define TIME_STOP               200
#define TIME_WATERING_UNIT      500
#define TIME_RECONNECT_MQTT     1000
#define TIME_DELAY_PUBLIC_MQTT  500
#define TIME_UPDATE_THINGSPEAK  15000 //  3600000

// Wifi Var
WiFiClient client;
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// IFTTT Var
const char* host = "maker.ifttt.com";
const char* request1 = "/trigger/high_temp_app_notification/json/with/key/fkwmMw_ZKsnpsw54KQUxDvBJKOUnyKge610IlRHg-N-";
const char* request2 = "/trigger/rain_email_notification/json/with/key/fkwmMw_ZKsnpsw54KQUxDvBJKOUnyKge610IlRHg-N-";

// const char* request1 = "/trigger/high_temp/json/with/key/mcxhGhi8rt2g6YyMsIerfKxnj1mSE6y4p99NQxmau4R";
// const char* request2 = "/trigger/rain/json/with/key/mcxhGhi8rt2g6YyMsIerfKxnj1mSE6y4p99NQxmau4R";
const int port = 80;

// Thing Speak Var
unsigned long sensorChannelNumber = 1990625;;
const char* sensorWriteAPIKey = "HLJIECNFJZYUZ877";
const char* sensorReadAPIKey = "0NDWKZIYHUXYI5KJ";

unsigned long wateringChannelNumber = 1994623;;
const char* wateringWriteAPIKey = "MCMWKFIM9BOW6AOW";
const char* wateringReadAPIKey = "1XL2JH9Q53MTTSUC";

// Process Var
DHTesp dhtSensor;
LiquidCrystal LCD(12, 13, 5, 4, 2, 15);
Servo myservo;

double timeStart = 0;
int LCDState = 0;

double temp_humi[2] = {0, 0};
int temp_humi_fields[2] = {1, 2};
double light_ph[2] = {0, 0};
int light_ph_fields[2] = {3, 4};

int ranning = 0;
int wateringPercent = 0;
int wateringComplete = 0; // 0: non-watering; 1: watering; 2: watered
double typeState = 0;
double waterState = 0;
double levelState = 0;
const char* types[] = {(char*)("Nho giot  "), (char*)("Phun suong"), (char*)("Xit nuoc  ")};
const float waters[] = {0.5, 1, 2, 3, 4, 5};
const char* levels[] = {(char*)("Nhe "), (char*)("Vua "), (char*)("Manh")};
int watering_fields[] = {1, 2, 3, 4, 5, 6};


// Wifi Function
void wifiConnect() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println(" Connected!");
}

// IFTTT Function
void sendHttpRequest(const char* request) {
  WiFiClient client;
  while(!client.connect(host, port)) {
    Serial.println("Connection failed");
    delay(1000);
  }

  client.print("GET " + String(request) + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  delay(500);
}

// ThingSpeak Function
void espToThingSpeak() {
  if (millis() - timeStart >= TIME_UPDATE_THINGSPEAK) {
    ThingSpeak.setField(temp_humi_fields[0], int(round(temp_humi[0])));
    ThingSpeak.setField(temp_humi_fields[1], int(round(temp_humi[1])));
    ThingSpeak.setField(light_ph_fields[0], int(round(light_ph[0])));
    ThingSpeak.setField(light_ph_fields[1], int(round(light_ph[1])));
    int sensorChannelRet = ThingSpeak.writeFields(sensorChannelNumber, sensorWriteAPIKey);

    ThingSpeak.setField(watering_fields[0], (int)typeState % 3);
    ThingSpeak.setField(watering_fields[1], (int)waterState % 6);
    ThingSpeak.setField(watering_fields[2], (int)levelState % 3);
    ThingSpeak.setField(watering_fields[3], ranning);
    ThingSpeak.setField(watering_fields[4], wateringPercent);
    ThingSpeak.setField(watering_fields[5], wateringComplete);
    int wateringChannelRet = ThingSpeak.writeFields(wateringChannelNumber, wateringWriteAPIKey);
    
    if (sensorChannelRet == 200 && wateringChannelRet == 200) {
      Serial.println("Sensor channel update successful.");
      Serial.println("\tf1: " + String(temp_humi[0]));
      Serial.println("\tf2: " + String(temp_humi[1]));
      Serial.println("\tf3: " + String(light_ph[0]));
      Serial.println("\tf4: " + String(light_ph[1]));

      Serial.println("Watering channel update successful.");
      Serial.println("\tf1: " + String((int)typeState % 3));
      Serial.println("\tf2: " + String((int)waterState % 6));
      Serial.println("\tf3: " + String((int)levelState % 3));
      Serial.println("\tf4: " + String(ranning));
      Serial.println("\tf5: " + String(wateringPercent));
      Serial.println("\tf6: " + String(wateringComplete));
      Serial.println("-----------------------------------\n");

      timeStart = millis();
    } else if (sensorChannelRet != 200 && wateringChannelRet == 200) {
      Serial.println("Problem updating sensor channel. HTTP error code ");
      Serial.println(sensorChannelRet);

      // timeStart += 3000;
    } else if (sensorChannelRet == 200 && wateringChannelRet != 200) {
      Serial.println("Problem updating watering channel. HTTP error code ");
      Serial.println(wateringChannelRet);

      // timeStart += 3000;
    } else {
      Serial.println("Problem updating sensor and watering channel. HTTP error code ");
      Serial.println(String(sensorChannelRet) + " " + String(wateringChannelRet));

      // timeStart += 3000;
    }
  }
}

// Process Function
float calLux(int analogVal)
{
  const float GAMMA = 0.7;
  const float RL10 = 50;

  float voltage = analogVal / 4064. * 5;
  float resistance = 2000 * voltage / (1 - voltage / 5);

  return pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, 1 / GAMMA);
}

void updateSensor() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  temp_humi[0] = data.temperature;
  temp_humi[1] = data.humidity;

  light_ph[0] = calLux(analogRead(LDR_PIN));
  light_ph[1] = random(500, 1000) / 100.;
}

double setupServo(int type, int level) {
  int angle;
  double k;

  if (type == 0) angle = 10;
  else if (type == 1) angle = 30;
  else angle = 150;

  if (level == 0) k = 1.0 / 3.0;
  else if (level == 1) k = 2.0 / 3.0;
  else k = 1.0;

  return angle * k;
}

void updateWateringLCD(double preVals[], double newTypeState, double newWaterState, double newLevelState) {
  if (preVals[0] != newTypeState || preVals[1] != newWaterState || preVals[2] != newLevelState) {
    LCD.setCursor(8, 1);
    LCD.print(types[(int)newTypeState % 3]);

    LCD.setCursor(8, 2);
    LCD.print(waters[(int)newWaterState % 6]);

    LCD.setCursor(8, 3);
    LCD.print(levels[(int)newLevelState % 3]);
  }
}

void printLCD(double arr[]) {
  if (LCDState == 0) {
    LCD.setCursor(0, 0);      LCD.print("Temp: " + String(arr[0]) + " Celcius  ");  
    LCD.setCursor(0, 1);      LCD.print("Humi: " + String(arr[1]) + "%         ");
    LCD.setCursor(0, 2);      LCD.print("                    ");
    LCD.setCursor(0, 3);      LCD.print("                    ");
  }
  else if (LCDState == 1) {
    LCD.setCursor(0, 0);      LCD.print("Light: " + String(arr[0]) + " lux   ");
    LCD.setCursor(0, 1);      LCD.print("   pH: " + String(arr[1]) + "         ");
    LCD.setCursor(0, 2);      LCD.print("                    ");
    LCD.setCursor(0, 3);      LCD.print("                    ");
  }
  else {
    LCD.setCursor(0, 0);      LCD.print("       OPTION       ");
    LCD.setCursor(0, 1);      LCD.print("  Type: " + String(types[(int)arr[0] % 3]));
    LCD.setCursor(0, 2);      LCD.print(" Water: " + String(waters[(int)arr[1] % 6]) + "L");
    LCD.setCursor(0, 3);      LCD.print(" Level: " + String(levels[(int)arr[2] % 3]));
  }
}