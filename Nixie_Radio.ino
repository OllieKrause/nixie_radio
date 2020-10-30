/*
  Nixie Radio Firmware v1.0
  Author: Ollie K
  License: Creative Commons Non Commercial
  Last updated: October 2020
  /*

  /*
  THINGS TO ADD/QUESTIONS
  -WHEN TO USE DEFINE AND WHEN TO USE CONSTINT
  -MOVE ALL DEFINES AND CONST INTS TO A SEPARATE HEADER FILE
*/

// Libraries
#include <Wire.h>
#include "TEA5767.h"
#include <EEPROM.h>
#include <SPI.h>

// Rotary encoder inputs
#define encoderA 10
#define encoderB 11
#define encoderSW 12

// Number of nixie tubes (number of digits)
#define digits 4

// Nixie tube pins
#define decimalPin 49

uint8_t nixiePinArray[] = {30, 31, 32, 33, 34, 35, 36, 37, 22, 23, 24, 25, 26, 27, 28, 29}; // Array of nixie tube pins. Must be in A1, B1, C1, D1, A2, B2, etc order.

bool nixieEnable = false; // Set to true to turn on nixie tubes

TEA5767 Radio; // Create instance of radio

// Bool to determine if the user is adjusting the frequency or volume
bool inputState = true; // true -> changing frequency | false -> changing volume

// Rotary encoder global variables
int currentStateA;
int lastStateA;
unsigned long lastButtonPress = 0;
bool encoderDirection; // 0 -> CW 1 -> CCW

// TEA5767 FM radio global variables
const uint16_t minFrequency = 880; // Minimum frequency as an integer (top end reset)
const uint16_t maxFrequency = 1070; // Maximum frequency as an integer (bottom end reset)

// Data stored to EEPROM - will eventually use a struct and wear leveling
uint16_t frequency = 1055; // Frequency as an integer to simplify bit math. Divided by 10 when sent to radio.
uint8_t volume = 100; // Volume as a number from 0-100

int eeAddress = 0; // EEPROM address where EEPROMdata struct will be stored

#define potCs 53 // MCP42100 chip select pin

// Configure SPI settings
SPISettings spiSettings(20000000, MSBFIRST, SPI_MODE0);

/*
   --SETUP FUNCTION--
   CONFIGURES I/O
   RESTORES EEPROM VALUES
*/
void setup() {
  Serial.begin(9600); // Setup serial monitor

  Wire.begin(); // Enable I2C communication

  SPI.begin(); // Enable SPI communication

  Radio.init(); // Initialize TEA5767 radio & mute the voliume

  Serial.println(F("----- Nixie Radio v1.1 -----")); // Write version number to serial monitor on bootup

  frequency = EEPROM.get(eeAddress, frequency);  // Read frequency from EEPROM

  volume = 50; // Arbitrary default volume level to check mute features until I get it properly stored in EEPROM

  frequency++; // Add one to frequency because EEPROM always seems to record frequency-1

  Serial.print(F("EEPROM recovered. ")); // Print recovered frequency

  muteHard(); // Mute radio while radio changes frequency

  changeFrequency(); // Tune radio to recovered frequency

  pinMode(encoderA, INPUT); // Set encoder pins as inputs
  pinMode(encoderB, INPUT);
  pinMode(encoderSW, INPUT_PULLUP);

  pinMode(potCs, OUTPUT); // Configure pot chip select as output
  digitalWrite(potCs, HIGH); // Pot chip select default to de-selected

  // Define nixie tube pins as outputs
  for (int i = 0; i < (digits * 4) - 1; i++) {
    pinMode(nixiePinArray[i], OUTPUT);
  }

  pinMode(decimalPin, OUTPUT); // Define decimal (INS-1) pin as an output

  lastStateA = digitalRead(encoderA); // Read the initial state of encoderA

  nixieEnable = true;

  setNixie(0000); // Write nixie tubes to 0 while booting

  delay (4000); // Delay 2 seconds to allow radio to tune into last station.

  unmuteSoft(); // Soft unmute radio now that it's tuned into the proper station

  setNixie(frequency); // Set nixies to recovered frequency

  Serial.println(F("Setup complete"));
}

/*
   --MAIN LOOP FUNCTION--
   CALLS "readEncoder" function
*/

void loop() {
  readEncoder(); // Read encoder position and update respective values
}

