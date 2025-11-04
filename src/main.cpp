#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "web_bundle.h"

// ********** Access Point configuration **********
// The ESP32 runs as an AP so no external Wi-Fi credentials are required.
// Optionally set AP_PASSWORD (minimum 8 chars) to secure the network.
const char *AP_SSID = "ToneHub-ESP32";
const char *AP_PASSWORD = "ToneHub123";  // Minimum 8 characters if not empty
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GW(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

// ********** Web resources **********
constexpr uint16_t kWebSocketPort = 81;



// ********** UI helpers **********
class OledDisplay {
 private:
  U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2;

 public:
  OledDisplay() : u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5) {}

  void begin() {
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
  }

  void showLines(const String &line1, const String &line2, const String &line3) {
    u8g2.clearBuffer();
    if (line1.length()) {
      u8g2.drawStr(0, 10, line1.c_str());
    }
    if (line2.length()) {
      u8g2.drawStr(0, 20, line2.c_str());
    }
    if (line3.length()) {
      u8g2.drawStr(0, 30, line3.c_str());
    }
    u8g2.sendBuffer();
  }
};

class LedController {
 private:
  uint8_t pin;
  bool state;

 public:
  explicit LedController(uint8_t ledPin) : pin(ledPin), state(false) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  void on() {
    digitalWrite(pin, HIGH);
    state = true;
  }

  void off() {
    digitalWrite(pin, LOW);
    state = false;
  }

  void toggle() {
    state ? off() : on();
  }

  bool isOn() const { return state; }
};

class Button {
 private:
  uint8_t pin;
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long debounceDelay;

 public:
  explicit Button(uint8_t buttonPin)
      : pin(buttonPin),
        lastState(HIGH),
        currentState(HIGH),
        lastDebounceTime(0),
        debounceDelay(50) {
    pinMode(pin, INPUT_PULLUP);
  }

  bool wasPressed() {
    bool reading = digitalRead(pin);
    if (reading != lastState) {
      lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != currentState) {
        currentState = reading;
        if (currentState == LOW) {
          lastState = reading;
          return true;
        }
      }
    }

    lastState = reading;
    return false;
  }
};

// ********** Globals **********
OledDisplay display;
LedController statusLed(8);
Button selectorButton(9);
WebServer server(80);
WebSocketsServer webSocket(kWebSocketPort);

const uint16_t toneFrequencies[] = {261, 329, 392, 440, 523, 659};
constexpr size_t toneCount = sizeof(toneFrequencies) / sizeof(toneFrequencies[0]);
constexpr float kDefaultRangeMinHz = 120.0f;
constexpr float kDefaultRangeMaxHz = 1200.0f;
constexpr uint16_t kNormalizedScale = 1000;  // Precision for debouncing broadcasts

float currentNormalizedValue = 0.0f;
size_t currentToneIndex = 0;
String lastNormalizedStr = String("0.0000");
uint16_t lastBroadcastNormalizedScaled = kNormalizedScale + 1;
String ipString = "0.0.0.0";
volatile uint8_t apClientCount = 0;
uint16_t connectedClientCount = 0;
bool apReady = false;

// ********** Forward declarations **********
void broadcastNormalizedValue(float normalized, bool force = false);
void refreshDisplay();
void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
bool tryParseNormalizedCommand(const String &payload, float &normalizedOut);
size_t findClosestToneIndex(uint16_t frequency);
float frequencyToNormalized(float frequency);
float normalizedToFrequency(float normalized);

void refreshDisplay() {
  const String line1 = apReady ? String("AP: ") + AP_SSID : "AP: starting...";
  const float displayFrequency = normalizedToFrequency(currentNormalizedValue);
  String line2 = "IP: " + ipString;
  String line3 = "N:" + String(currentNormalizedValue, 2) + " F:" + String(displayFrequency, 0);
  if (apClientCount > 0 || connectedClientCount > 0) {
    line3 += " C:" + String(apClientCount) + "/" + String(connectedClientCount);
  }
  display.showLines(line1, line2, line3);
}

