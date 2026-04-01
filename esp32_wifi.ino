/*
 * =============================================================
 *  Hydroponic IoT — ESP32 WiFi Bridge
 *
 *  Listens on Serial2 (RX2=GPIO16) for newline-terminated JSON
 *  from the Arduino Mega, then POSTs it to the Flask server.
 *
 *  Mega TX1 (pin 18) → voltage divider → ESP32 RX2 (GPIO16)
 *  Common GND between Mega and ESP32 is REQUIRED
 * =============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>

// ----- WiFi credentials -----
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// ----- Flask server endpoint -----
const char* SERVER_URL = "http://YOUR_SERVER_IP:5000/api/data";

// ----- Serial2 pin (Mega → ESP32) -----
#define RXD2 16   // GPIO16 — connect via voltage divider from Mega TX1
#define TXD2 17   // GPIO17 — connect to Mega RX1 (optional, for future commands)

// ----- Buffer -----
String serialBuffer = "";
const int MAX_BUFFER = 600;  // max JSON length from Mega

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);           // USB debug
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);  // from Mega

  Serial.println("=== Hydroponic ESP32 WiFi Bridge Starting ===");
  connectWiFi();
}

// =============================================================
//  LOOP — read Serial2, forward complete JSON lines
// =============================================================
void loop() {
  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARN] WiFi lost — reconnecting...");
    connectWiFi();
  }

  // Read bytes from Mega one at a time
  while (Serial2.available()) {
    char c = (char)Serial2.read();

    if (c == '\n') {
      // Complete line received — trim and process
      serialBuffer.trim();
      if (serialBuffer.length() > 0) {
        Serial.println("[RX] Received from Mega: " + serialBuffer);
        sendToServer(serialBuffer);
      }
      serialBuffer = "";  // reset buffer
    } else {
      serialBuffer += c;
      // Guard against buffer overflow (malformed data)
      if (serialBuffer.length() > MAX_BUFFER) {
        Serial.println("[WARN] Buffer overflow — discarding");
        serialBuffer = "";
      }
    }
  }
}

// =============================================================
//  POST JSON TO FLASK SERVER
// =============================================================
void sendToServer(const String& jsonPayload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERROR] No WiFi — cannot send data");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);  // 5 second timeout

  int httpCode = http.POST(jsonPayload);

  if (httpCode == 200 || httpCode == 201) {
    Serial.println("[OK] Data sent to server (HTTP " + String(httpCode) + ")");
  } else if (httpCode < 0) {
    Serial.println("[ERROR] Connection failed: " + http.errorToString(httpCode));
  } else {
    Serial.println("[ERROR] Server returned HTTP " + String(httpCode));
  }

  http.end();
}

// =============================================================
//  WIFI CONNECTION
// =============================================================
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Connection failed — will retry");
  }
}
