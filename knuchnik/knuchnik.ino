/*******************************************************************
 * True Random Number Generator (TRNG) v2.2 - Advanced UI
 *
 * Implements a gated hardware counter TRNG on an ESP32.
 * This version features an advanced UI with multi-level menus.
 * It uses the lightweight AESLib by Matej Sychra for encryption.
 *
 * --- ARCHITECTURE ---
 * 1. Entropy Source 1: MPU-6050 motion data.
 * 2. Entropy Source 2: External high-frequency hardware oscillator.
 * 3. Gating: ESP32 uses motion data to randomly gate the oscillator signal.
 * 4. Key Generation: An external 8-bit counter captures the gated pulses.
 * 5. Security: The final 128-bit key is encrypted with AES-128.
 *
 * --- HARDWARE REQUIRED ---
 * - ESP32 Development Board
 * - MPU-6050 Accelerometer/Gyroscope
 * - SSD1306 I2C OLED Display (128x32)
 * - 3x Push Buttons (Up, Down, Select)
 * - External Hardware:
 * - Signal Generator (~100 kHz)
 * - Digital Gate (e.g., 74HC08 AND gate or N-Channel MOSFET)
 * - 8-bit Counter (e.g., 74HC590) with reset capability
 *******************************************************************/

// --- Core Libraries ---
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "I2Cdev.h"
#include "MPU6050.h"
#include <AESLib.h>

// --- Pin Definitions ---
#define BUTTON_UP     13
#define BUTTON_DOWN   12
#define BUTTON_SELECT 14
#define GATE_CONTROL_PIN 27
#define COUNTER_RESET_PIN 26
const int counterPins[8] = {4, 5, 15, 16, 17, 18, 19, 23};

// --- Display & MPU Setup ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MPU6050 mpu;

// --- Main Menu Configuration ---
const int MENU_ITEMS_COUNT = 4;
const char* menuItems[MENU_ITEMS_COUNT] = {
  "Generate Password",
  "Set Length",
  "Set Complexity",
  "About"
};

// --- State Variables ---
int8_t selector = 0;
int8_t top_line_index = 0;
long lastDebounceTime = 0;
long debounceDelay = 200;

// --- Password Generation Settings ---
int passwordLength = 16;
// Complexity levels for the PC to interpret
enum Complexity {
  NUMBERS_ONLY,
  LOWERCASE_ONLY,
  UPPERCASE_ONLY,
  LOWER_UPPER,
  LOWER_UPPER_NUM,
  ALL_CHARS
};
int complexityLevel = ALL_CHARS; // Default to max complexity
const char* complexityNames[] = {
  "Numbers", "Lowercase", "Uppercase", "Letters", "Alphanumeric", "All Symbols"
};

// --- Cryptography ---
uint8_t encryptionKey[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
byte generatedKey[16];

/*========================================================================*/
/* SETUP                                                                  */
/*========================================================================*/
void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(GATE_CONTROL_PIN, OUTPUT);
  pinMode(COUNTER_RESET_PIN, OUTPUT);
  digitalWrite(GATE_CONTROL_PIN, LOW);

  for (int i = 0; i < 8; i++) {
    pinMode(counterPins[i], INPUT);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  mpu.initialize();
  if (!mpu.testConnection()) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("MPU6050 Failed!");
    display.display();
    for (;;);
  }

  display.clearDisplay();
  display.display();
}

/*========================================================================*/
/* MAIN LOOP                                                              */
/*========================================================================*/
void loop() {
  handleInput();
  drawMenu();
}

/*========================================================================*/
/* MENU & INPUT HANDLING                                                  */
/*========================================================================*/
void handleInput() {
  if ((millis() - lastDebounceTime) < debounceDelay) return;

  bool upPressed = (digitalRead(BUTTON_UP) == LOW);
  bool downPressed = (digitalRead(BUTTON_DOWN) == LOW);
  bool selectPressed = (digitalRead(BUTTON_SELECT) == LOW);

  if (upPressed) {
    selector--;
    if (selector < 0) selector = MENU_ITEMS_COUNT - 1;
  }
  if (downPressed) {
    selector++;
    if (selector >= MENU_ITEMS_COUNT) selector = 0;
  }

  if (selector < top_line_index) top_line_index = selector;
  if (selector >= top_line_index + 3) top_line_index = selector - 2;

  if (selectPressed) {
    performAction();
  }

  if (upPressed || downPressed || selectPressed) {
    lastDebounceTime = millis();
  }
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  for (int i = 0; i < 3; i++) {
    int item_index = top_line_index + i;
    if (item_index < MENU_ITEMS_COUNT) {
      display.setCursor(10, i * 10);
      display.print(menuItems[item_index]);
    }
  }

  int selector_y_pos = (selector - top_line_index) * 10;
  display.setCursor(0, selector_y_pos);
  display.print(">");
  display.display();
}

void performAction() {
  switch (selector) {
    case 0:
      runPasswordGeneration();
      break;
    case 1:
      chooseLength();
      break;
    case 2:
      chooseComplexity();
      break;
    case 3:
      displayAbout();
      break;
  }
}

/*========================================================================*/
/* CORE TRNG & CRYPTO LOGIC                                               */
/*========================================================================*/
byte readCounter() {
  byte value = 0;
  for (int i = 0; i < 8; i++) {
    if (digitalRead(counterPins[i]) == HIGH) {
      value |= (1 << i);
    }
  }
  return value;
}

