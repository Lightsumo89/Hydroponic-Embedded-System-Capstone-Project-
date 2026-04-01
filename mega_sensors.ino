/*
 * =============================================================
 *  Hydroponic IoT — Arduino Mega Sensor Hub
 *  Reads all sensors, builds JSON, sends to ESP32 via Serial1
 *
 *  Serial1 (TX1=Pin18, RX1=Pin19) → ESP32 RX/TX
 *  Baud: 115200
 * =============================================================
 */

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include "SHT1x.h"

// ----- Pin Definitions -----
#define ONE_WIRE_PIN    4    // DS18B20 data
#define SHT11_DATA_PIN  6    // SHT11 data
#define SHT11_CLK_PIN   7    // SHT11 clock
#define TRIG_PIN        8    // HC-SR04 trigger
#define ECHO_PIN        9    // HC-SR04 echo (5V — safe on Mega)
#define RELAY_PUMP      22   // Relay IN1 — pump
#define RELAY_LIGHTS    23   // Relay IN2 — lights
#define RELAY_SPARE     24   // Relay IN3 — spare

// ----- ADS1115 Channel Assignments -----
#define ADS_PH_CHAN      0   // A0 — pH analog output
#define ADS_EC_CHAN      1   // A1 — EC analog output
#define ADS_TURB_CHAN    2   // A2 — Turbidity analog output

// ----- Calibration Constants -----
// Calibrate pH with pH 4.0 and pH 7.0 buffer solutions
// slope = (7.0 - 4.0) / (V_at_7 - V_at_4)
// intercept = 7.0 - slope * V_at_7
const float PH_SLOPE      = -5.70f;
const float PH_INTERCEPT  = 21.34f;

// EC: calibrate with standard solution (e.g. 1.413 mS/cm)
// slope = known_EC / raw_ADC_count
const float EC_SLOPE      = 0.00125f;
const float EC_INTERCEPT  = 0.0f;

// Turbidity: calibrate empirically with clear water vs turbid sample
const float TURB_SLOPE     = 0.073f;
const float TURB_INTERCEPT = -1120.0f;

// Reservoir depth in cm (from sensor face to bottom when empty)
const float RESERVOIR_DEPTH_CM = 30.0f;

// ----- Timing -----
const unsigned long READ_INTERVAL_MS = 30000;  // 30s poll cycle
const int  LIGHTS_ON_HOUR   = 6;               // 06:00 lights on
const int  LIGHTS_OFF_HOUR  = 22;              // 22:00 lights off
const unsigned long PUMP_ON_MS  = 15UL * 60000UL;  // 15 min on
const unsigned long PUMP_OFF_MS = 45UL * 60000UL;  // 45 min off

// ----- Objects -----
OneWire           oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
SHT1x             sht11(SHT11_DATA_PIN, SHT11_CLK_PIN);
Adafruit_ADS1115  ads;
RTC_DS3231        rtc;

// ----- State -----
unsigned long lastReadTime   = 0;
unsigned long lastPumpToggle = 0;
bool pumpState   = false;
bool lightsState = false;

// =============================================================
//  SENSOR DATA STRUCT
// =============================================================
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

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);   // USB debug monitor
  Serial1.begin(115200);  // TX1(18)/RX1(19) → ESP32

  Serial.println("=== Hydroponic Mega Sensor Hub Starting ===");

  // Relay pins — HIGH = OFF (active-low relay module)
  pinMode(RELAY_PUMP,   OUTPUT); digitalWrite(RELAY_PUMP,   HIGH);
  pinMode(RELAY_LIGHTS, OUTPUT); digitalWrite(RELAY_LIGHTS, HIGH);
  pinMode(RELAY_SPARE,  OUTPUT); digitalWrite(RELAY_SPARE,  HIGH);

  // Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // I2C
  Wire.begin();

  // DS18B20
  ds18b20.begin();
  Serial.println("[OK] DS18B20 ready");

  // ADS1115
  if (!ads.begin()) {
    Serial.println("[ERROR] ADS1115 not found — check wiring!");
  } else {
    ads.setGain(GAIN_ONE);  // ±4.096V, 0.125mV/count
    Serial.println("[OK] ADS1115 ready");
  }

  // RTC
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

