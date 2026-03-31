#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <math.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "u660b616.ala.eu-central-1.emqxsl.com";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "helmet123";
const char* MQTT_PASSWORD = "helmet123";
const char* MQTT_CLIENT_ID = "sand76";
const char* MQTT_TOPIC = "bms/live";

const int BUTTON_PIN = 13;
const int BATTERY_ADC_PIN = 32;
const int BUZZER_PIN = 23;
const int DHT_PIN = 4;
const int DHT_TYPE = DHT11;
const int I2C_SDA_PIN = 21;
const int I2C_SCL_PIN = 22;

const float R1 = 100000.0f;
const float R2 = 22000.0f;
const float ADC_VREF = 3.3f;
const float ADC_MAX = 4095.0f;
const float TEMP_ALERT_C = 35.0f;

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
Adafruit_INA219 ina219;
LiquidCrystal_I2C lcd(0x27, 20, 4);
DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastSensorMillis = 0;
unsigned long lastDhtMillis = 0;
unsigned long lastMqttMillis = 0;
unsigned long lastDisplayMillis = 0;
unsigned long lastButtonMillis = 0;
unsigned long lastBeepMillis = 0;
unsigned long lastScreenChangeMillis = 0;

bool lastButtonState = HIGH;
int screenIndex = 0;
const unsigned long AUTO_SCREEN_MS = 7000;

float packVoltage = 0.0f;
float busVoltage = 0.0f;
float currentA = 0.0f;
float powerW = 0.0f;
float temperatureC = 30.0f;
float humidity = 50.0f;
float soc = 0.0f;
float soh = 96.0f;
bool tempAlert = false;

char line0[21] = "";
char line1[21] = "";
char line2[21] = "";
char line3[21] = "";

float clampValue(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void writeCachedLine(uint8_t row, char* cache, const char* text) {
  char padded[21];
  snprintf(padded, sizeof(padded), "%-20.20s", text);
  if (strcmp(cache, padded) != 0) {
    lcd.setCursor(0, row);
    lcd.print(padded);
    strcpy(cache, padded);
  }
}

void clearDisplayCache() {
  line0[0] = '\0';
  line1[0] = '\0';
  line2[0] = '\0';
  line3[0] = '\0';
}

float readBatteryVoltage() {
  int raw = analogRead(BATTERY_ADC_PIN);
  float adcVoltage = (raw / ADC_MAX) * ADC_VREF;
  return adcVoltage * ((R1 + R2) / R2);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
    if (!mqttClient.connected()) {
      delay(2000);
    }
  }
}

void readFastSensors() {
  busVoltage = ina219.getBusVoltage_V();
  currentA = ina219.getCurrent_mA() / 1000.0f;
  packVoltage = readBatteryVoltage();
  powerW = packVoltage * currentA;
  soc = clampValue(((packVoltage - 3.0f) / 1.2f) * 100.0f, 0.0f, 100.0f);
  soh = clampValue(96.0f - (temperatureC > 35.0f ? 2.0f : 0.0f) + sin(millis() / 20000.0f), 90.0f, 99.0f);
}

void readSlowSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperatureC = t;
  if (!isnan(h)) humidity = h;
  tempAlert = temperatureC >= TEMP_ALERT_C;
}

void handleButton() {
  bool state = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && state == LOW && millis() - lastButtonMillis > 300) {
    screenIndex = (screenIndex + 1) % 3;
    lastButtonMillis = millis();
    lastScreenChangeMillis = millis();
    lcd.clear();
    clearDisplayCache();
  }
  lastButtonState = state;
}

void autoChangeScreen() {
  if (millis() - lastScreenChangeMillis >= AUTO_SCREEN_MS) {
    screenIndex = (screenIndex + 1) % 3;
    lastScreenChangeMillis = millis();
    lcd.clear();
    clearDisplayCache();
  }
}

void updateBuzzer() {
  if (tempAlert) {
    if (millis() - lastBeepMillis > 700) {
      tone(BUZZER_PIN, 2800, 180);
      lastBeepMillis = millis();
    }
  } else {
    noTone(BUZZER_PIN);
  }
}

void updateDisplay() {
  char temp[32];

  if (screenIndex == 0) {
    snprintf(temp, sizeof(temp), "Volt: %.2f V", packVoltage);
    writeCachedLine(0, line0, temp);
    snprintf(temp, sizeof(temp), "Curr: %.2f A", currentA);
    writeCachedLine(1, line1, temp);
    snprintf(temp, sizeof(temp), "Power: %.2f W", powerW);
    writeCachedLine(2, line2, temp);
    writeCachedLine(3, line3, "Push Btn Next");
  } else if (screenIndex == 1) {
    snprintf(temp, sizeof(temp), "Temp: %.1f C", temperatureC);
    writeCachedLine(0, line0, temp);
    snprintf(temp, sizeof(temp), "SOC: %.0f %%", soc);
    writeCachedLine(1, line1, temp);
    snprintf(temp, sizeof(temp), "SOH: %.0f %%", soh);
    writeCachedLine(2, line2, temp);
    writeCachedLine(3, line3, tempAlert ? "Alert High Temp" : "Status Normal");
  } else {
    writeCachedLine(0, line0, WiFi.status() == WL_CONNECTED ? "WiFi Connected" : "WiFi Offline");
    writeCachedLine(1, line1, mqttClient.connected() ? "MQTT Connected" : "MQTT Offline");
    snprintf(temp, sizeof(temp), "Hum: %.0f %%", humidity);
    writeCachedLine(2, line2, temp);
    writeCachedLine(3, line3, "BMS Live");
  }
}

void publishData() {
  char payload[300];
  snprintf(
    payload,
    sizeof(payload),
    "{\"packVoltage\":%.2f,\"current\":%.2f,\"power\":%.2f,\"temperature\":%.1f,"
    "\"humidity\":%.1f,\"soc\":%.0f,\"soh\":%.0f,\"mode\":\"%s\",\"alert\":%s}",
    packVoltage,
    currentA,
    powerW,
    temperatureC,
    humidity,
    soc,
    soh,
    currentA > 0.08f ? "Charging" : (currentA < -0.08f ? "Discharging" : "Idle"),
    tempAlert ? "true" : "false"
  );
  mqttClient.publish(MQTT_TOPIC, payload);
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();
  clearDisplayCache();

  dht.begin();
  ina219.begin();

  secureClient.setInsecure();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(512);

  connectWiFi();
  connectMQTT();

  readFastSensors();
  readSlowSensors();
  updateDisplay();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if (!mqttClient.connected()) {
    connectMQTT();
  }

  mqttClient.loop();
  handleButton();
  autoChangeScreen();
  updateBuzzer();

  unsigned long now = millis();

  if (now - lastSensorMillis >= 300) {
    readFastSensors();
    lastSensorMillis = now;
  }

  if (now - lastDhtMillis >= 2000) {
    readSlowSensors();
    lastDhtMillis = now;
  }

  if (now - lastDisplayMillis >= 500) {
    updateDisplay();
    lastDisplayMillis = now;
  }

  if (now - lastMqttMillis >= 350) {
    publishData();
    lastMqttMillis = now;
  }

  delay(10);
}
