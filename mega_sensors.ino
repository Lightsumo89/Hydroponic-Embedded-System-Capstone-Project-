#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include "SHT1x.h"

#define ONE_WIRE_PIN    4    

#define SHT11_DATA_PIN  6    

#define SHT11_CLK_PIN   7    

#define TRIG_PIN        8    

#define ECHO_PIN        9    

#define RELAY_PUMP      22   

#define RELAY_LIGHTS    23   

#define RELAY_SPARE     24   

#define ADS_PH_CHAN      0   

#define ADS_EC_CHAN      1   

#define ADS_TURB_CHAN    2   

const float PH_SLOPE      = -5.70f;
const float PH_INTERCEPT  = 21.34f;

const float EC_SLOPE      = 0.00125f;
const float EC_INTERCEPT  = 0.0f;

const float TURB_SLOPE     = 0.073f;
const float TURB_INTERCEPT = -1120.0f;

const float RESERVOIR_DEPTH_CM = 30.0f;

const unsigned long READ_INTERVAL_MS = 30000;  

const int  LIGHTS_ON_HOUR   = 6;               

const int  LIGHTS_OFF_HOUR  = 22;              

const unsigned long PUMP_ON_MS  = 15UL * 60000UL;  

const unsigned long PUMP_OFF_MS = 45UL * 60000UL;  

OneWire           oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
SHT1x             sht11(SHT11_DATA_PIN, SHT11_CLK_PIN);
Adafruit_ADS1115  ads;
RTC_DS3231        rtc;

unsigned long lastReadTime   = 0;
unsigned long lastPumpToggle = 0;
bool pumpState   = false;
bool lightsState = false;

struct SensorData {
  float  waterTemp;
  float  airTemp;
  float  humidity;
  float  pH;
  float  ec;
  float  turbidity;
  float  waterLevelPct;
  String timestamp;
};

