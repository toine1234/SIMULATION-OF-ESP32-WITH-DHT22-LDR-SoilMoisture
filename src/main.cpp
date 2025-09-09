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
Servo servo;
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

// Hàm kết nối lại
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

    int soilMin = 1000;
    int soilMax = 4000;
    int soilPercent =( (float)(soilMoistureValue - soilMin) / (soilMax - soilMin) ) * 100;
    if (soilPercent < 0) soilPercent = 0;
    if (soilPercent > 100) soilPercent = 100;

    int lightMin = 0;
    int lightMax = 4095;
    int lightPercent = 100 - ( (float)(lightValue - lightMin) / (lightMax - lightMin) ) * 100;
    if(lightPercent < 0) {
      lightPercent = 0;
    }
    if(lightPercent > 100) {
      lightPercent = 100;
    }

    // Phân loại độ ẩm đất và điều khiển đèn LED
    if (soilPercent > 70) {
      Serial.println("Soil is Dry. Activating automatic watering and turning ON LED.");
      digitalWrite(LED, HIGH);
      servo.write(90);
      delay(5000);
      servo.write(0);
    } else {
      Serial.println("Soil is Moist/Wet - Turning OFF LED.");
      digitalWrite(LED, LOW);
    }

    CRGB color;
    if(lightPercent > 80){
      Serial.println("High Light - NeoPixel color : White");
      color = CRGB::White;
    }
    else if (lightPercent > 40){
      color = CRGB::Yellow;
    }else{
      Serial.println("Low Light - NeoPixel color : Blue");
      color = CRGB::Blue;
    }
    fill_solid(leds, NUM_LEDS, color);
    FastLED.show();

    // In giá trị ra Serial Monitor
    Serial.printf("Temperature: %.2f C\n", temp);
    Serial.printf("Humidity: %.2f %%\n", hum);
    Serial.printf("LDR Value: %d%%\n", lightPercent);
    Serial.printf("Soil Moisture Value: %d%%\n", soilPercent);

    // Gửi dữ liệu lên MQTT với các topic riêng biệt
    client.publish(tempTopic, String(temp).c_str());
    client.publish(humTopic, String(hum).c_str());
    client.publish(lightTopic, String(lightPercent).c_str());
    client.publish(soilTopic, String(soilPercent).c_str());

    Serial.println("Data published successfully to separate topics.");
  }
}