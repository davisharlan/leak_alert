/*
  ESP8266 Leak Alert -> ntfy push notifications

  Board:  NodeMCU 1.0 (ESP-12E) or Wemos D1 mini
  Sensor: Two bare probe wires on D5 and GND. Water bridges them and D5 reads LOW.
  Alerts: Sent to https://ntfy.sh/<NTFY_TOPIC>. See README.md for setup.
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

// ---------- Config: fill these in ----------
// WIFI_SSID, WIFI_PASS and NTFY_TOPIC live in secrets.h (start from secrets.example.h)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found: copy secrets.example.h to secrets.h and fill it in"
#endif

const char* LOCATION = "Jack and Jill bathroom";  // Shows up in the notifications

// ---------- Pins ----------
const uint8_t SENSOR_PIN = 14;           // D5 / GPIO14 (avoid D3, D4, D8: they affect boot)
const uint8_t LED_PIN    = LED_BUILTIN;  // Onboard LED, active LOW

// ---------- Timing ----------
const unsigned long WET_CONFIRM_MS  = 2UL * 1000;             // Must stay wet 2 s to count as a leak
const unsigned long DRY_CONFIRM_MS  = 30UL * 1000;            // Must stay dry 30 s to clear
const unsigned long REMINDER_MS     = 5UL * 60 * 1000;        // Re-alert every 5 min while wet
const unsigned long RETRY_MS        = 30UL * 1000;            // Retry a failed send after 30 s
const unsigned long HEARTBEAT_MS    = 24UL * 60 * 60 * 1000;  // Daily "still alive" message
const unsigned long WIFI_TIMEOUT_MS = 20UL * 1000;            // Give up on a WiFi connect after 20 s

// ---------- State ----------
bool leakActive = false;            // True from a confirmed leak until it's confirmed dry
bool wetTiming  = false;            // True while timing a continuous wet stretch
bool dryTiming  = false;            // True while timing a continuous dry stretch
unsigned long wetSince        = 0;  // When the current wet stretch began
unsigned long drySince        = 0;  // When the current dry stretch began
unsigned long nextAlertAt     = 0;  // When the next leak reminder (or retry) is due
unsigned long nextHeartbeatAt = 0;  // When the next heartbeat (or retry) is due

// ---------- Helpers ----------

// Connect to WiFi if not already connected. Blocks for up to WIFI_TIMEOUT_MS.
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED");
  }
}

// Send a push notification to the ntfy topic. Returns true if ntfy accepted it.
//   priority: "min", "low", "default", "high", or "urgent"
//   tags:     Comma-separated ntfy emoji names, e.g. "rotating_light" or "white_check_mark"
bool sendNtfy(const String& title, const String& msg,
              const char* priority = "default", const char* tags = "") {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();               // Skip cert check — fine for a hobby alert
  client.setBufferSizes(1024, 1024);  // Smaller TLS buffers save RAM on the ESP8266

  HTTPClient https;
  String url = String("https://ntfy.sh/") + NTFY_TOPIC;
  if (!https.begin(client, url)) {
    Serial.println("HTTPS begin failed");
    return false;
  }

  https.addHeader("Content-Type", "text/plain; charset=utf-8");
  https.addHeader("Title", title);    // Keep titles plain ASCII (no emoji in headers)
  https.addHeader("Priority", priority);
  if (tags[0] != '\0') https.addHeader("Tags", tags);

  int code = https.POST(msg);         // Message body is the notification text
  if (code != 200) {
    Serial.printf("ntfy error %d: %s\n", code, https.getString().c_str());
  } else {
    Serial.printf("ntfy sent: %s - %s\n", title.c_str(), msg.c_str());
  }
  https.end();
  return code == 200;
}

// ---------- Main ----------

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(SENSOR_PIN, INPUT_PULLUP);  // Dry = HIGH (pulled up), wet = LOW (shorted to GND)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);        // LED off

  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);             // Don't write WiFi settings to flash on every connect
  connectWiFi();

  sendNtfy("Leak sensor online", LOCATION, "low", "white_check_mark");
  nextHeartbeatAt = millis() + HEARTBEAT_MS;
}

void loop() {
  unsigned long now = millis();
  bool wet = (digitalRead(SENSOR_PIN) == LOW);

  // Track how long the sensor has been continuously wet or dry
  if (wet) {
    dryTiming = false;
    if (!wetTiming) {
      wetTiming = true;
      wetSince = now;
    }
  } else {
    wetTiming = false;
    if (!dryTiming) {
      dryTiming = true;
      drySince = now;
    }
  }

  // Note: the "(long)(now - deadline) >= 0" checks below stay correct when
  // millis() rolls over (every ~49 days), unlike a plain "now >= deadline".

  // New leak: wet long enough to confirm
  if (!leakActive && wetTiming && now - wetSince >= WET_CONFIRM_MS) {
    leakActive = true;
    digitalWrite(LED_PIN, LOW);       // LED on
    bool ok = sendNtfy("LEAK DETECTED", String("Water at: ") + LOCATION, "urgent", "rotating_light");
    nextAlertAt = now + (ok ? REMINDER_MS : RETRY_MS);
  }
  // Still leaking: send a reminder (or retry a failed send)
  else if (leakActive && wet && (long)(now - nextAlertAt) >= 0) {
    bool ok = sendNtfy("Still wet", String("Water at: ") + LOCATION, "urgent", "rotating_light");
    nextAlertAt = now + (ok ? REMINDER_MS : RETRY_MS);
  }
  // Leak cleared: dry long enough to confirm
  else if (leakActive && dryTiming && now - drySince >= DRY_CONFIRM_MS) {
    leakActive = false;
    digitalWrite(LED_PIN, HIGH);      // LED off
    sendNtfy("Dry again", LOCATION, "default", "white_check_mark");
  }

  // Daily heartbeat so you know it's still working (or retry a failed send)
  if ((long)(now - nextHeartbeatAt) >= 0) {
    bool ok = sendNtfy("Leak sensor OK", LOCATION, "min", "heartbeat");
    nextHeartbeatAt = now + (ok ? HEARTBEAT_MS : RETRY_MS);
  }

  delay(100);
}
