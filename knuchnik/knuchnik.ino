// --- Menu Configuration ---
const int MENU_ITEMS_COUNT = 7;
const int VISIBLE_LINES = 3;

const char* menuItems[] = {
  "Generate Hash",
  "View Last Hash",
  "Set Hash Length",
  "About"
};

// --- State Variables ---
int8_t selector = 0;
int8_t top_line_index = 0;
long lastDebounceTime = 0;
long debounceDelay = 150;

// Global variable to store the hash
char generatedHash[65]; // 64 characters + null terminator
int hashLength = 16; // Default hash length (e.g., 16 characters)

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600)

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.display();
}

void loop() {
  // put your main code here, to run repeatedly:

}

void performAction() {
  switch (selector) {
    case 0: // "Generate Hash"
      // Call your new function to start the generation process
      generateHash();
      break;
    case 1: // "View Last Hash"
      // Display the previously generated hash on the screen
      displayLastHash();
      break;
    // ... other cases
  }
}

void generateRandomBit() {
  // Example logic inside generateRandomBit()
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp); // Get new sensor events

  // Get the raw accelerometer integer values
  int16_t ax = a.acceleration.x_raw;
  int16_t ay = a.acceleration.y_raw;
  int16_t az = a.acceleration.z_raw;

  // Combine the values to create a seed for our timing.
  // Using a large, unsigned type is best.
  unsigned long T = abs(ax) + abs(ay) + abs(az);

  // We can add a small base time to ensure it's never zero.
  T = T + 100; // T is now our duration in microseconds

  // Continuing inside generateRandomBit()
  unsigned long noise_accumulator = 0;
  unsigned long start_time = micros();

  // Loop for the duration T, reading the noisy pin
  while (micros() - start_time < T) {
    noise_accumulator += analogRead(A0); // A0 is our unconnected pin
  }

  // The randomness is in the LSB of the final sum.
  // Use the bitwise AND operator to get the last bit.
  int random_bit = noise_accumulator & 1; // Result is 0 or 1

  return random_bit;
}


void generateHash() {
  // 1. Show a "Generating..." message on the display
  display.clearDisplay();
  display.setCursor(0, 10);
  display.print("Generating...");
  display.display();

  // 2. Clear the old hash string
  memset(generatedHash, 0, sizeof(generatedHash));

  // 3. Loop to generate each character of the hash
  for (int i = 0; i < hashLength; i++) {
    int8_t one_char = 0;
    // To make one character, we need 8 bits
    for (int j = 0; j < 8; j++) {
      one_char = one_char << 1; // Shift bits to the left
      int bit = generateRandomBit();
      one_char = one_char | bit; // Add the new random bit
    }
    // Add the character to the hash string
    // Note: We add 32 to avoid unprintable characters
    generatedHash[i] = (char)(one_char % 94 + 33);
  }

  // 4. Show the result
  displayLastHash();
}

void displayLastHash() {
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Last Hash:");
    display.setCursor(0,10);
    display.print(generatedHash);
    display.display();
}