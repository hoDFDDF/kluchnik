
//ESP32_counter
#include <Arduino.h>
#define FRONT_COUNTER 14
volatile uint8_t counter;

void IRAM_ATTR onRise(){
  counter++;
}
void setup() {
  //Serial.begin(9600);
  pinMode(FRONT_COUNTER, INPUT);
   attachInterrupt(FRONT_COUNTER,onRise,RISING);  
}
void loop() { 
  delay(100);
   //Serial.println(counter);
}