/*
  --READ ENCODER FUNCTION--
  READS ENCODER ROTATION DIRECTION AND BUTTON STATE
  CHECKS USER INPUT STATE (VOLUME OR FREQUENCY)
  CALLS "changeFrequency" AND "changeVolume" FUNCTIONS
*/

void readEncoder() {
  currentStateA = digitalRead(encoderA); // Read the current state of encoderA

  // If last and current state of encoderA are different, then pulse occurred. React to only 1 state change to avoid double count
  if (currentStateA != lastStateA  && currentStateA == 1) {
    // If the encoderB is different than the encoderA state then the encoder is rotating CW
    if (digitalRead(encoderB) != currentStateA) {
      encoderDirection = 1;
    }
    else {
      encoderDirection = 0;
    }

    // Check user's input mode
    if (inputState) {
      changeFrequency(); // Change frequency
    }
    else {
      changeVolume(); // Change frequency
    }
  }

  lastStateA = currentStateA; // Remember last CLK state

  int btnState = digitalRead(encoderSW); // Read the button state

  //If detect LOW signal, button is pressed
  if (btnState == LOW) {
    //if at least 50ms have passed since last LOW pulse, button has been pressed and released
    if (millis() - lastButtonPress > 50) {

      // Change to opposite input state
      if (inputState) {
        inputState = false;
      }
      else {
        inputState = true;
      }

      Serial.print(F("Input state changed: "));
      Serial.println((inputState ? "Frequency" : "Volume"));

    }
    lastButtonPress = millis(); // Remember last button press event
  }
  delay(1); // Small delay to debounce the reading
}

/*
   --CHANGE FREQUENCY FUNCTION--
   TAKES ENCODER DIRECTION AS INPUT
   INCREASES OR DECREASES FREQUENCY
   SENDS FREQUENCY TO "void setNixie"
*/

void changeFrequency() {
  // Increase frequency if encoder is turned CW
  if (encoderDirection) {
    if (frequency <= maxFrequency) {
      frequency = frequency + 1; // Increase frequency by 1/10MHz (1 because divided by 10 when sent to radio)
    }
    else {
      frequency = minFrequency;
      Serial.println(F("Top end reset"));
    }
  }

  // Decrease frequency if encoder is turned CW
  else {
    if (frequency >= minFrequency) {
      frequency = frequency - 1; // Decrease frequency by 1/10MHz (1 because divided by 10 when sent to radio)
    }
    else {
      frequency = maxFrequency;
      Serial.println(F("Bottom end reset"));
    }
  }

  // Set radio frequency
  Radio.set_frequency((float)frequency / 10.0);

  // Write current frequency to serial monitor
  Serial.print(F("Frequency: "));
  Serial.println((float)frequency / 10.0);

  // Write current radio frequency to EEPROM
  EEPROM.put(eeAddress, frequency);

  setNixie(frequency); // Call function to set nixie tubes
}

/*
   --CHANGE VOLUME FUNCTION--
   TAKES ENCODER DIRECTION AS INPUT
   INCREASES OR DECREASES VOLUME
   SENDS VOLUME TO "void setNixie"
*/

void changeVolume () {
  if (encoderDirection) {
    if (volume < 100) {
      volume ++;
    }
    else {
      volume = 100;
    }
  }

  if (!encoderDirection) {
    if (volume > 0) {
      volume --;
    }
    else {
      volume = 0;
    }
  }

  uint8_t volumeActual = map(volume, 0, 100, 255, 0); // Local volume variable remapped for digital pot input range

  SPI.beginTransaction(spiSettings); // Begin SPI transaction
  digitalWrite(potCs, LOW); // Select pot chip
  SPI.transfer(0b00010011); // Select both pots as address and enter write data mode
  SPI.transfer(volumeActual); // Send volume to pot
  digitalWrite(potCs, HIGH); // Deselect pot chip
  SPI.endTransaction(); // End SPI transaction

  // Write current frequency to serial monitor
  Serial.print(F("Volume: "));
  Serial.println(volume);

  //EEPROM.put(eeAddress + sizeof(frequency), volume); // Write volume to EEPROM in the bits right after

  setNixie((unsigned int)volume * 10); // Call function to set nixie tubes. Multiply volume by 10 to shift over one decimal place.

}


