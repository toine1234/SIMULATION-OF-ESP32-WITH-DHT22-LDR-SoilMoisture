#include <Adafruit_Sensor.h>
#include <DHT_U.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <FastLED.h>

#define DHTPIN 12
#define LED 26
#define SERVO_PIN 2
#define LED_PIN 4
#define NUM_LEDS 16

#define DHTTYPE    DHT22

#define LDR_PIN 34

#define SOIL_MOISTURE_PIN 35

DHT_Unified dht(DHTPIN, DHTTYPE);
uint32_t delayMS;

// Servo motor
Servo servo;

// WS2812 LED strip
CRGB leds[NUM_LEDS];

// MQTT Credentials
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqttServer = "broker.hivemq.com";
const char* clientID = "ESP32-wokwi";
const char* topic = "Tempdata";

// Parameters for using non-blocking delay
unsigned long previousMillis = 0;
const long interval = 10000;

String msgStr = "";
float temp, hum;
int lightValue;
int soilMoistureValue;

// Setting up WiFi and MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  Serial.println("Connecting to WiFi.....");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("\nIP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("\nAttempting MQTT connection...");
    if (client.connect(clientID)) {
      Serial.println("\nMQTT connected");
      client.subscribe("\nlights");
      client.subscribe("\nservo");
      client.subscribe("\nlights/neopixel");
      Serial.println("\nTopic Subscribed");
    }
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println("\ntry again in 5 seconds");
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
  Serial.print("Message size: ");
  Serial.println(length);
  Serial.println();
  Serial.println("-----------------------");
  Serial.println(data);

  if (String(topic) == "lights") {
    if (data == "ON") {
      Serial.println("LED");
      digitalWrite(LED, HIGH);
    }
    else {
      digitalWrite(LED, LOW);
    }
  }
  else if (String(topic) == "servo") {
    int degree = data.toInt(); // Convert the received data to an integer
    Serial.print("Moving servo to degree: ");
    Serial.println(degree);
    servo.write(degree); // Move the servo to the specified degree
  }
  else if (String(topic) == "lights/neopixel") {
    int red, green, blue;
    sscanf(data.c_str(), "%d,%d,%d", &red, &green, &blue); // Parse the received data into RGB values
    Serial.print("Setting NeoPixel color to (R,G,B): ");
    Serial.print(red);
    Serial.print(",");
    Serial.print(green);
    Serial.print(",");
    Serial.println(blue);
    fill_solid(leds, NUM_LEDS, CRGB(red, green, blue));
    FastLED.show();
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

  unsigned long currentMillis = millis(); // Read current time
  if (currentMillis - previousMillis >= interval) { // If current time - last time > 5 sec
    previousMillis = currentMillis;
    
    // Read sensor data
    sensors_event_t temp_event, hum_event;
    dht.temperature().getEvent(&temp_event);
    dht.humidity().getEvent(&hum_event);
    
    float temp = isnan(temp_event.temperature) ? -999.0 : temp_event.temperature;
    float hum = isnan(hum_event.relative_humidity) ? -999.0 : hum_event.relative_humidity;
    int lightValue = analogRead(LDR_PIN);
    int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN); // Đọc giá trị từ chân 35

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

    // Combine and publish data
    String payload = String(temp) + "," + String(hum) + "," + String(lightValue) + "," + String(soilMoistureValue);
    
    if (client.publish(topic, payload.c_str())) {
      Serial.println("Data published successfully!");
    } else {
      Serial.println("Failed to publish data.");
    }
  }
}
