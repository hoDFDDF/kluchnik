/*******************************************************************
 * True Random Number Generator (TRNG) v2.0
 *
 * Implements a gated hardware counter TRNG on an ESP32.
 * This version uses the lightweight AESLib by Matej Sychra.
 *
 * --- ARCHITECTURE ---
 * 1. Entropy Source 1: An MPU-6050 provides 6-axis motion data from user shakes.
 * 2. Entropy Source 2: An external high-frequency hardware oscillator.
 * 3. Gating: The ESP32 uses motion data to randomly open/close a gate
 * between the oscillator and a hardware counter.
 * 4. Key Generation: The final value on the counter is a random byte. This
 * is repeated to form a 128-bit key.
 * 5. Security: The generated key is encrypted with AES-128 before being sent to a PC.
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
#include <AESLib.h> // Using the AESLib library by Matej Sychra

// --- Pin Definitions (CONFIGURE FOR YOUR ESP32 SETUP) ---
// I2C Pins for Display and MPU6050 are default (SDA=21, SCL=22)

// --- Input/Output Pins ---
#define BUTTON_UP     13 // GPIO for the 'Up' button
#define BUTTON_DOWN   12 // GPIO for the 'Down' button
#define BUTTON_SELECT 14 // GPIO for the 'Select' button

#define GATE_CONTROL_PIN 27 // This pin opens/closes the gate to the counter
#define COUNTER_RESET_PIN 26 // This pin resets the 8-bit counter to zero

// --- CORRECTED PIN DEFINITIONS FOR COUNTER ---
// Define 8 pins to read the counter's value. These pins avoid I2C (21, 22)
// and other assigned pins. Always check your specific ESP32 board's pinout.
const int counterPins[8] = {4, 5, 15, 16, 17, 18, 19, 23}; // D0 to D7

// --- Display & MPU Setup ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MPU6050 mpu;

// --- Menu Configuration ---
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
bool useSpecialSymbols = true;

// --- Cryptography ---
// This key MUST match the key on the PC-side decryption software.
// It is used to encrypt the newly generated random key before transmission.
uint8_t encryptionKey[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};

// --- Global Key Storage ---
byte generatedKey[16]; // Stores the 128-bit key from the TRNG

/*========================================================================*/
/* SETUP                                                                  */
/*========================================================================*/
void setup() {
  Serial.begin(115200);
  Wire.begin();

  // --- Initialize Pins ---
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(GATE_CONTROL_PIN, OUTPUT);
  pinMode(COUNTER_RESET_PIN, OUTPUT);
  digitalWrite(GATE_CONTROL_PIN, LOW); // Ensure gate is closed initially
  for (int i = 0; i < 8; i++) {
    pinMode(counterPins[i], INPUT);
  }

  // --- Initialize Display ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  // --- Initialize MPU6050 ---
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

/**
 * @brief Reads the 8 parallel data lines from the hardware counter.
 * @return The 8-bit value (byte) from the counter.
 */
byte readCounter() {
  byte value = 0;
  for (int i = 0; i < 8; i++) {
    if (digitalRead(counterPins[i]) == HIGH) {
      value |= (1 << i);
    }
  }
  return value;
}

/**
 * @brief Generates a single truly random byte.
 * @return A random 8-bit value.
 */
byte generateRandomByte() {
  int16_t ax, ay, az, gx, gy, gz;

  // Reset the external counter to 0
  digitalWrite(COUNTER_RESET_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(COUNTER_RESET_PIN, LOW);

  // Run the gating process for a fixed duration (e.g., 200ms)
  unsigned long startTime = millis();
  while (millis() - startTime < 200) {
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    long motionEnergy = abs(ax) + abs(ay) + abs(az) + abs(gx) + abs(gy) + abs(gz);
    unsigned long gateTime = (motionEnergy % 100) + 10; // Unpredictable time in us

    digitalWrite(GATE_CONTROL_PIN, HIGH); // Open gate
    delayMicroseconds(gateTime);
    digitalWrite(GATE_CONTROL_PIN, LOW);  // Close gate
    delayMicroseconds(10); // Small fixed delay to ensure gate timing is not too fast
  }

  return readCounter();
}

/**
 * @brief Main workflow to generate, encrypt, and send the password data.
 */
void runPasswordGeneration() {
  display.clearDisplay();
  display.setCursor(10, 5);
  display.print("Shaking device to");
  display.setCursor(25, 15);
  display.print("gather entropy...");
  display.display();

  // 1. Generate the 128-bit (16-byte) random key
  for (int i = 0; i < 16; i++) {
    generatedKey[i] = generateRandomByte();
  }

  // 2. Encrypt the generated key IN-PLACE using AESLib.
  // The 'generatedKey' buffer is overwritten with the ciphertext.
  aes128_enc_single(encryptionKey, generatedKey);

  // 3. Send the encrypted data (which is now in the generatedKey buffer) to PC
  sendToPC(generatedKey);
  
  display.clearDisplay();
  display.setCursor(35, 12);
  display.print("Data sent!");
  display.display();
  delay(2000);
}

/**
 * @brief Sends the final encrypted key and settings to the PC over Serial.
 */
void sendToPC(byte* dataToSend) {
  Serial.print("LEN:");
  Serial.print(passwordLength);
  Serial.print(",COMPLEX:");
  Serial.print(useSpecialSymbols ? "1" : "0");
  Serial.print(",KEY:");
  for (int i = 0; i < 16; i++) {
    if (dataToSend[i] < 16) Serial.print("0");
    Serial.print(dataToSend[i], HEX);
  }
  Serial.println();
}


/*========================================================================*/
/* UTILITY & UI FUNCTIONS                                                 */
/*========================================================================*/
void chooseLength() {
  bool setting = true;
  while (setting) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (digitalRead(BUTTON_UP) == LOW) {
        passwordLength++;
        if (passwordLength > 64) passwordLength = 64;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_DOWN) == LOW) {
        passwordLength--;
        if (passwordLength < 8) passwordLength = 8;
        lastDebounceTime = millis();
      }
      if (digitalRead(BUTTON_SELECT) == LOW) {
        setting = false;
        lastDebounceTime = millis();
      }
    }
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Set Length (8-64)");
    display.setCursor(0, 12);
    display.print("(Press Select to OK)");
    display.setTextSize(2);
    display.setCursor(50, 16);
    display.print(passwordLength);
    display.setTextSize(1);
    display.display();
  }
  delay(200);
}

void chooseComplexity() {
    useSpecialSymbols = !useSpecialSymbols; // Toggle on each entry
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Special Symbols?");
    display.setTextSize(2);
    display.setCursor(15, 15);
    display.print(useSpecialSymbols ? "ENABLED" : "DISABLED");
    display.setTextSize(1);
    display.display();
    delay(1500);
}

void displayAbout() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("TRNG v2.1-AESLib");
  display.setCursor(0, 10);
  display.print("Gated Counter Arch.");
  display.setCursor(0, 20);
  display.print("Press any key...");
  display.display();
  delay(500);
  while (digitalRead(BUTTON_SELECT) == HIGH && digitalRead(BUTTON_UP) == HIGH && digitalRead(BUTTON_DOWN) == HIGH);
}
