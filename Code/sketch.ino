#include "header.h"
#include "PubSubClient.h"

// MQTT Var
WiFiClient espClient;
PubSubClient mqtt(espClient);

const char* mqttServer = "broker.mqtt-dashboard.com";
// const char* mqttServer = "test.mosquitto.org";
int portMQTT = 1883;

String sms;
int sw = 0;
double arrOption[] = {0, 0, 0};

// MQTT Function
void callback(char* topic, byte* message, unsigned int length) {
  Serial.print(topic);
  sms = "";

  for (int i = 0; i < length; ++i)
    sms += char(message[i]);

  Serial.print(":");
  Serial.println(sms);
}

void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");

    if (mqtt.connect("Grp10_IoT")) {
      Serial.println(" connected");
      mqtt.subscribe("Grp10_IoT/watering");
    } else {
      Serial.println((" try again in 1 seconds"));
      delay(1000);
    }
  }
}

void mqttLoop() {
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();
}

void checkNodeRed(String sms, int& sw, double arr[]) {
  arr[0] = char(sms[0]) - '0';
  arr[1] = char(sms[1]) - '0';
  arr[2] = char(sms[2]) - '0';
  sw = char(sms[3]) - '0';
  return;
}

void setup() {
  // Setup Servo
  ESP32PWM::allocateTimer(0);
	ESP32PWM::allocateTimer(1);
	ESP32PWM::allocateTimer(2);
	ESP32PWM::allocateTimer(3);
	myservo.setPeriodHertz(50); // standard 50 hz servo
	myservo.attach(SERVO_PIN);  // attaches the servo on pin 12 to the servo object
  myservo.write(0);
  
  // Connect Wifi - Client
  Serial.begin(115200);
  Serial.print("Connecting to WiFi");
  wifiConnect();
  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // MQTT
  mqtt.setServer(mqttServer, portMQTT);
  mqtt.setCallback(callback);
  
  // ThingSpeak
  ThingSpeak.begin(client);

  // Setup Program
  pinMode(NEXT_BUT_PIN, INPUT_PULLUP);
  pinMode(UP_BUT_PIN, INPUT_PULLUP);
  pinMode(OPTION_BUT_PIN, INPUT_PULLUP);
  pinMode(WATERING_BUT_PIN, INPUT_PULLUP);

  pinMode(LDR_PIN, INPUT);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  pinMode(LCD_PIN, OUTPUT);
  LCD.begin(20, 4);

  timeStart = millis() - TIME_UPDATE_THINGSPEAK * 1.;
  delay(100);
}