// =============================================================
//  LOOP
// =============================================================
void loop() {
  unsigned long now = millis();

  // Sensor read + serial send cycle
  if (now - lastReadTime >= READ_INTERVAL_MS || lastReadTime == 0) {
    lastReadTime = now;

    SensorData data = readAllSensors();
    printSensorData(data);
    sendJSON(data);
    checkAlerts(data);
  }

  // Actuator control runs every loop iteration (non-blocking)
  controlLights();
  controlPump(now);
}

// =============================================================
//  READ ALL SENSORS
// =============================================================
SensorData readAllSensors() {
  SensorData d;

  // --- DS18B20 water temperature ---
  ds18b20.requestTemperatures();
  float wt = ds18b20.getTempCByIndex(0);
  d.waterTemp = (wt == DEVICE_DISCONNECTED_C) ? -999.0f : wt;

  // --- SHT11 air temp + humidity ---
  d.airTemp  = sht11.readTemperatureC();
  d.humidity = sht11.readHumidity();

  // --- ADS1115 analog sensors ---
  int16_t rawPH   = ads.readADC_SingleEnded(ADS_PH_CHAN);
  int16_t rawEC   = ads.readADC_SingleEnded(ADS_EC_CHAN);
  int16_t rawTurb = ads.readADC_SingleEnded(ADS_TURB_CHAN);

  float voltsPH = ads.computeVolts(rawPH);

  d.pH        = constrain(PH_SLOPE * voltsPH + PH_INTERCEPT, 0.0f, 14.0f);
  d.ec        = constrain(EC_SLOPE * rawEC   + EC_INTERCEPT, 0.0f, 10.0f);
  d.turbidity = constrain(TURB_SLOPE * rawTurb + TURB_INTERCEPT, 0.0f, 3000.0f);

  // --- HC-SR04 water level ---
  float distCm = readUltrasonic();
  if (distCm < 0) {
    d.waterLevelPct = -1.0f;
  } else {
    float waterHeight = RESERVOIR_DEPTH_CM - distCm;
    d.waterLevelPct = constrain((waterHeight / RESERVOIR_DEPTH_CM) * 100.0f, 0.0f, 100.0f);
  }

  // --- RTC timestamp ---
  DateTime now = rtc.now();
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  d.timestamp = String(buf);

  return d;
}

// =============================================================
//  HC-SR04
// =============================================================
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

// =============================================================
//  SEND JSON OVER SERIAL1 TO ESP32
// =============================================================
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

  // Serialize to Serial1 (→ ESP32), terminated with newline
  serializeJson(doc, Serial1);
  Serial1.println();  // newline tells ESP32 the message is complete

  Serial.println("[TX] JSON sent to ESP32");
}

// =============================================================
//  ALERTS (serial debug — extend to add buzzer/LED)
// =============================================================
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

// =============================================================
//  LIGHT CONTROL (RTC schedule)
// =============================================================
void controlLights() {
  DateTime now = rtc.now();
  bool shouldBeOn = (now.hour() >= LIGHTS_ON_HOUR && now.hour() < LIGHTS_OFF_HOUR);
  if (shouldBeOn != lightsState) {
    lightsState = shouldBeOn;
    digitalWrite(RELAY_LIGHTS, lightsState ? LOW : HIGH);
    Serial.println(lightsState ? "[ACT] Lights ON" : "[ACT] Lights OFF");
  }
}

// =============================================================
//  PUMP CONTROL (timed cycle)
// =============================================================
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

// =============================================================
//  DEBUG PRINT
// =============================================================
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