float clampNormalized(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

float frequencyToNormalized(float frequency) {
  const float span = kDefaultRangeMaxHz - kDefaultRangeMinHz;
  if (span <= 0.0f) {
    return 0.0f;
  }
  const float normalized = (frequency - kDefaultRangeMinHz) / span;
  return clampNormalized(normalized);
}

float normalizedToFrequency(float normalized) {
  const float span = kDefaultRangeMaxHz - kDefaultRangeMinHz;
  return kDefaultRangeMinHz + clampNormalized(normalized) * span;
}

void broadcastNormalizedValue(float normalized, bool force) {
  const float sanitized = clampNormalized(normalized);
  const uint16_t scaled = static_cast<uint16_t>(sanitized * kNormalizedScale + 0.5f);
  if (!force && scaled == lastBroadcastNormalizedScaled) {
    return;
  }

  lastBroadcastNormalizedScaled = scaled;
  currentNormalizedValue = sanitized;
  lastNormalizedStr = String(sanitized, 4);
  webSocket.broadcastTXT(lastNormalizedStr);
  refreshDisplay();
}

bool tryParseNormalizedCommand(const String &payload, float &normalizedOut) {
  String trimmed = payload;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return false;
  }

  String lower = trimmed;
  lower.toLowerCase();
  int separatorIndex = lower.indexOf(':');
  if (separatorIndex < 0) {
    separatorIndex = lower.indexOf('=');
  }

  String numericPortion;
  if (separatorIndex > 0) {
    const String key = lower.substring(0, separatorIndex);
    if (key != "val" && key != "value" && key != "norm" && key != "normalized") {
      return false;
    }
    numericPortion = trimmed.substring(separatorIndex + 1);
  } else {
    numericPortion = trimmed;
  }

  numericPortion.trim();
  if (numericPortion.length() == 0) {
    return false;
  }

  const float parsed = numericPortion.toFloat();
  if (!(parsed >= 0.0f)) {
    return false;
  }

  normalizedOut = clampNormalized(parsed);
  return true;
}

size_t findClosestToneIndex(uint16_t frequency) {
  size_t closestIndex = 0;
  uint32_t smallestDelta = toneFrequencies[0] > frequency ? (toneFrequencies[0] - frequency)
                                                         : (frequency - toneFrequencies[0]);
  for (size_t i = 1; i < toneCount; ++i) {
    const uint32_t delta = toneFrequencies[i] > frequency ? (toneFrequencies[i] - frequency)
                                                          : (frequency - toneFrequencies[i]);
    if (delta < smallestDelta) {
      smallestDelta = delta;
      closestIndex = i;
    }
  }
  return closestIndex;
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_SUBNET);

  display.showLines("Starting AP...", AP_SSID, "");
  Serial.printf("Starting access point '%s'\n", AP_SSID);

  bool started = false;
  if (strlen(AP_PASSWORD) == 0) {
    started = WiFi.softAP(AP_SSID);
  } else {
    started = WiFi.softAP(AP_SSID, AP_PASSWORD);
  }

  if (!started) {
    display.showLines("AP start failed", AP_SSID, "");
    Serial.println("Failed to start SoftAP");
    return;
  }

  apReady = true;
  ipString = WiFi.softAPIP().toString();
  apClientCount = WiFi.softAPgetStationNum();
  connectedClientCount = 0;
  refreshDisplay();
  statusLed.on();
}

void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(clientNum);
      Serial.printf("WebSocket client #%u connected from %s\n", clientNum, ip.toString().c_str());
      webSocket.sendTXT(clientNum, lastNormalizedStr);
      connectedClientCount = webSocket.connectedClients();
      refreshDisplay();
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client #%u disconnected\n", clientNum);
      connectedClientCount = webSocket.connectedClients();
      refreshDisplay();
      break;
    case WStype_TEXT: {
      String payloadStr(reinterpret_cast<char *>(payload), length);
      payloadStr.trim();
      Serial.printf("Received data from client #%u: %s\n", clientNum, payloadStr.c_str());
      if (payloadStr.equalsIgnoreCase("next")) {
        currentToneIndex = (currentToneIndex + 1) % toneCount;
        const float normalized = frequencyToNormalized(static_cast<float>(toneFrequencies[currentToneIndex]));
        broadcastNormalizedValue(normalized);
      } else {
        float requestedNormalized = 0.0f;
        if (tryParseNormalizedCommand(payloadStr, requestedNormalized)) {
          const float requestedFrequency = normalizedToFrequency(requestedNormalized);
          currentToneIndex = findClosestToneIndex(static_cast<uint16_t>(requestedFrequency + 0.5f));
          broadcastNormalizedValue(requestedNormalized);
        }
      }
      connectedClientCount = webSocket.connectedClients();
      refreshDisplay();
      break;
    }
    default:
      break;
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      apClientCount = WiFi.softAPgetStationNum();
      refreshDisplay();
      break;
    default:
      break;
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.sendHeader("ETag", FPSTR(INDEX_HTML_SHA1));
    server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(INDEX_HTML_GZ), INDEX_HTML_GZ_LEN);
  });
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);
  server.begin();
  Serial.println("HTTP and WebSocket servers started");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("ESP32 WebTone starting");

  display.begin();
  display.showLines("Booting...", "", "");

  WiFi.onEvent(onWiFiEvent);
  startAccessPoint();
  setupWebServer();

  currentNormalizedValue = frequencyToNormalized(static_cast<float>(toneFrequencies[currentToneIndex]));
  broadcastNormalizedValue(currentNormalizedValue, true);
}

void loop() {
  server.handleClient();
  webSocket.loop();

  if (selectorButton.wasPressed()) {
    currentToneIndex = (currentToneIndex + 1) % toneCount;
    const float normalized = frequencyToNormalized(static_cast<float>(toneFrequencies[currentToneIndex]));
    broadcastNormalizedValue(normalized);
    statusLed.toggle();
  }
}