void LCDTranform() {
  // PROCESS::LCD_00-01
  if (LCDState < 2) {
    while (digitalRead(NEXT_BUT_PIN) == 1) {
      // Update sensor and send to ThingSpeak
      double prev_temp = temp_humi[0];
      updateSensor();
      espToThingSpeak();

      // Receive msg from MQTT
      mqttLoop();
      checkNodeRed(sms, sw, arrOption);
      if(sw == 1) break;

      // Check high temp
      if (temp_humi[0] > 50 && temp_humi[0] != prev_temp) {
        Serial.println("The temperature is too high !!!");
        sendHttpRequest(request1);
      }

      // Print LCD
      if (LCDState == 0) printLCD(temp_humi);
      else printLCD(light_ph);
    }

    delay(TIME_STOP);
    ++LCDState;
  } else {  // PROCESS::LCD_02
    double arr[] = { typeState, waterState, levelState };
    printLCD(arr);

    int option = 1;
    int cursorOption = 0;
    LCD.setCursor(8, 1);
    delay(TIME_STOP);

    while (1) {
      updateWateringLCD(arrOption, typeState, waterState, levelState);

      // Check option button for change mode
      if (digitalRead(OPTION_BUT_PIN) == 0) {
        if (option == 1) option = 0;
        else option = 1;
      }

      // MODE::MODIFY
      if (option == 0) {
        LCD.blink();

        // Check next button for new LCD
        if (digitalRead(NEXT_BUT_PIN) == 0)
          if (cursorOption == 0) { LCD.setCursor(8, 2); cursorOption = 1; }
          else if (cursorOption == 1) { LCD.setCursor(8, 3); cursorOption = 2; }
          else { LCD.setCursor(8, 1); cursorOption = 0; }

        // Check up button for new selection option
        if (digitalRead(UP_BUT_PIN) == 0) {
          if (cursorOption == 0) ++typeState;
          else if (cursorOption == 1) ++waterState;
          else ++levelState;

          updateWateringLCD(arr, typeState, waterState, levelState);
          arr[0] = typeState;
          arr[1] = waterState;
          arr[2] = levelState;
        }

        LCD.setCursor(8, cursorOption + 1);
        delay(TIME_STOP);
      } else {  // MODE::WAITING FOR WATERING
        LCD.noBlink();
        if (digitalRead(WATERING_BUT_PIN) == 0 || sw == 1) {
          int node_red_control = 0;

          // Update watering state if have request from node-red
          if (sw == 1) {
            node_red_control = 1;
            updateWateringLCD(arr, arrOption[0], arrOption[1], arrOption[2]);
          }
            
          int r0 = random(0, 5);

          delay(TIME_STOP);
          LCD.setCursor(0, 0);
          LCD.print("-- WARTERING -- 000%");
          wateringComplete = 1;

          // Setup Servo
          double servoAngle;
          if (sw == 1)
            servoAngle = setupServo((int)arrOption[0] % 3, (int)arrOption[2] % 3);
          else
            servoAngle = setupServo((int)arr[0] % 3, (int)arr[2] % 3);
          
          wateringPercent = 0;
          while (digitalRead(WATERING_BUT_PIN) == 1 && sw == 1 && node_red_control == 1
                || digitalRead(WATERING_BUT_PIN) == 1 && node_red_control == 0) {
            // Update sensor and send to ThingSpeak
            double prev_temp = temp_humi[0];
            updateSensor();
            espToThingSpeak();

            // Receive msg from MQTT
            mqttLoop();
            checkNodeRed(sms, sw, arrOption);
            
            // Check high temp
            if (temp_humi[0] > 50) {
              Serial.println("The temperature is too high !!!");
              sendHttpRequest(request1);
              
              sw = 0;
              mqtt.publish("Grp10_IoT/receive_watering_but", "0");
              delay(TIME_DELAY_PUBLIC_MQTT);
              break;
            }

            // Probability of rain when watering start is 20% - r0
            // Probability of rain while watering is 2% - r
            int r = random(0, 100);
            if (r0 == 0 || r <= 1) {
              ranning = 1;
              Serial.println("It's raining !!!");
              sendHttpRequest(request2);

              sw = 0;
              mqtt.publish("Grp10_IoT/receive_watering_but", "0");
              delay(TIME_DELAY_PUBLIC_MQTT);
              break;
            }

            myservo.write(servoAngle);

            // Cal watering delay time
            double wateringDelayTime = 0;
            if (sw == 1) wateringDelayTime = TIME_WATERING_UNIT * (arrOption[1] + 1) / (arrOption[2] + 1);
            else wateringDelayTime = TIME_WATERING_UNIT * (waterState + 1) / (typeState + 1);
            delay(wateringDelayTime);

            LCD.setCursor(16, 0);
            wateringPercent += 10;

            // Print watering percent
            if (wateringPercent < 10) LCD.print("00" + String(wateringPercent));
            else if (wateringPercent >= 10 && wateringPercent < 100) LCD.print("0" + String(wateringPercent));
            else LCD.print(String(wateringPercent));

            // Check watering complete
            if (wateringPercent >= 100) {
              sw = 0;
              mqtt.publish("Grp10_IoT/receive_watering_but", "0");
              delay(TIME_DELAY_PUBLIC_MQTT);
              break;
            }
          }

          myservo.write(0);
          LCD.setCursor(0, 0);
          LCD.print("-- WARTERED --  ");
          
          if (wateringPercent == 0) wateringComplete = 0;
          else wateringComplete = 2;
          
          sw = 0;
          mqtt.publish("Grp10_IoT/receive_watering_but", "0");
          delay(TIME_DELAY_PUBLIC_MQTT);
        }

        // Check next button for new LCD
        if (digitalRead(NEXT_BUT_PIN) == 0) break;
        
        delay(TIME_STOP);
      }

      // Update sensor and send to ThingSpeak
      double prev_temp = temp_humi[0];
      updateSensor();
      espToThingSpeak();

      // Receive msg from MQTT
      mqttLoop();
      checkNodeRed(sms, sw, arrOption);

      // Check high temp
      if (temp_humi[0] > 50 && temp_humi[0] != prev_temp) {
        Serial.println("The temperature is too high !!!");
        sendHttpRequest(request1);
      }
    }

    LCDState = 0;
  }
}

void loop() {
  LCDTranform();
  delay(TIME_STOP);
}