byte generateRandomByte() {
  int16_t ax, ay, az, gx, gy, gz;
  digitalWrite(COUNTER_RESET_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(COUNTER_RESET_PIN, LOW);

  unsigned long startTime = millis();
  while (millis() - startTime < 200) {
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    long motionEnergy = abs(ax) + abs(ay) + abs(az) + abs(gx) + abs(gy) + abs(gz);
    unsigned long gateTime = (motionEnergy % 100) + 10; // формула для рандома
    digitalWrite(GATE_CONTROL_PIN, HIGH);
    delayMicroseconds(gateTime);
    digitalWrite(GATE_CONTROL_PIN, LOW);
    delayMicroseconds(10);
  }
  return readCounter();
}

void runPasswordGeneration() {
  display.clearDisplay();
  display.setCursor(10, 5);
  display.print("Shaking device to");
  display.setCursor(25, 15);
  display.print("gather entropy...");
  display.display();

  for (int i = 0; i < 16; i++) {
    generatedKey[i] = generateRandomByte();
  }
  aes128_enc_single(encryptionKey, generatedKey);
  sendToPC(generatedKey);
  
  display.clearDisplay();
  display.setCursor(35, 12);
  display.print("Data sent!");
  display.display();
  delay(2000);
}

void sendToPC(byte* dataToSend) {
  Serial.print("LEN:");
  Serial.print(passwordLength);
  Serial.print(",COMPLEX:");
  Serial.print(complexityLevel); // Send the integer complexity level
  Serial.print(",KEY:");
  for (int i = 0; i < 16; i++) {
    if (dataToSend[i] < 16) Serial.print("0");
    Serial.print(dataToSend[i], HEX);
  }
  Serial.println();
}

/*========================================================================*/
/* ADVANCED UI FUNCTIONS                                                  */
/*========================================================================*/
void chooseLength() {
  bool setting = true;
  while (setting) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (digitalRead(BUTTON_UP) == LOW) { // UP = +1
        passwordLength++;
        if (passwordLength > 64) passwordLength = 64;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_DOWN) == LOW) { // DOWN = -1
        passwordLength--;
        if (passwordLength < 8) passwordLength = 8;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_SELECT) == LOW) { // SELECT = SAVE
        setting = false;
        lastDebounceTime = millis();
      }
    }
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Set Length (8-64)");
    display.setCursor(0, 12);
    display.print("Up/Down=+1/-1 Sel=OK");
    display.setTextSize(2);
    display.setCursor(50, 16);
    display.print(passwordLength);
    display.setTextSize(1);
    display.display();
  }
  delay(200);
}

void chooseComplexity() {
  bool setting = true;
  int tempSelector = complexityLevel;
  int numComplexityLevels = sizeof(complexityNames) / sizeof(char*);

  while (setting) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (digitalRead(BUTTON_UP) == LOW) {
        tempSelector--;
        if (tempSelector < 0) tempSelector = numComplexityLevels - 1;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_DOWN) == LOW) {
        tempSelector++;
        if (tempSelector >= numComplexityLevels) tempSelector = 0;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_SELECT) == LOW) {
        complexityLevel = tempSelector; // Save selection
        setting = false;
        lastDebounceTime = millis();
      }
    }
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Set Complexity");
    display.setCursor(0, 12);
    display.print("Up/Down=Change Sel=OK");
    display.setTextSize(1);
    display.setCursor(20, 22);
    display.print(complexityNames[tempSelector]);
    display.display();
  }
  delay(200);
}

void displayAbout() {
  const int aboutPagesCount = 4;
  const char* aboutTitles[] = {"< Back", "Generate", "Set Length", "Set Complexity"};
  int aboutSelector = 1; // Start on the first topic, not "< Back"
  bool inAboutMenu = true;

  while(inAboutMenu) {
    // Handle navigation in the about menu
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (digitalRead(BUTTON_UP) == LOW) {
        aboutSelector--;
        if (aboutSelector < 0) aboutSelector = aboutPagesCount - 1;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_DOWN) == LOW) {
        aboutSelector++;
        if (aboutSelector >= aboutPagesCount) aboutSelector = 0;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_SELECT) == LOW) {
        if (aboutSelector == 0) { // Selected "< Back"
          inAboutMenu = false;
        } else {
          // Show the detailed help text for the selected topic
          display.clearDisplay();
          display.setCursor(0,0);
          display.print(aboutTitles[aboutSelector]);
          display.drawFastHLine(0, 9, 128, WHITE);
          
          switch(aboutSelector) {
            case 1: // Generate
              display.setCursor(0,12);
              display.print("Shake device to add");
              display.setCursor(0,22);
              display.print("randomness.");
              break;
            case 2: // Set Length
              display.setCursor(0,12);
              display.print("Up/Down to change.");
              display.setCursor(0,22);
              display.print("Select to save.");
              break;
            case 3: // Set Complexity
              display.setCursor(0,12);
              display.print("Select from a list");
              display.setCursor(0,22);
              display.print("of char sets.");
              break;
          }
          display.display();
          delay(500); // Wait for user to release button
          while(digitalRead(BUTTON_SELECT) == HIGH); // Wait for another press to go back
        }
        lastDebounceTime = millis();
      }
    }
    
    // Draw the about menu
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("-- About --");
    for(int i = 0; i < aboutPagesCount; i++) {
        display.setCursor(10, 12 + (i*8)); // Simple list, no scrolling needed for 4 items
    }
    display.setCursor(0, 12 + (aboutSelector*8));
    display.print(">");
    display.setCursor(10, 12); display.print(aboutTitles[0]);
    display.setCursor(10, 20); display.print(aboutTitles[1]);
    // Only show 2 topics at a time below the title
    display.display();
  }
  delay(200);
}
