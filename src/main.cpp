#include <Arduino.h>
#include <U8g2lib.h>

// OLED Display class
class OledDisplay {
private:
  U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2;

public:
  OledDisplay() : u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5) {
    // Constructor initializes with SDA=5, SCL=6
  }

  void begin() {
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
  }

  void displayText(const char* text, uint8_t x, uint8_t y) {
    u8g2.clearBuffer();
    u8g2.drawStr(x, y, text);
    u8g2.sendBuffer();
  }

  void displayTextAtLine(const char* text, uint8_t line) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, (line + 1) * 10, text);
    u8g2.sendBuffer();
  }

  void clear() {
    u8g2.clearBuffer();
    u8g2.sendBuffer();
  }

  void drawProgressBar(uint8_t percentage) {
    u8g2.clearBuffer();
    
    // Draw frame
    u8g2.drawFrame(0, 15, 72, 10);
    
    // Draw fill
    if (percentage > 0) {
      uint8_t fillWidth = (percentage * 70) / 100;
      u8g2.drawBox(1, 16, fillWidth, 8);
    }
    
    // Draw percentage text
    char buffer[10];
    sprintf(buffer, "%d%%", percentage);
    u8g2.drawStr(0, 10, buffer);
    
    u8g2.sendBuffer();
  }
};

// LED Controller class
class LedController {
private:
  uint8_t pin;
  bool state;

public:
  LedController(uint8_t ledPin) : pin(ledPin), state(false) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  void turnOn() {
    digitalWrite(pin, HIGH);
    state = true;
  }

  void turnOff() {
    digitalWrite(pin, LOW);
    state = false;
  }

  void toggle() {
    if (state) {
      turnOff();
    } else {
      turnOn();
    }
  }

  bool getState() const {
    return state;
  }
};

// Button class
class Button {
private:
  uint8_t pin;
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long debounceDelay;

public:
  Button(uint8_t buttonPin) : 
    pin(buttonPin), 
    lastState(HIGH), 
    currentState(HIGH),
    lastDebounceTime(0),
    debounceDelay(50) {
    pinMode(pin, INPUT_PULLUP);
  }

  bool isPressed() {
    bool reading = digitalRead(pin);
    
    // Check to see if you just pressed the button
    if (reading != lastState) {
      lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != currentState) {
        currentState = reading;
        
        // Only return true when button is pressed (LOW signal)
        if (currentState == LOW) {
          lastState = reading;
          return true;
        }
      }
    }
    
    lastState = reading;
    return false;
  }

  bool isHeld() {
    return (digitalRead(pin) == LOW);
  }
};

// Global instances
OledDisplay display;
LedController led(8);
Button button(9);

// Progress bar demo variables
uint8_t progress = 0;
bool progressDirection = true;

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println("ESP32-C3 OLED Demo Starting...");
  
  // Initialize display
  display.begin();
  display.displayText("Hello!", 0, 10);
  
  // Small delay to show initial message
  delay(1000);
  
  // Show LED status
  display.displayText(led.getState() ? "LED: ON" : "LED: OFF", 0, 10);
  Serial.println(led.getState() ? "LED is ON" : "LED is OFF");
}

void loop() {
  // Check if button was pressed
  if (button.isPressed()) {
    // Toggle LED state
    led.toggle();
    
    // Update display with LED status
    display.displayText(led.getState() ? "LED: ON" : "LED: OFF", 0, 10);
    Serial.println(led.getState() ? "LED turned ON" : "LED turned OFF");
    
    // Small delay to debounce
    delay(100);
  }
  
  // Demo progress bar animation
  display.drawProgressBar(progress);
  
  // Update progress
  if (progressDirection) {
    progress++;
    if (progress >= 100) {
      progressDirection = false;
    }
  } else {
    progress--;
    if (progress == 0) {
      progressDirection = true;
    }
  }
  
  // Small delay for animation
  delay(50);
}