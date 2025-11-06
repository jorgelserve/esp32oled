#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <cstdio>
#include <cstring>
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
#define kWebSocketPort 81  // Web socket port
#define kNormalizedScale 1000  // Precision for debouncing broadcasts
#define kPotentiometerDeltaThreshold 0.005f  // Minimum change to trigger broadcast (0.5%)
#define kAdcFilterAlpha 0.1f  // Alpha value for exponential moving average (0.1 = more smoothing)
#define kUiScreenCount 3  // Number of UI screens
#define kDisplayFrameIntervalMs 125  // Display refresh interval in ms
#define kMarqueeGapPx 12  // Gap in pixels for marquee text
#define kMarqueeStepMs 45  // Marquee animation step in ms
#define kPotentiometerPin 4  // ADC1_CH4 on ESP32-C3
#define kDefaultRangeMinHz 120.0f  // Default frequency range minimum
#define kDefaultRangeMaxHz 1200.0f  // Default frequency range maximum
#define kNormalizedScaleFloat 1000.0f  // Float version for calculations
#define kDebounceDelay 50  // Button debounce delay in ms


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

  void showLines(const String &line1, const String &line2, const String &line3, int16_t line2PrimaryX = 0,
                 bool drawWrapped = false, int16_t line2WrappedX = 0) {
    u8g2.clearBuffer();
    if (line1.length()) {
      u8g2.drawUTF8(0, 10, line1.c_str());
    }
    if (line2.length()) {
      u8g2.drawUTF8(line2PrimaryX, 20, line2.c_str());
      if (drawWrapped) {
        u8g2.drawUTF8(line2WrappedX, 20, line2.c_str());
      }
    }
    if (line3.length()) {
      u8g2.drawUTF8(0, 30, line3.c_str());
    }
    u8g2.sendBuffer();
  }

  void beginFrame() { u8g2.clearBuffer(); }

  void endFrame() { u8g2.sendBuffer(); }

  void drawText(int16_t x, int16_t y, const char *text) { u8g2.drawUTF8(x, y, text); }

  void drawText(int16_t x, int16_t y, const String &text) { drawText(x, y, text.c_str()); }

  uint16_t textWidth(const char *text) { return u8g2.getUTF8Width(text); }

  uint16_t textWidth(const String &text) { return textWidth(text.c_str()); }

  uint8_t width() { return u8g2.getDisplayWidth(); }
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
Button stopButton(7);  // New stop button on pin 7
WebServer server(80);
WebSocketsServer webSocket(kWebSocketPort);

const uint16_t toneFrequencies[] = {261, 329, 392, 440, 523, 659};
const size_t toneCount = sizeof(toneFrequencies) / sizeof(toneFrequencies[0]);

float currentNormalizedValue = 0.0f;
float filteredNormalizedValue = 0.0f;  // Filtered value for smoothing
size_t currentToneIndex = 0;
String lastNormalizedStr = String("0.0000");
uint16_t lastBroadcastNormalizedScaled = kNormalizedScale + 1;
String ipString = "0.0.0.0";
volatile uint8_t apClientCount = 0;
uint16_t connectedClientCount = 0;
bool apReady = false;
bool toneStopped = false;  // New state to track if tone should be stopped

uint8_t currentUiScreen = 0;
bool displayDirty = true;
uint32_t lastDisplayFrameMs = 0;

// ********** Forward declarations **********
void broadcastNormalizedValue(float normalized, bool force = false);
void refreshDisplay();
void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
bool tryParseNormalizedCommand(const String &payload, float &normalizedOut);
size_t findClosestToneIndex(uint16_t frequency);
float frequencyToNormalized(float frequency);
float normalizedToFrequency(float normalized);

void refreshDisplay() { displayDirty = true; }

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

int16_t computeMarqueePrimaryX(const char *text, uint32_t now) {
  const uint16_t textPixelWidth = display.textWidth(text);
  const uint8_t screenWidth = display.width();
  if (textPixelWidth <= screenWidth) {
    return 0;
  }
  const uint16_t travel = textPixelWidth + screenWidth + kMarqueeGapPx;
  const uint32_t step = (now / kMarqueeStepMs) % travel;
  return static_cast<int16_t>(screenWidth) - static_cast<int16_t>(step);
}

void drawMarqueeLine(const char *text, int16_t y, uint32_t now) {
  const uint16_t textPixelWidth = display.textWidth(text);
  const uint8_t screenWidth = display.width();
  if (textPixelWidth <= screenWidth) {
    display.drawText(0, y, text);
    return;
  }

  const int16_t primaryX = computeMarqueePrimaryX(text, now);
  display.drawText(primaryX, y, text);
  display.drawText(primaryX + textPixelWidth + kMarqueeGapPx, y, text);
}