/*
   --SET NIXIE FUNCTION--
  TAKES A NUMBER AS AN INPUT
  SEPARATES NUMBER INTO BYTE ARRAY
  EXTRACTS 0-9 BITS FROM EACH BYTE AND SENDS TO NIXIE TUBES
*/

void setNixie(uint16_t nixieVal) {
  uint8_t digitArray[digits]; // Separate number into individual digits within an array.
  for (int i = digits - 1; i >= 0; i--) { // Array is zero indexing so start at digits - 1.
    digitArray[i] = nixieVal % 10; // Modulo the number to isolate last digit
    nixieVal = floor((float) nixieVal / 10.0); // Divide number by 10 to knock off the last digit
  }

  if (nixieEnable == true) { // Only write to nixies if tubes are enabled
    digitalWrite(decimalPin, HIGH); // Turn on decimal (INS-1)
    for (int i = 0; i < digits; i++) { // i represents each nixie tube digit
      //Serial.print(F(" | "));
      //Serial.print(digitArray[i]); // Now that frequency digits are separated into digitArray, print out each separated digit to verify the separation

      int c = 0; // Used to identify which of the four bits in each digit is being controlled

      for (int mask = 0b0001; mask < 0b1000; mask <<= 1 ) { // Use a bitmask to extract the 4 bits from each digit
        if (digitArray[i] & mask) { // Check if each bit is HIGH or LOW.
          digitalWrite(nixiePinArray[(i * 4) + c], HIGH); // Write corresponding nixie pin HIGH. i*4 because each nixie tube is driven by a nibble
        }
        else {
          digitalWrite(nixiePinArray[(i * 4) + c], LOW); // Else write corresponding nixie pin LOW.
        }
        c++; // Add to c to move on to next bit
      }
    }
  }
  else {
    for (int i = 0; i < sizeof(nixiePinArray); i++) {
      digitalWrite(nixiePinArray[i], HIGH); // Write all nixie pins high to turn off all tubes
      digitalWrite(decimalPin, LOW); // Turn off decimal (INS-1)
    }
  }
}

/*
   --HARD MUTE FUNCTION--
  IMMEDIATELY MUTES RADIO VOLUME
*/

void muteHard () {
  SPI.beginTransaction(spiSettings); // Begin SPI transaction
  digitalWrite(potCs, LOW); // Select pot chip
  SPI.transfer(0b00010011); // Select both pots as address and enter write data mode
  SPI.transfer(255); // Set maxmimum pot resistance
  digitalWrite(potCs, HIGH); // Deselect pot chip
  SPI.endTransaction(); // End SPI transaction
}

/*
   --SOFT MUTE FUNCTION--
  QUICKLY FADES OUT RADIO VOLUME
*/

void muteSoft() {
  for (int i = map(volume, 0, 100, 255, 0); i < 255; i++) {
    SPI.beginTransaction(spiSettings); // Begin SPI transaction
    digitalWrite(potCs, LOW); // Select pot chip
    SPI.transfer(0b00010011); // Select both pots as address and enter write data mode
    SPI.transfer(i); // Fade out volume
    digitalWrite(potCs, HIGH); // Deselect pot chip
    SPI.endTransaction(); // End SPI transaction
    delay(10);
  }
}

/*
   --HARD UNMUTE FUNCTION--
  IMMEDIATELY UNMUTES RADIO VOLUME
*/

void unmuteHard () {
  SPI.beginTransaction(spiSettings); // Begin SPI transaction
  digitalWrite(potCs, LOW); // Select pot chip
  SPI.transfer(0b00010011); // Select both pots as address and enter write data mode
  SPI.transfer(0); // Set minimum pot resistance
  digitalWrite(potCs, HIGH); // Deselect pot chip
  SPI.endTransaction(); // End SPI transaction
}

/*
   --SOFT UNMUTE FUNCTION--
  QUICKLY FADES IN RADIO VOLUME
*/

void unmuteSoft() {
  for (int i = 255; i > map(volume, 0, 100, 255, 0); i--) {
    SPI.beginTransaction(spiSettings); // Begin SPI transaction
    digitalWrite(potCs, LOW); // Select pot chip
    SPI.transfer(0b00010011); // Select both pots as address and enter write data mode
    SPI.transfer(i); // Fade in volume
    digitalWrite(potCs, HIGH); // Deselect pot chip
    SPI.endTransaction(); // End SPI transaction
    delay(10);
  }
}
