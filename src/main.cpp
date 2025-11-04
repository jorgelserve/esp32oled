#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

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
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <title>ESP32 Tone Receiver</title>
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <style>
      :root {
        color-scheme: light dark;
      }
      body {
        font-family: system-ui, -apple-system, Segoe UI, sans-serif;
        margin: 0;
        padding: 1.5rem;
        display: grid;
        place-items: center;
        min-height: 100vh;
        background: radial-gradient(circle at top, #1e90ff22, transparent 60%);
      }
      main {
        max-width: 420px;
        border-radius: 16px;
        box-shadow: 0 25px 60px -35px #000;
        padding: 2rem;
        background: rgba(255,255,255,0.65);
        backdrop-filter: blur(6px);
      }
      h1 {
        font-size: 1.6rem;
        margin-top: 0;
      }
      .tone {
        font-size: 2.8rem;
        margin: 1rem 0;
        line-height: 1.1;
        font-weight: 600;
      }
      button {
        font-size: 1rem;
        padding: 0.75rem 1.5rem;
        border: none;
        border-radius: 999px;
        cursor: pointer;
        background: #1e90ff;
        color: #fff;
      }
      button:disabled {
        opacity: 0.6;
        cursor: default;
      }
      .status {
        font-size: 0.95rem;
        margin-top: 1.5rem;
        line-height: 1.4;
      }
      .log {
        font-family: ui-monospace, SFMono-Regular, SFMono, Menlo, Consolas, "Liberation Mono", monospace;
        font-size: 0.8rem;
        margin-top: 1rem;
        padding: 0.75rem;
        border-radius: 10px;
        background: rgba(30,144,255,0.08);
        max-height: 200px;
        overflow-y: auto;
      }
    </style>
  </head>
  <body>
    <main>
      <h1>ESP32 Tone Receiver</h1>
      <p>The device dictates the tone frequency over WebSocket. Enable sound once, then leave this tab if you want—the audio keeps playing.</p>
      <div class="tone" id="frequency">Waiting...</div>
      <button id="enable">Enable Audio Output</button>
      <p class="status">
        <strong>Connection:</strong> <span id="connection">connecting...</span><br />
        <strong>Last update:</strong> <span id="updated">never</span>
      </p>
      <div class="log" id="log"></div>
    </main>
    <script>
      const connectionEl = document.getElementById("connection");
      const updatedEl = document.getElementById("updated");
      const frequencyEl = document.getElementById("frequency");
      const enableButton = document.getElementById("enable");
      const logEl = document.getElementById("log");

      let audioContext;
      let oscillator;
      let gainNode;
      let websocket;

      function log(message) {
        const now = new Date().toLocaleTimeString();
        logEl.textContent = `[${now}] ${message}\n` + logEl.textContent;
      }

      function ensureAudioChain() {
        if (!audioContext) {
          audioContext = new (window.AudioContext || window.webkitAudioContext)();
          oscillator = audioContext.createOscillator();
          oscillator.type = "sine";
          gainNode = audioContext.createGain();
          gainNode.gain.value = 0;
          oscillator.connect(gainNode).connect(audioContext.destination);
          oscillator.start();
        }
        if (audioContext.state === "suspended") {
          audioContext.resume();
        }
      }

      function playFrequency(freq) {
        ensureAudioChain();
        frequencyEl.textContent = `${Number(freq).toFixed(2)} Hz`;
        oscillator.frequency.setTargetAtTime(freq, audioContext.currentTime, 0.01);
        gainNode.gain.setTargetAtTime(0.18, audioContext.currentTime, 0.02);
        updatedEl.textContent = new Date().toLocaleTimeString();
      }

      function connectWebSocket() {
        const protocol = location.protocol === "https:" ? "wss" : "ws";
        websocket = new WebSocket(`${protocol}://${location.hostname}:${81}/`);
        websocket.onopen = () => {
          connectionEl.textContent = "connected";
          connectionEl.style.color = "green";
          log("WebSocket connected");
        };
        websocket.onclose = () => {
          connectionEl.textContent = "disconnected (retrying...)";
          connectionEl.style.color = "red";
          log("WebSocket disconnected; retrying in 2s");
          setTimeout(connectWebSocket, 2000);
        };
        websocket.onerror = (error) => {
          log(`Socket error: ${error.message || error}`);
        };
        websocket.onmessage = (event) => {
          const freq = parseFloat(event.data);
          if (!isFinite(freq)) {
            log(`Ignored non-numeric payload: ${event.data}`);
            return;
          }
          playFrequency(freq);
        };
      }

      enableButton.addEventListener("click", () => {
        ensureAudioChain();
        gainNode.gain.setValueAtTime(0.18, audioContext.currentTime);
        enableButton.disabled = true;
        enableButton.textContent = "Audio Ready";
        log("Audio output enabled by user");
      });

      document.addEventListener("visibilitychange", () => {
        if (document.visibilityState === "hidden" && audioContext) {
          audioContext.resume();
        }
      });

      window.addEventListener("focus", () => {
        if (audioContext && audioContext.state === "suspended") {
          audioContext.resume();
        }
      });

      connectWebSocket();
    </script>
  </body>
</html>
)rawliteral";

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
size_t currentToneIndex = 0;
String lastFrequencyStr = String(toneFrequencies[0]);
String ipString = "0.0.0.0";
volatile uint8_t apClientCount = 0;
uint16_t connectedClientCount = 0;
bool apReady = false;

// ********** Forward declarations **********
void broadcastFrequency(uint16_t frequency);
void refreshDisplay();
void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

void refreshDisplay() {
  const String line1 = apReady ? String("AP: ") + AP_SSID : "AP: starting...";
  const String line2 = "IP: " + ipString;
  String line3 = "Tone: " + lastFrequencyStr + " Hz";
  if (apClientCount > 0 || connectedClientCount > 0) {
    line3 += " C:" + String(apClientCount) + "/" + String(connectedClientCount);
  }
  display.showLines(line1, line2, line3);
}

void broadcastFrequency(uint16_t frequency) {
  lastFrequencyStr = String(frequency);
  webSocket.broadcastTXT(lastFrequencyStr);
  refreshDisplay();
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
      webSocket.sendTXT(clientNum, lastFrequencyStr);
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
        broadcastFrequency(toneFrequencies[currentToneIndex]);
      } else if (payloadStr.length() > 0) {
        int requested = payloadStr.toInt();
        if (requested > 0) {
          broadcastFrequency(static_cast<uint16_t>(requested));
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
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
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

  broadcastFrequency(toneFrequencies[currentToneIndex]);
}

void loop() {
  server.handleClient();
  webSocket.loop();

  if (selectorButton.wasPressed()) {
    currentToneIndex = (currentToneIndex + 1) % toneCount;
    broadcastFrequency(toneFrequencies[currentToneIndex]);
    statusLed.toggle();
  }
}