void drawMarqueeLine(const String &text, int16_t y, uint32_t now) {
  drawMarqueeLine(text.c_str(), y, now);
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

void renderOverviewScreen(uint32_t now) {
  display.drawText(0, 10, apReady ? "AP Ready" : "AP Boot");
  char ipLine[24];
  snprintf(ipLine, sizeof(ipLine), "IP %s", ipString.c_str());
  drawMarqueeLine(ipLine, 20, now);
  char clientLine[24];
  snprintf(clientLine, sizeof(clientLine), "STA %u WS %u", static_cast<unsigned>(apClientCount),
           static_cast<unsigned>(connectedClientCount));
  display.drawText(0, 30, clientLine);
}

void renderToneScreen(uint32_t now) {
  (void)now;
  display.drawText(0, 10, "Tone Ctl");
  char normLine[24];
  snprintf(normLine, sizeof(normLine), "Norm %.3f", static_cast<double>(currentNormalizedValue));
  display.drawText(0, 20, normLine);
  char freqLine[24];
  snprintf(freqLine, sizeof(freqLine), "Freq %.0f Hz", static_cast<double>(normalizedToFrequency(currentNormalizedValue)));
  display.drawText(0, 30, freqLine);
}

void renderInfoScreen(uint32_t now) {
  display.drawText(0, 10, "BTN: Next");
  char ssidLine[32];
  snprintf(ssidLine, sizeof(ssidLine), "SSID %s", AP_SSID);
  drawMarqueeLine(ssidLine, 20, now);
  const char *password = strlen(AP_PASSWORD) > 0 ? AP_PASSWORD : "(open)";
  char pwdLine[32];
  snprintf(pwdLine, sizeof(pwdLine), "PWD %s", password);
  drawMarqueeLine(pwdLine, 30, now);
}

void renderCurrentScreen(uint32_t now) {
  display.beginFrame();
  switch (currentUiScreen) {
    case 0:
      renderOverviewScreen(now);
      break;
    case 1:
      renderToneScreen(now);
      break;
    case 2:
      renderInfoScreen(now);
      break;
    default:
      renderOverviewScreen(now);
      break;
  }
  display.endFrame();
  lastDisplayFrameMs = now;
  displayDirty = false;
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
      // Send appropriate message based on current state
      if (toneStopped) {
        webSocket.sendTXT(clientNum, "stop");
      } else {
        webSocket.sendTXT(clientNum, lastNormalizedStr);
      }
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
      if (payloadStr.equalsIgnoreCase("ping")) {
        webSocket.sendTXT(clientNum, "pong");
        break;
      }
      if (payloadStr.equalsIgnoreCase("next")) {
        currentToneIndex = (currentToneIndex + 1) % toneCount;
        const float normalized = frequencyToNormalized(static_cast<float>(toneFrequencies[currentToneIndex]));
        toneStopped = false; // Reset stop state when changing tone
        filteredNormalizedValue = normalized; // Update filtered value to match
        broadcastNormalizedValue(normalized);
      } else if (payloadStr.equalsIgnoreCase("stop")) {
        // Set the stop state and broadcast "stop" command
        toneStopped = true;
        webSocket.broadcastTXT("stop");
        Serial.println("Tone stopped command received and broadcast to all clients");
      } else {
        float requestedNormalized = 0.0f;
        if (tryParseNormalizedCommand(payloadStr, requestedNormalized)) {
          const float requestedFrequency = normalizedToFrequency(requestedNormalized);
          currentToneIndex = findClosestToneIndex(static_cast<uint16_t>(requestedFrequency + 0.5f));
          toneStopped = false; // Reset stop state when setting specific value
          filteredNormalizedValue = requestedNormalized; // Update filtered value to match
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
  webSocket.enableHeartbeat(15000, 3000, 2);
  webSocket.onEvent(handleWebSocketEvent);
  server.begin();
  Serial.println("HTTP and WebSocket servers started");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("ESP32 WebTone starting");

  setCpuFrequencyMhz(80);

  display.begin();
  display.showLines("Booting...", "", "");

  WiFi.onEvent(onWiFiEvent);
  startAccessPoint();
  setupWebServer();

  currentNormalizedValue = frequencyToNormalized(static_cast<float>(toneFrequencies[currentToneIndex]));
  filteredNormalizedValue = currentNormalizedValue;  // Initialize filtered value
  broadcastNormalizedValue(currentNormalizedValue, true);
}

void loop() {
  server.handleClient();
  webSocket.loop();

  const uint32_t now = millis();

  {
    const int adcValue = analogRead(kPotentiometerPin);
    const float rawNormalized = static_cast<float>(adcValue) / 4095.0f;
    
    // Apply exponential moving average filter to smooth the input
    filteredNormalizedValue = kAdcFilterAlpha * rawNormalized + (1.0f - kAdcFilterAlpha) * filteredNormalizedValue;
    
    // Only broadcast potentiometer updates if tone is not stopped
    if (!toneStopped) {
      // Only broadcast if the change is significant enough to avoid noise
      const float delta = abs(filteredNormalizedValue - currentNormalizedValue);
      if (delta >= kPotentiometerDeltaThreshold) {
        broadcastNormalizedValue(filteredNormalizedValue);
      }
    }
  }

  if (selectorButton.wasPressed()) {
    currentUiScreen = (currentUiScreen + 1) % kUiScreenCount;
    statusLed.toggle();
    refreshDisplay();
  }

  // Check if the stop button is pressed
  if (stopButton.wasPressed()) {
    // Toggle between stopped and playing states
    if (toneStopped) {
      // Currently stopped, so resume
      toneStopped = false;
      // Update filtered value to current value to avoid jumps
      filteredNormalizedValue = currentNormalizedValue;
      // Send current normalized value to resume tone
      webSocket.broadcastTXT(lastNormalizedStr);
      Serial.println("Tone resumed via hardware button");
    } else {
      // Currently playing, so stop
      toneStopped = true;
      webSocket.broadcastTXT("stop");
      Serial.println("Tone stopped via hardware button");
    }
    refreshDisplay();
  }

  if (displayDirty || (now - lastDisplayFrameMs) >= kDisplayFrameIntervalMs) {
    renderCurrentScreen(now);
  }
}
