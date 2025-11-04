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
        max-width: 460px;
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
        transition: opacity 0.2s ease;
        width: 100%;
        margin-top: 1rem;
      }
      button:disabled {
        opacity: 0.6;
        cursor: default;
      }
      .controls {
        margin-top: 1.5rem;
      }
      .slider-label {
        display: flex;
        justify-content: space-between;
        font-size: 0.95rem;
        margin-bottom: 0.5rem;
        color: #0f172a;
      }
      input[type="range"] {
        width: 100%;
        accent-color: #1e90ff;
      }
      .range-inputs {
        display: flex;
        gap: 0.75rem;
        margin-top: 0.75rem;
      }
      .range-inputs label {
        flex: 1;
        font-size: 0.8rem;
        color: #334155;
        display: flex;
        flex-direction: column;
        gap: 0.35rem;
      }
      .range-inputs input {
        font-size: 1rem;
        padding: 0.4rem 0.5rem;
        border-radius: 10px;
        border: 1px solid rgba(15,23,42,0.15);
        background: rgba(255,255,255,0.75);
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
        white-space: pre-line;
      }
    </style>
  </head>
  <body>
    <main>
      <h1>ESP32 Tone Receiver</h1>
      <p>Controla el tono desde cualquier dispositivo conectado. Ajusta la frecuencia con el control deslizante y se sincronizará en tiempo real.</p>
      <div class="tone" id="frequency">Waiting...</div>
      <section class="controls">
        <label class="slider-label" for="frequencySlider">
          <span>Ajusta la frecuencia</span>
          <span id="sliderValue">440 Hz</span>
        </label>
        <input type="range" id="frequencySlider" min="120" max="1200" step="1" value="440" />
        <div class="range-inputs">
          <label>
            Min (Hz)
            <input type="number" id="minFrequency" min="1" value="120" />
          </label>
          <label>
            Max (Hz)
            <input type="number" id="maxFrequency" min="2" value="1200" />
          </label>
        </div>
      </section>
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
      const slider = document.getElementById("frequencySlider");
      const sliderValueEl = document.getElementById("sliderValue");
      const minInput = document.getElementById("minFrequency");
      const maxInput = document.getElementById("maxFrequency");

      let audioContext;
      let oscillator;
      let gainNode;
      let websocket;

      const state = {
        desiredFrequency: Number(slider.value),
        lastBroadcastedFrequency: Number.NaN,
        pendingSend: null,
        queuedFrequency: null,
        applyingRemoteUpdate: false,
      };

      function log(message) {
        const now = new Date().toLocaleTimeString();
        logEl.textContent = `[${now}] ${message}
` + logEl.textContent;
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
          audioContext.resume().catch(() => {});
        }
        if (audioContext.state === "running" && !enableButton.disabled) {
          enableButton.disabled = true;
          enableButton.textContent = "Audio Ready";
        }
      }

      function playFrequency(freq) {
        ensureAudioChain();
        const frequencyValue = Number(freq);
        frequencyEl.textContent = `${frequencyValue.toFixed(2)} Hz`;
        sliderValueEl.textContent = `${frequencyValue.toFixed(0)} Hz`;
        oscillator.frequency.setTargetAtTime(frequencyValue, audioContext.currentTime, 0.01);
        gainNode.gain.setTargetAtTime(0.18, audioContext.currentTime, 0.02);
        updatedEl.textContent = new Date().toLocaleTimeString();
      }

      function sanitizeBounds(changed) {
        let minVal = parseInt(minInput.value, 10);
        let maxVal = parseInt(maxInput.value, 10);

        if (!Number.isFinite(minVal)) {
          minVal = Number(slider.min) || 120;
        }
        if (!Number.isFinite(maxVal)) {
          maxVal = Number(slider.max) || 1200;
        }

        if (minVal < 1) {
          minVal = 1;
        }

        if (minVal >= maxVal) {
          if (changed === "min") {
            maxVal = minVal + 1;
            maxInput.value = Math.round(maxVal);
          } else {
            minVal = maxVal - 1;
            if (minVal < 1) {
              minVal = 1;
            }
            minInput.value = Math.round(minVal);
          }
        }

        minVal = Math.round(minVal);
        maxVal = Math.round(maxVal);

        minInput.value = String(minVal);
        maxInput.value = String(maxVal);
        slider.min = String(minVal);
        slider.max = String(maxVal);

        return { min: minVal, max: maxVal };
      }

      function clampToBounds(freq) {
        const minVal = Number(slider.min);
        const maxVal = Number(slider.max);
        const rounded = Math.round(freq);
        return Math.min(Math.max(rounded, minVal), maxVal);
      }

      function extendBoundsToFit(freq) {
        const minVal = Number(slider.min);
        const maxVal = Number(slider.max);
        let changed = false;

        if (freq < minVal) {
          minInput.value = String(Math.max(1, Math.floor(freq)));
          changed = true;
        }
        if (freq > maxVal) {
          maxInput.value = String(Math.ceil(freq));
          changed = true;
        }
        if (changed) {
          sanitizeBounds();
        }
      }

      function pushFrequency(freq, options = {}) {
        const { force = false } = options;
        const sanitized = clampToBounds(freq);
        if (!websocket || websocket.readyState !== WebSocket.OPEN) {
          state.queuedFrequency = sanitized;
          return;
        }
        if (!force && Number.isFinite(state.lastBroadcastedFrequency) && state.lastBroadcastedFrequency === sanitized) {
          return;
        }
        websocket.send(`freq:${sanitized}`);
        state.lastBroadcastedFrequency = sanitized;
        state.queuedFrequency = null;
        log(`Frecuencia enviada: ${sanitized} Hz`);
      }

      function queueFrequencyBroadcast(freq) {
        state.desiredFrequency = freq;
        if (state.pendingSend) {
          clearTimeout(state.pendingSend);
        }
        state.pendingSend = setTimeout(() => {
          state.pendingSend = null;
          pushFrequency(state.desiredFrequency);
        }, 120);
      }

      function flushFrequencyBroadcast() {
        if (state.pendingSend) {
          clearTimeout(state.pendingSend);
          state.pendingSend = null;
        }
        pushFrequency(state.desiredFrequency);
      }

      function handleSliderInput() {
        if (state.applyingRemoteUpdate) {
          return;
        }
        const freq = clampToBounds(Number(slider.value));
        state.desiredFrequency = freq;
        playFrequency(freq);
        queueFrequencyBroadcast(freq);
      }

      slider.addEventListener("input", handleSliderInput);
      slider.addEventListener("change", flushFrequencyBroadcast);
      ["pointerup", "touchend", "mouseup"].forEach((evt) => {
        slider.addEventListener(evt, flushFrequencyBroadcast, { passive: true });
      });

      function handleBoundsChange(source) {
        sanitizeBounds(source);
        const clamped = clampToBounds(state.desiredFrequency);
        if (clamped !== state.desiredFrequency) {
          state.desiredFrequency = clamped;
          state.applyingRemoteUpdate = true;
          slider.value = String(clamped);
          state.applyingRemoteUpdate = false;
          playFrequency(clamped);
          queueFrequencyBroadcast(clamped);
        }
      }

      minInput.addEventListener("change", () => handleBoundsChange("min"));
      minInput.addEventListener("input", () => sanitizeBounds("min"));
      maxInput.addEventListener("change", () => handleBoundsChange("max"));
      maxInput.addEventListener("input", () => sanitizeBounds("max"));

      enableButton.addEventListener("click", () => {
        ensureAudioChain();
        gainNode.gain.setValueAtTime(0.18, audioContext.currentTime);
        enableButton.disabled = true;
        enableButton.textContent = "Audio Ready";
        log("Audio output enabled by user");
      });

      document.addEventListener("visibilitychange", () => {
        if (document.visibilityState === "hidden" && audioContext) {
          audioContext.resume().catch(() => {});
        }
      });

      window.addEventListener("focus", () => {
        if (audioContext && audioContext.state === "suspended") {
          audioContext.resume().catch(() => {});
        }
      });

      function connectWebSocket() {
        const protocol = location.protocol === "https:" ? "wss" : "ws";
        websocket = new WebSocket(`${protocol}://${location.hostname}:${81}/`);
        websocket.onopen = () => {
          connectionEl.textContent = "connected";
          connectionEl.style.color = "green";
          log("WebSocket connected");
          pushFrequency(state.desiredFrequency, { force: true });
          if (state.queuedFrequency !== null) {
            pushFrequency(state.queuedFrequency, { force: true });
          }
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
          if (!Number.isFinite(freq)) {
            log(`Ignored non-numeric payload: ${event.data}`);
            return;
          }
          const previous = state.lastBroadcastedFrequency;
          extendBoundsToFit(freq);
          const sanitized = clampToBounds(freq);
          state.desiredFrequency = sanitized;
          state.lastBroadcastedFrequency = sanitized;
          if (state.pendingSend) {
            clearTimeout(state.pendingSend);
            state.pendingSend = null;
          }
          state.queuedFrequency = null;
          state.applyingRemoteUpdate = true;
          slider.value = String(sanitized);
          state.applyingRemoteUpdate = false;
          playFrequency(sanitized);
          if (!Number.isFinite(previous) || previous !== sanitized) {
            log(`Frecuencia sincronizada: ${sanitized} Hz`);
          }
        };
      }

      sanitizeBounds();
      state.desiredFrequency = clampToBounds(state.desiredFrequency);
      slider.value = String(state.desiredFrequency);
      sliderValueEl.textContent = `${state.desiredFrequency} Hz`;

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
constexpr uint16_t kFrequencyMin = 60;   // Guard rails for inbound frequency commands
constexpr uint16_t kFrequencyMax = 5000; // Upper bound to avoid abusive values
uint16_t lastBroadcastFrequency = toneFrequencies[0];
String ipString = "0.0.0.0";
volatile uint8_t apClientCount = 0;
uint16_t connectedClientCount = 0;
bool apReady = false;

