#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ThingSpeak.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <ESP32Servo.h>
#include "DHT.h"

/* ===== PINS ===== */
#define DHTPIN 4
#define DHTTYPE DHT22
#define LDR_PIN 34
#define SOIL_PIN 35
#define RING_PIN 25
#define RING_PIX 16
#define SERVO_PIN 26

/* ===== Thresholds / timing ===== */
const int   LIGHT_ON  = 40;
const int   LIGHT_OFF = 50;
const int   SOIL_ON   = 35;
const int   SOIL_OFF  = 45;
const unsigned long PUMP_MIN_ON   = 5000;
const unsigned long PUMP_COOLDOWN = 10000;

/* ===== WiFi / MQTT / TS ===== */
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

#define MQTT_HOST   "broker.emqx.io"   // hoặc IP Mosquitto nội bộ
#define MQTT_PORT   1883
#define MQTT_CLIENT "esp32-garden-01"

const char* TOPIC_TEMP   = "farm/temp";
const char* TOPIC_HUM    = "farm/hum";
const char* TOPIC_LIGHT  = "farm/light";
const char* TOPIC_SOIL   = "farm/soil";

const char* T_CMD_MODE   = "farm/cmd/mode";
const char* T_CMD_LAMP   = "farm/cmd/lamp";
const char* T_CMD_BRIGHT = "farm/cmd/lamp/bright";
const char* T_CMD_COLOR  = "farm/cmd/lamp/color";
const char* T_CMD_PUMP   = "farm/cmd/pump";

const char* T_ST_MODE    = "farm/status/mode";
const char* T_ST_LAMP    = "farm/status/lamp";
const char* T_ST_BRIGHT  = "farm/status/bright";
const char* T_ST_COLOR   = "farm/status/lamp/color";
const char* T_ST_PUMP    = "farm/status/pump";

WiFiClient   net;
PubSubClient mqtt(net);

WiFiClient   tsClient;
unsigned long myChannelNumber = 3064394;
const char* myWriteAPIKey = "Q0C1U02B034TQZ1I";
unsigned long previousMillis = 0;
const long ts_update_interval = 15000;

/* ===== OLED ===== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

/* ===== NeoPixel + Servo ===== */
Adafruit_NeoPixel ring(RING_PIX, RING_PIN, NEO_GRB + NEO_KHZ800);
Servo pumpServo;

/* ===== Filters/calib ===== */
const int   LDR_MIN_RAW=0, LDR_MAX_RAW=3500;
const float EMA_ALPHA=0.15f; const int MEDIAN_SAMPLES=7; const int RAW_DEADBAND=4;
const bool  INVERT_LIGHT=true;
const int   SOIL_WET_RAW=0, SOIL_DRY_RAW=4095;
const float SOIL_EMA_ALPHA=0.15f; const int SOIL_MEDIAN_N=7; const int SOIL_DEADBAND=6;
const bool  SOIL_INVERT=false;

/* ===== Globals ===== */
DHT dht(DHTPIN, DHTTYPE);
float hum=0, temp=0, hic=0, lightPct=0, soilPct=0;

bool autoMode = true;
bool lampOn=false, pumpOn=false;
uint8_t lampBright = 120;               // 0..255
uint32_t lampColor = 0xFFB43C;         // 0xRRGGBB (mặc định vàng ấm)
unsigned long pumpTs=0;                // thời điểm gần nhất bật bơm
unsigned long pumpManualUntil=0;       // nếu >0: đang tưới theo lệnh manual đến mốc thời gian này

/* ---------- Icons rút gọn ---------- */
const unsigned char icon_temp16[] PROGMEM = {0x06,0,0x06,0,0x06,0,0x06,0,0x06,0,0x06,0,0x06,0,0x06,0,0x0F,0,0x1F,0x80,0x3F,0xC0,0x3F,0xC0,0x3F,0xC0,0x1F,0x80,0x0F,0,0x06,0};
const unsigned char icon_humid16[] PROGMEM={0x03,0,0x07,0x80,0x0F,0xC0,0x1F,0xE0,0x1F,0xE0,0x3F,0xF0,0x3F,0xF0,0x3F,0xF0,0x1F,0xE0,0x1F,0xE0,0x0F,0xC0,0x07,0x80,0x03,0,0x03,0,0x01,0,0,0};
const unsigned char icon_light16[] PROGMEM={0x01,0,0x03,0x80,0x07,0xC0,0x0C,0x60,0x10,0x10,0x10,0x10,0x20,0x08,0x21,0x08,0x20,0x08,0x10,0x10,0x10,0x10,0x0C,0x60,0x07,0xC0,0x03,0x80,0x01,0,0,0};
const unsigned char icon_soil16[]  PROGMEM={0,0,0x01,0,0x03,0x80,0x06,0xC0,0x03,0x80,0x01,0,0,0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0,0,0,0,0,0,0,0,0,0,0,0};

