#include <Adafruit_Sensor.h>
#include <DHT_U.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <FastLED.h>

// Defining Pins
#define DHTPIN 12
#define LED 26
#define SERVO_PIN 2
#define LED_PIN 4
#define NUM_LEDS 16
#define DHTTYPE DHT22
#define LDR_PIN 34
#define SOIL_MOISTURE_PIN 35

DHT_Unified dht(DHTPIN, DHTTYPE);

// Servo motor
Servo servo;

// WS2812 LED strip
CRGB leds[NUM_LEDS];

// MQTT Credentials
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqttServer = "broker.hivemq.com";
const char* clientID = "ESP32-wokwi";

// Các topic MQTT riêng biệt cho từng loại dữ liệu
const char* tempTopic = "sensors/temperature";
const char* humTopic = "sensors/humidity";
const char* lightTopic = "sensors/light";
const char* soilTopic = "sensors/soil_moisture";

// Parameters for non-blocking delay
unsigned long previousMillis = 0;
const long interval = 10000;

// Setting up WiFi and MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(clientID)) {
      Serial.println("MQTT connected");
      client.subscribe("lights");
      client.subscribe("servo");
      client.subscribe("lights/neopixel");
      Serial.println("Topic Subscribed");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  String data = "";
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    data += (char)payload[i];
  }
  Serial.println();
  Serial.println("-----------------------");

  if (String(topic) == "lights") {
    if (data == "ON") {
      digitalWrite(LED, HIGH);
      Serial.println("LED is ON");
    } else {
      digitalWrite(LED, LOW);
      Serial.println("LED is OFF");
    }
  } else if (String(topic) == "servo") {
    int degree = data.toInt();
    Serial.print("Moving servo to degree: ");
    Serial.println(degree);
    servo.write(degree);
  } else if (String(topic) == "lights/neopixel") {
    int red, green, blue;
    sscanf(data.c_str(), "%d,%d,%d", &red, &green, &blue);
    Serial.printf("Setting NeoPixel color to (R,G,B): %d,%d,%d\n", red, green, blue);
    fill_solid(leds, NUM_LEDS, CRGB(red, green, blue));
    FastLED.show();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize sensors and components
  dht.begin();
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // Setup servo
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(0);

  // Setup WS2812 LED strip
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);

  // Setup WiFi and MQTT
  setup_wifi();
  client.setServer(mqttServer, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    // Read sensor data
    sensors_event_t temp_event, hum_event;
    dht.temperature().getEvent(&temp_event);
    dht.humidity().getEvent(&hum_event);
    
    float temp = isnan(temp_event.temperature) ? -999.0 : temp_event.temperature;
    float hum = isnan(hum_event.relative_humidity) ? -999.0 : hum_event.relative_humidity;
    int lightValue = analogRead(LDR_PIN);
    int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);

    // In giá trị ra Serial Monitor
    Serial.printf("Temperature: %.2f C\n", temp);
    Serial.printf("Humidity: %.2f %%\n", hum);
    Serial.printf("LDR Value: %d\n", lightValue);
    Serial.printf("Soil Moisture Value: %d\n", soilMoistureValue);

    // Phân loại độ ẩm đất và điều khiển đèn LED (nếu cần)
    if (soilMoistureValue > 2000) {
      Serial.println("Soil is Dry");
    } else if (soilMoistureValue >= 1000) {
      Serial.println("Soil is Moist");
    } else {
      Serial.println("Soil is Wet");
    }

    // Gửi dữ liệu lên MQTT với các topic riêng biệt
    client.publish(tempTopic, String(temp).c_str());
    client.publish(humTopic, String(hum).c_str());
    client.publish(lightTopic, String(lightValue).c_str());
    client.publish(soilTopic, String(soilMoistureValue).c_str());

    Serial.println("Data published successfully to separate topics.");
  }
}