// ********** Forward declarations **********
void broadcastFrequency(uint16_t frequency, bool force = false);
void refreshDisplay();
void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
bool tryParseFrequencyCommand(const String &payload, uint16_t &frequencyOut);
size_t findClosestToneIndex(uint16_t frequency);

void refreshDisplay() {
  const String line1 = apReady ? String("AP: ") + AP_SSID : "AP: starting...";
  const String line2 = "IP: " + ipString;
  String line3 = "Tone: " + lastFrequencyStr + " Hz";
  if (apClientCount > 0 || connectedClientCount > 0) {
    line3 += " C:" + String(apClientCount) + "/" + String(connectedClientCount);
  }
  display.showLines(line1, line2, line3);
}

void broadcastFrequency(uint16_t frequency, bool force) {
  if (!force && frequency == lastBroadcastFrequency) {
    return;
  }
  lastBroadcastFrequency = frequency;
  lastFrequencyStr = String(frequency);
  webSocket.broadcastTXT(lastFrequencyStr);
  refreshDisplay();
}

bool tryParseFrequencyCommand(const String &payload, uint16_t &frequencyOut) {
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
    if (key != "freq" && key != "frequency") {
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
  if (!(parsed > 0.0f)) {
    return false;
  }

  uint16_t frequencyCandidate = static_cast<uint16_t>(parsed + 0.5f);
  if (frequencyCandidate < kFrequencyMin) {
    frequencyCandidate = kFrequencyMin;
  } else if (frequencyCandidate > kFrequencyMax) {
    frequencyCandidate = kFrequencyMax;
  }

  frequencyOut = frequencyCandidate;
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
      } else {
        uint16_t requestedFrequency = 0;
        if (tryParseFrequencyCommand(payloadStr, requestedFrequency)) {
          currentToneIndex = findClosestToneIndex(requestedFrequency);
          broadcastFrequency(requestedFrequency);
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

  broadcastFrequency(toneFrequencies[currentToneIndex], true);
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