/* ===== Utils ===== */
template<int N> int medianRead(int pin){
  int a[N]; for(int i=0;i<N;i++){ a[i]=analogRead(pin); delay(1); }
  for(int i=0;i<N-1;i++) for(int j=i+1;j<N;j++) if(a[j]<a[i]){ int t=a[i]; a[i]=a[j]; a[j]=t; }
  return a[N/2];
}
static inline float mapFloat(float x,float in_min,float in_max,float out_min,float out_max){
  return (x-in_min)*(out_max-out_min)/(in_max-in_min)+out_min;
}

/* ===== Actuators ===== */
void applyLamp(){
  ring.setBrightness(lampBright);
  if(lampOn){ ring.fill(lampColor); }
  else      { ring.clear(); }
  ring.show();
}
void lampSet(bool on){ lampOn=on; applyLamp(); }

void pumpStart(){ pumpOn=true; pumpTs=millis(); pumpServo.write(90); }
void pumpStop (){ pumpOn=false;              pumpServo.write(0);  }

/* ===== Publish status ===== */
void pub(const char* t, const String& s){ mqtt.publish(t, s.c_str(), true); }
void publishAllStatus(){
  pub(T_ST_MODE,   autoMode? "AUTO":"MANUAL");
  pub(T_ST_LAMP,   lampOn? "ON":"OFF");
  pub(T_ST_BRIGHT, String(lampBright));
  char hex[8]; sprintf(hex,"#%06X", (unsigned)lampColor); pub(T_ST_COLOR, String(hex));
  pub(T_ST_PUMP,   pumpOn? "ON":"OFF");
}

/* ===== Helpers ===== */
uint32_t parseHexColor(String s){
  s.trim(); s.toUpperCase();
  if(s.startsWith("#")) s.remove(0,1);
  if(s.length()!=6) return lampColor;
  char *end=nullptr;
  uint32_t v = strtoul(s.c_str(), &end, 16);
  return v;
}

/* ===== MQTT callback ===== */
void onMqtt(char* topic, byte* payload, unsigned int len){
  String top = String(topic);
  String msg; msg.reserve(len);
  for(unsigned int i=0;i<len;i++) msg += (char)payload[i];
  msg.trim();

  if(top == T_CMD_MODE){
    autoMode = (msg.equalsIgnoreCase("AUTO"));
    pub(T_ST_MODE, autoMode? "AUTO":"MANUAL");
    return;
  }
  if(top == T_CMD_LAMP){
    if(msg.equalsIgnoreCase("ON"))  lampSet(true);
    else                            lampSet(false);
    pub(T_ST_LAMP, lampOn? "ON":"OFF");
    return;
  }
  if(top == T_CMD_BRIGHT){
    int v = constrain(msg.toInt(), 0, 255);
    lampBright = (uint8_t)v;
    applyLamp();
    pub(T_ST_BRIGHT, String(lampBright));
    return;
  }
  if(top == T_CMD_COLOR){
    lampColor = parseHexColor(msg);
    applyLamp();
    char hex[8]; sprintf(hex,"#%06X", (unsigned)lampColor);
    pub(T_ST_COLOR, String(hex));
    return;
  }
  if(top == T_CMD_PUMP){
    if(msg.startsWith("ON")){
      // dạng "ON:10000"
      int idx = msg.indexOf(':');
      unsigned long dur = (idx>0)? msg.substring(idx+1).toInt() : 10000;
      if(dur<2000) dur=2000; // an toàn
      if((millis()-pumpTs) > PUMP_COOLDOWN){     // tôn trọng cooldown
        pumpManualUntil = millis() + dur;
        if(!pumpOn) pumpStart();
        pub(T_ST_PUMP, "ON");
      }
    }else{ // OFF
      pumpManualUntil = 0;
      if(pumpOn && (millis()-pumpTs)>=PUMP_MIN_ON){
        pumpStop();
        pub(T_ST_PUMP, "OFF");
      }
    }
    return;
  }
}

/* ===== MQTT connect ===== */
void mqttEnsure(){
  if(mqtt.connected()) return;
  while(!mqtt.connected()){
    mqtt.connect(MQTT_CLIENT);
    if(!mqtt.connected()) delay(500);
  }
  mqtt.subscribe("farm/cmd/#");
  publishAllStatus();
}

/* ===== Setup / Loop ===== */
void setup(){
  Serial.begin(115200);
  dht.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN,  ADC_11db);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
  display.setCursor(10,24); display.println(F("Garden (MQTT + Auto)")); display.display();

  ring.begin(); ring.clear(); ring.setBrightness(lampBright); ring.show();
  pumpServo.setPeriodHertz(50);
  pumpServo.attach(SERVO_PIN, 500, 2400);
  pumpStop();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(WiFi.status()!=WL_CONNECTED) delay(300);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);

  ThingSpeak.begin(tsClient);
}