void setup() {
  Serial.begin(115200);   

  Serial1.begin(115200);  

  Serial.println("=== Hydroponic Mega Sensor Hub Starting ===");

  pinMode(RELAY_PUMP,   OUTPUT); digitalWrite(RELAY_PUMP,   HIGH);
  pinMode(RELAY_LIGHTS, OUTPUT); digitalWrite(RELAY_LIGHTS, HIGH);
  pinMode(RELAY_SPARE,  OUTPUT); digitalWrite(RELAY_SPARE,  HIGH);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Wire.begin();

  ds18b20.begin();
  Serial.println("[OK] DS18B20 ready");

  if (!ads.begin()) {
    Serial.println("[ERROR] ADS1115 not found — check wiring!");
  } else {
    ads.setGain(GAIN_ONE);  

    Serial.println("[OK] ADS1115 ready");
  }

  if (!rtc.begin()) {
    Serial.println("[ERROR] DS3231 not found!");
  } else {
    if (rtc.lostPower()) {
      Serial.println("[WARN] RTC reset to compile time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("[OK] DS3231 ready");
  }

  Serial.println("Setup complete. Starting sensor loop.\n");
}

void loop() {
  unsigned long now = millis();

  if (now - lastReadTime >= READ_INTERVAL_MS || lastReadTime == 0) {
    lastReadTime = now;

    SensorData data = readAllSensors();
    printSensorData(data);
    sendJSON(data);
    checkAlerts(data);
  }

  controlLights();
  controlPump(now);
}

SensorData readAllSensors() {
  SensorData d;

  ds18b20.requestTemperatures();
  float wt = ds18b20.getTempCByIndex(0);
  d.waterTemp = (wt == DEVICE_DISCONNECTED_C) ? -999.0f : wt;

  d.airTemp  = sht11.readTemperatureC();
  d.humidity = sht11.readHumidity();

  int16_t rawPH   = ads.readADC_SingleEnded(ADS_PH_CHAN);
  int16_t rawEC   = ads.readADC_SingleEnded(ADS_EC_CHAN);
  int16_t rawTurb = ads.readADC_SingleEnded(ADS_TURB_CHAN);

  float voltsPH = ads.computeVolts(rawPH);

  d.pH        = constrain(PH_SLOPE * voltsPH + PH_INTERCEPT, 0.0f, 14.0f);
  d.ec        = constrain(EC_SLOPE * rawEC   + EC_INTERCEPT, 0.0f, 10.0f);
  d.turbidity = constrain(TURB_SLOPE * rawTurb + TURB_INTERCEPT, 0.0f, 3000.0f);

  float distCm = readUltrasonic();
  if (distCm < 0) {
    d.waterLevelPct = -1.0f;
  } else {
    float waterHeight = RESERVOIR_DEPTH_CM - distCm;
    d.waterLevelPct = constrain((waterHeight / RESERVOIR_DEPTH_CM) * 100.0f, 0.0f, 100.0f);
  }

  DateTime now = rtc.now();
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  d.timestamp = String(buf);

  return d;
}

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  if (dur == 0) return -1.0f;
  return (dur * 0.0343f) / 2.0f;
}

void sendJSON(const SensorData& d) {
  StaticJsonDocument<512> doc;
  doc["temperature_water"] = round(d.waterTemp  * 10) / 10.0;
  doc["temperature_air"]   = round(d.airTemp    * 10) / 10.0;
  doc["humidity"]          = round(d.humidity   * 10) / 10.0;
  doc["ph"]                = round(d.pH         * 100) / 100.0;
  doc["ec"]                = round(d.ec         * 100) / 100.0;
  doc["turbidity"]         = round(d.turbidity);
  doc["water_level"]       = round(d.waterLevelPct);
  doc["pump_on"]           = pumpState;
  doc["lights_on"]         = lightsState;
  doc["timestamp"]         = d.timestamp;

  serializeJson(doc, Serial1);
  Serial1.println();  

  Serial.println("[TX] JSON sent to ESP32");
}

void checkAlerts(const SensorData& d) {
  if (d.pH > 0 && (d.pH < 5.5f || d.pH > 6.5f))
    Serial.printf("[ALERT] pH out of range: %.2f\n", d.pH);
  if (d.ec > 0 && (d.ec < 0.8f || d.ec > 3.5f))
    Serial.printf("[ALERT] EC out of range: %.2f mS/cm\n", d.ec);
  if (d.waterLevelPct >= 0 && d.waterLevelPct < 20.0f)
    Serial.printf("[ALERT] Water level low: %.0f%%\n", d.waterLevelPct);
  if (d.waterTemp > 0 && (d.waterTemp < 18.0f || d.waterTemp > 26.0f))
    Serial.printf("[ALERT] Water temp out of range: %.1f C\n", d.waterTemp);
  if (d.turbidity > 500.0f)
    Serial.printf("[ALERT] High turbidity: %.0f NTU\n", d.turbidity);
}

void controlLights() {
  DateTime now = rtc.now();
  bool shouldBeOn = (now.hour() >= LIGHTS_ON_HOUR && now.hour() < LIGHTS_OFF_HOUR);
  if (shouldBeOn != lightsState) {
    lightsState = shouldBeOn;
    digitalWrite(RELAY_LIGHTS, lightsState ? LOW : HIGH);
    Serial.println(lightsState ? "[ACT] Lights ON" : "[ACT] Lights OFF");
  }
}

void controlPump(unsigned long now) {
  unsigned long elapsed = now - lastPumpToggle;
  if (pumpState && elapsed >= PUMP_ON_MS) {
    pumpState = false;
    lastPumpToggle = now;
    digitalWrite(RELAY_PUMP, HIGH);
    Serial.println("[ACT] Pump OFF");
  } else if (!pumpState && elapsed >= PUMP_OFF_MS) {
    pumpState = true;
    lastPumpToggle = now;
    digitalWrite(RELAY_PUMP, LOW);
    Serial.println("[ACT] Pump ON");
  }
}

void printSensorData(const SensorData& d) {
  Serial.println("─── Sensor Readings @ " + d.timestamp + " ───");
  Serial.printf("  Water Temp : %.1f C\n",    d.waterTemp);
  Serial.printf("  Air Temp   : %.1f C\n",    d.airTemp);
  Serial.printf("  Humidity   : %.1f %%\n",   d.humidity);
  Serial.printf("  pH         : %.2f\n",      d.pH);
  Serial.printf("  EC         : %.2f mS/cm\n",d.ec);
  Serial.printf("  Turbidity  : %.0f NTU\n",  d.turbidity);
  Serial.printf("  Water Level: %.0f %%\n",   d.waterLevelPct);
}

