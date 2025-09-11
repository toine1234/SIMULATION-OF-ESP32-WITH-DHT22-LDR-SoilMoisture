#include <Adafruit_Sensor.h>
#include <DHT_U.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <FastLED.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


// define chân kết nối cảm biến và các thiết bị khác
#define DHTPIN 12
#define LED 26
#define SERVO_PIN 2
#define LED_PIN 4
#define NUM_LEDS 16
#define DHTTYPE DHT22
#define LDR_PIN 34
#define SOIL_MOISTURE_PIN 35

// màn hình OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

//--------------------- tạo icon cho màn hình OLED --------------------- 
// 16x16 - Nhiệt độ (nhiệt kế)
const unsigned char PROGMEM icon_temp16[] = {
  0x06,0x00, 0x06,0x00, 0x06,0x00, 0x06,0x00,
  0x06,0x00, 0x06,0x00, 0x06,0x00, 0x06,0x00,
  0x0F,0x00, 0x1F,0x80, 0x3F,0xC0, 0x3F,0xC0,
  0x3F,0xC0, 0x1F,0x80, 0x0F,0x00, 0x06,0x00
};

// 16x16 - Độ ẩm (giọt nước)
const unsigned char PROGMEM icon_humid16[] = {
  0x03,0x00, 0x07,0x80, 0x0F,0xC0, 0x1F,0xE0,
  0x1F,0xE0, 0x3F,0xF0, 0x3F,0xF0, 0x3F,0xF0,
  0x1F,0xE0, 0x1F,0xE0, 0x0F,0xC0, 0x07,0x80,
  0x03,0x00, 0x03,0x00, 0x01,0x00, 0x00,0x00
};

// 16x16 - Ánh sáng (mặt trời + tia)
const unsigned char PROGMEM icon_light16[] = {
  0x01,0x00, 0x03,0x80, 0x07,0xC0, 0x0C,0x60,
  0x10,0x10, 0x10,0x10, 0x20,0x08, 0x21,0x08,
  0x20,0x08, 0x10,0x10, 0x10,0x10, 0x0C,0x60,
  0x07,0xC0, 0x03,0x80, 0x01,0x00, 0x00,0x00
};

// 16x16 - Độ ẩm đất (mầm cây + nền đất)
const unsigned char PROGMEM icon_soil16[] = {
  0x00,0x00, 0x01,0x00, 0x03,0x80, 0x06,0xC0,
  0x03,0x80, 0x01,0x00, 0x00,0x00, 0xFF,0xFF,
  0xFF,0xFF, 0xFF,0xFF, 0x00,0x00, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00
};


const unsigned char wifi_icon [] PROGMEM = {
  0x00,0x00,0x00,0x07,0xe0,0x1f,0xf8,0x7f,0xfc,0xff,0xfe,0x7f,0xfc,0x1f,0xf8,0x07,
  0xe0,0x00,0x00,0x00
};

const unsigned char wifi_dc [] PROGMEM = {
  0x00,0x00,0x00,0x03,0xc0,0x0f,0xf0,0x3f,0xf8,0x7f,0xfc,0x3f,0xf8,0x0f,0xf0,0x03,
  0xc0,0x00,0x00,0x00
};

const unsigned char Update_OK [] PROGMEM = {
  0x01,0x80,0x06,0xf0,0x10,0x18,0x20,0x20,0x40,0x40,0x32,0x0c,0x62,0x06,0x84,0x03,
  0x83,0x03,0x41,0x82,0x40,0x82,0x20,0x84,0x10,0x88,0x08,0x90,0x07,0xe0,0x01,0x80
};

const unsigned char Update_NOK [] PROGMEM = {
  0x01,0x80,0x06,0x70,0x08,0x08,0x10,0x10,0x20,0x20,0x44,0x44,0x82,0x82,0x82,0x82,
  0x82,0x82,0x44,0x44,0x20,0x20,0x10,0x10,0x08,0x08,0x06,0x70,0x01,0x80,0x00,0x00
};
// 
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