void loop(){
  mqttEnsure();
  mqtt.loop();

  // ---- LDR ----
  static float emaRaw=-1; static int lastStable=-1;
  int med = medianRead<MEDIAN_SAMPLES>(LDR_PIN);
  if(emaRaw<0) emaRaw=med;
  emaRaw = EMA_ALPHA*med + (1.0f-EMA_ALPHA)*emaRaw;
  int raw = lroundf(emaRaw);
  if(lastStable>=0 && abs(raw-lastStable)<=RAW_DEADBAND) raw=lastStable; else lastStable=raw;
  float pct = mapFloat(raw, LDR_MIN_RAW, LDR_MAX_RAW, 0.0f, 100.0f);
  lightPct = constrain(INVERT_LIGHT ? 100.0f - pct : pct, 0.0f, 100.0f);

  // ---- DHT ----
  hum = dht.readHumidity();
  temp = dht.readTemperature();
  if(isnan(hum) || isnan(temp)) return;
  hic = dht.computeHeatIndex(temp, hum, false);

  // ---- Soil ----
  int soilMed = medianRead<SOIL_MEDIAN_N>(SOIL_PIN);
  static float soilEma = soilMed;
  soilEma = SOIL_EMA_ALPHA*soilMed + (1.0f-SOIL_EMA_ALPHA)*soilEma;
  int soilRaw = lroundf(soilEma);
  static int soilLast=soilRaw;
  if(abs(soilRaw-soilLast)<=SOIL_DEADBAND) soilRaw=soilLast; else soilLast=soilRaw;
  float sp = (float)(soilRaw - SOIL_WET_RAW) * 100.0f / (float)(SOIL_DRY_RAW - SOIL_WET_RAW);
  soilPct = constrain(SOIL_INVERT ? 100.0f - sp : sp, 0.0f, 100.0f);

  // ---- Điều khiển ----
  unsigned long now = millis();

  // manual pump (được ưu tiên)
  if(pumpManualUntil>0){
    if(!pumpOn) pumpStart();
    if(now >= pumpManualUntil && (now - pumpTs) >= PUMP_MIN_ON){
      pumpManualUntil = 0;
      pumpStop();
      pub(T_ST_PUMP, "OFF");
    }
  }else if(autoMode){
    // Auto lamp
    if(!lampOn && lightPct < LIGHT_ON)      { lampSet(true);  pub(T_ST_LAMP,"ON"); }
    else if(lampOn && lightPct > LIGHT_OFF) { lampSet(false); pub(T_ST_LAMP,"OFF"); }
    // Auto pump
    if(!pumpOn && soilPct < SOIL_ON && (now - pumpTs) > PUMP_COOLDOWN){
      pumpStart(); pub(T_ST_PUMP,"ON");
    }
    if(pumpOn && soilPct > SOIL_OFF && (now - pumpTs) > PUMP_MIN_ON){
      pumpStop();  pub(T_ST_PUMP,"OFF");
    }
  }

  // ---- OLED ----
  display.clearDisplay(); display.setTextSize(1);
  display.drawBitmap(0, 0,  icon_temp16, 16,16,WHITE); display.setCursor(20, 4);  display.print("T: ");  display.print(temp,1);  display.print(" C");
  display.drawBitmap(0,16,  icon_humid16,16,16,WHITE); display.setCursor(20,20);  display.print("H: ");  display.print(hum,0);   display.print(" %");
  display.drawBitmap(0,32,  icon_light16,16,16,WHITE); display.setCursor(20,36);  display.print("L: ");  display.print((int)lightPct); display.print(" %");
  display.drawBitmap(0,48,  icon_soil16, 16,16,WHITE); display.setCursor(20,52);  display.print("S: ");  display.print((int)soilPct);  display.print(" %");
  display.setCursor(92,36); display.print(lampOn ? "LAMP":"    ");
  display.setCursor(92,52); display.print(pumpOn ? "PUMP":"    ");
  display.display();

  // ---- Publish sensor mỗi 15s ----
  if(millis() - previousMillis >= ts_update_interval){
    previousMillis = millis();
    char buf[16];
    dtostrf(temp,     0, 1, buf); mqtt.publish(TOPIC_TEMP,  buf, true);
    dtostrf(hum,      0, 0, buf); mqtt.publish(TOPIC_HUM,   buf, true);
    dtostrf(lightPct, 0, 0, buf); mqtt.publish(TOPIC_LIGHT, buf, true);
    dtostrf(soilPct,  0, 0, buf); mqtt.publish(TOPIC_SOIL,  buf, true);
    ThingSpeak.setField(1, hum);
    ThingSpeak.setField(2, temp);
    ThingSpeak.setField(3, hic);
    ThingSpeak.setField(4, lightPct);
    ThingSpeak.setField(5, soilPct);
    ThingSpeak.setField(6, lampOn ? 1 : 0);
    ThingSpeak.setField(7, pumpOn ? 1 : 0);
    ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  }
}