// các topic lấy dữ liệu
const char* autoLightTopic = "signal/auto_light";
const char* autoWateringTopic = "signal/auto_watering";
const char* SwitchLight = "signal/switch_light";
const char* LightColor = "signal/light_color";

// Parameters for non-blocking delay
unsigned long previousMillis = 0;
const long interval = 5000;

// Setting up WiFi and MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

// --------------------- Hàm kết nối WiFi -----------------
void setup_wifi() {
  int a = 0, i=0 ;
  Serial.println("Connecting to WiFi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    display.clearDisplay();
    display.setTextSize(1);
    if (a == 0) { display.drawBitmap(52, 15, wifi_icon, 16, 16, WHITE); a = 1; }
    else { display.drawBitmap(52, 15, wifi_icon, 16, 16, BLACK); a = 0; }
    display.setCursor(25, 38); display.print("Connecting ...");
    display.display();
    delay(1000);
  }

  display.clearDisplay();
  display.setTextSize(1);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n Connected!");
      display.drawBitmap(52, 15, wifi_icon, 16, 16, WHITE);
      display.setCursor(33, 38); display.print("Connected!");
    } else {
      display.drawBitmap(52, 15, wifi_dc, 16, 16, WHITE);
      display.setCursor(20, 38); display.print("Wifi Connection");
      display.setCursor(36, 48); display.print("Failed!");
    }
  display.display();
  delay(1200);
  
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
      client.subscribe(autoLightTopic);
      client.subscribe(autoWateringTopic);
      client.subscribe(SwitchLight);
      client.subscribe(LightColor);
      Serial.println("Topic Subscribed");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// --- khai báo biến toàn cục ---
bool autoLightOn = false;
bool autoWateringOn = false;
char switchLightState = false;
char lightColor[10] = "white";

// --- callback nhận dữ liệu ---
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Nhận từ topic: ");
  Serial.println(topic);
  Serial.print("Nội dung: ");
  Serial.println(message);

  if (String(topic) == autoLightTopic) {
    Serial.println(message);
    if (message == "true") {
      autoLightOn = true;
    } else if (message == "false") {
      autoLightOn = false;
    }
  }

  if (String(topic) == autoWateringTopic) {
    if (message == "true") {
      autoWateringOn = true;
    } else if (message == "false") {
      autoWateringOn = false;
    }
  }

  if (String(topic) == SwitchLight) {
    if (message == "true") {
      switchLightState = true;
      Serial.println("LED turned ON via MQTT");
    } else if (message == "false") {
      switchLightState = false;
      Serial.println("LED turned OFF via MQTT");
    }
  }

  if (String(topic) == LightColor) {
    message.toCharArray(lightColor, sizeof(lightColor));
  }
}



// --------------------- Hàm setup (cấu hình ban đầu để hoạt động) -----------------
void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize sensors and components
  dht.begin();
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // Setup servo
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(90);

  // Setup WS2812 LED strip
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);

  // Setup OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 20); display.println(F("Hellooo"));
  display.setCursor(10, 35); display.println(F("DHT22 + LDR + Soil Moisture"));
  display.display();
  delay(1200);

  // Setup WiFi and MQTT
  setup_wifi();
  client.setServer(mqttServer, 1883);
  client.setCallback(callback);
}


// --------------------- Hàm loop (hàm hoạt động hiển thị và lấy dữ liệu) -----------------
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
    
    // Nhiệt độ 
    float temp = isnan(temp_event.temperature) ? -999.0 : temp_event.temperature;
    float hum = isnan(hum_event.relative_humidity) ? -999.0 : hum_event.relative_humidity;
    int lightValue = analogRead(LDR_PIN);
    int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);

    // Độ ẩm
    int soilMin = 1000;
    int soilMax = 4000;
    int soilPercent =( (float)(soilMoistureValue - soilMin) / (soilMax - soilMin) ) * 100;
    if (soilPercent < 0) soilPercent = 0;
    if (soilPercent > 100) soilPercent = 100;

    // Ánh sáng
    int lightMin = 0;
    int lightMax = 4095;
    int lightPercent = 100 - ( (float)(lightValue - lightMin) / (lightMax - lightMin) ) * 100;
    if(lightPercent < 0) {
      lightPercent = 0;
    }
    if(lightPercent > 100) {
      lightPercent = 100;
    }

    //----------In giá trị ra terminal---------------
    Serial.printf("Temperature: %.2f C\r\n", temp);
    Serial.printf("Humidity: %.2f %%\r\n", hum);
    Serial.printf("LDR Value: %d%%\r\n", lightPercent);
    Serial.printf("Soil Moisture Value: %d%%\r\n", soilPercent);

    //------------Gửi dữ liệu lên MQTT với các topic riêng biệt-----------
    client.publish(tempTopic, String(temp).c_str());
    client.publish(humTopic, String(hum).c_str());
    client.publish(lightTopic, String(lightPercent).c_str());
    client.publish(soilTopic, String(soilPercent).c_str());

    //--------Hiển thị dữ liệu lên màn hình----------
    display.clearDisplay();
    display.setTextSize(1);

    // Temp
    display.drawBitmap(0, 0,  icon_temp16,  16, 16, WHITE); display.setCursor(20, 4); display.print("Temp: "); display.print(temp, 1); display.print(" C");

    // Humidity
    display.drawBitmap(0, 16, icon_humid16, 16, 16, WHITE); display.setCursor(20, 20); display.print("H: "); display.print(hum, 0); display.print(" %");

    // Light
    display.drawBitmap(0, 32, icon_light16, 16, 16, WHITE); display.setCursor(20, 36); display.print("Light: "); display.print((int)roundf(lightPercent)); display.print(" %");

    // Soil moisture
    display.drawBitmap(0, 48, icon_soil16, 16, 16, WHITE); display.setCursor(20, 52); display.print("Soil: "); display.print((int)roundf(soilPercent)); display.print(" %");
    
    // WiFi status
    if (WiFi.status() != WL_CONNECTED) {
      display.drawBitmap(110, 0, wifi_dc, 16, 16, WHITE);
    } else {
      display.drawBitmap(110, 0, wifi_icon, 16, 16, WHITE);
    }
    display.display();
    delay(100);

    //------------kiểm tra auto light-----------------------
      if (autoLightOn) {
        Serial.println("Auto Light ON - Turning ON LED.");
    // Điều khiển màu sắc của dải LED WS2812 dựa trên mức độ ánh sáng
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
      } else {
        Serial.println("Auto Light OFF - Turning OFF LED.");
        // Bật tắt đèn LED theo lệnh từ MQTT
        if(switchLightState) {
          digitalWrite(LED, HIGH);
          Serial.println(lightColor);
          if(strcmp(lightColor, "Red") == 0) {
            fill_solid(leds, NUM_LEDS, CRGB::Red);
            Serial.println("LED color set to Red via MQTT");
          } else if(strcmp(lightColor, "Yellow") == 0) {
            fill_solid(leds, NUM_LEDS, CRGB::Yellow);
            Serial.println("LED color set to Yellow via MQTT");
          } else if(strcmp(lightColor, "Blue") == 0) {
            fill_solid(leds, NUM_LEDS, CRGB::Blue);
            Serial.println("LED color set to Blue via MQTT");
          } else {
            fill_solid(leds, NUM_LEDS, CRGB::White);
            Serial.println("LED color set to White (default)");
          }
        } else {
          digitalWrite(LED, LOW);
          fill_solid(leds, NUM_LEDS, CRGB::Black);
        }
        FastLED.show();

      }

    //------------Phân loại độ ẩm đất-----------------
    if (soilPercent < 30) {
      Serial.println("Soil is Dry. Activating automatic watering and turning ON LED.");
      digitalWrite(LED, HIGH);
      servo.write(0);
      servo.write(90);
    } else {
      Serial.println("Soil is Moist/Wet - Turning OFF LED.");
      servo.write(90);
      digitalWrite(LED, LOW);
    }

    Serial.println("Data published successfully to separate topics.");
    Serial.println("-----------------------------");

    // Cập nhật hiển thị OLED
    display.clearDisplay();
    display.setTextSize(1);

  }
}