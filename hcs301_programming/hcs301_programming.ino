#include <U8x8lib.h>
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

// This is how many words the EEPROM has (12x 16 bit addresses)
#define EEPROM_SIZE 12
// The PWM PIN for programming/reading data.
#define PWM_PIN 18
// Clock Pin (S2)
#define CLK_PIN 14
// Power PIN
#define VDD_PIN 21
// Input for programming button
#define PROGRAM_PIN 37
// Input for execute button
#define EXECUTE_PIN 38

// Configuration Struct
struct HCSConfig {
  int auto_shutoff;
  int bsl0;
  int bsl1;
  int voltage_low;
  int overflow_0;
  int overflow_1;
  int envelope_encryption;
};

uint16_t eeprom_buffer[EEPROM_SIZE]; // Buffer for EEPROM Data
uint16_t vbuffer[EEPROM_SIZE]; // Data for verification received from device

void config_eeprom_buffer(uint16_t *eeprom_buffer, uint64_t key, uint16_t sync, uint32_t serial, uint32_t seed, struct HCSConfig config) {
  uint16_t base_config = 0x0000;
  // config_eeprom makes heavy use of bitmasking so that the function is a bit more user friendly.
  // Instead of passing 4 variables for the key, just pass a 64bit uint.
  // Since the HCS301 takes the eeprom config in 16 bit chunks when writing, we store it like that inside an array of 16 bit ints

  // Configure the secret key:
  // Split the key up into 4 separate words inside the eeprom buffer
  // This is accomplished by bitmasking the 64 bit key provided as an argument
  eeprom_buffer[0] = (key & 0x000000000000ffff);          // KEY_0
  eeprom_buffer[1] = (key & 0x00000000ffff0000) >> 16;    // KEY_1
  eeprom_buffer[2] = (key & 0x0000ffff00000000) >> 32;    // KEY_2
  eeprom_buffer[3] = (key & 0xffff000000000000) >> 48;    // KEY_3
  eeprom_buffer[4] = sync;                                // SYNC
  eeprom_buffer[5] = 0x0000;                              // RESERVED

  // In the HCS301 serial numbers only use the first 28 bits. If anything is set with those first 4 bits, its an invalid serial number
  // That being said, the bits are just ignored...
  if (serial >= 0x10000000) {
    printf("Invalid Serial!");
  }

  // Take the serial number and split into 2 words by bitmask and shift.
  eeprom_buffer[6] = (serial & 0x0000ffff);               // SERIAL_0
  eeprom_buffer[7] = (serial & 0x0fff0000) >> 16;         // SERIAL_1 / Auto Shutoff (MSb)

  // If AUTO_SHUTOFF is true, set the most significant bit (31) of the serial number.
  // We can simply add 0x8000 to the since it will be set to zero because of the bitmask above
  if (config.auto_shutoff == 1) {
    eeprom_buffer[7] += 0x8000;
  }

  // Split the seed into 2 words
  eeprom_buffer[8] = (seed & 0x0000ffff);                 // SEED_0
  eeprom_buffer[9] = (seed & 0xffff0000) >> 16;           // SEED_1
  eeprom_buffer[10] = 0x0000;                             // RESERVED
  
  // Setup the base config. We mask with 0x1 so if for some reason a bad int is passed, for example 0x1001, we ignore everything else for the bitwise or.
  // Probably not necessary, but...
  base_config = ((config.overflow_0 & 0x1) << 10) | base_config;
  base_config = ((config.overflow_1 & 0x1) << 11) | base_config;
  base_config = ((config.voltage_low & 0x1) << 12) | base_config;
  base_config = ((config.bsl0 & 0x1) << 13) | base_config;
  base_config = ((config.bsl1 & 0x1) << 14) | base_config;

  // Take the first 10 bit of the serial number to set the discrimination bits per the datasheet:
  eeprom_buffer[11] = (serial & 0x03ff) | base_config;    // CONFIG
}

void print_eeprom_buffer(uint16_t *eeprom_buffer) {
  // This function just prints stuff to the serial line for debugging. Useful for looking at what will be sent in binary/hex to verify functionality.
  int val;
  int bitmask;
  char buf[64]; // Temporary buffer for our formatted text for serial output.
  for (int line_number = 0; line_number < EEPROM_SIZE; line_number++) {
    sprintf(buf, "%1.2i: 0x%.4x  ", line_number, eeprom_buffer[line_number]);
    Serial.print(buf);
    // Print the binary representation of the number
    for (int i = 15; i >= 0; i--) {
      // First, we want to mask the bit we are interested in
      bitmask = 0x01 << i;
      // Next, lets mask the bit and shift to get the value
      val = (eeprom_buffer[line_number] & bitmask) >> i;
      sprintf(buf, "%i", val);
      Serial.print(buf);
      if (i != 0) {
        Serial.print(" ");
      }
    }
    Serial.print("\n");
  }
}

void write_eeprom_buffer(uint16_t *eeprom_buffer, uint16_t *vbuffer) {
  // Set PWM to output mode
  pinMode(CLK_PIN, OUTPUT);
  pinMode(PWM_PIN, OUTPUT);
  // Default the pwm to low
  digitalWrite(PWM_PIN, LOW);
  // Enable programming mode for the HCS301
  // S2[CLK_PIN] High for 3.5MS
  digitalWrite(CLK_PIN, HIGH);
  digitalWrite(VDD_PIN, HIGH);
  // TPS for 3.5ms-4.5ms
  delayMicroseconds(3500);
  digitalWrite(PWM_PIN, HIGH);
  // TPH1 for minimum of 3.5ms
  delayMicroseconds(3500);
  digitalWrite(PWM_PIN, LOW);
  // TPH2 for 50us
  delayMicroseconds(50);
  digitalWrite(CLK_PIN, LOW);
  // TPBW for 4.0ms
  delay(4);
  for (int line_number = 0; line_number < EEPROM_SIZE; line_number++) {
    // Ensure PWM is in output mode.
    pinMode(PWM_PIN, OUTPUT);
    for (int i = 0; i <= 15; i++) {
      uint16_t val;
      // Shift the buffer line_number i bits to the right, then bitmask with 0x01 to get a 0 or 1 (LOW/HIGH) and output directly to the PWM pin.
      val = (eeprom_buffer[line_number] >> i) & 0x01;
      digitalWrite(PWM_PIN, val);
      digitalWrite(CLK_PIN, HIGH);
      delayMicroseconds(35);
      digitalWrite(CLK_PIN, LOW);
      delayMicroseconds(35);
    }
    digitalWrite(CLK_PIN, LOW);
    digitalWrite(PWM_PIN, LOW);
    // Set PWM to input mode so we can get the ack
    pinMode(PWM_PIN, INPUT);
    // TWC for 50ms
    delay(50);
  }
  // Verify Cycle.
  digitalWrite(CLK_PIN, LOW);
  digitalWrite(PWM_PIN, LOW);
  for (int line_number = 0; line_number < EEPROM_SIZE; line_number++) {
    uint16_t val = 0x0000;
    for (int i = 0; i < 16 ; i++) {
      digitalWrite(CLK_PIN, HIGH);
      delayMicroseconds(35);
      // Read from LSB first...
      val += digitalRead(PWM_PIN) << i;
      digitalWrite(CLK_PIN, LOW);
      delayMicroseconds(35);
    }
    // Write the entire word to the vbuffer
    vbuffer[line_number] = val;
  }
  // Turn off the HCS301
  delay(200);
  digitalWrite(VDD_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  pinMode(CLK_PIN, OUTPUT);     // CLK (S2)
  pinMode(PWM_PIN, OUTPUT);     // PWM
  pinMode(VDD_PIN, OUTPUT);     // VDD for HCS301
  pinMode(PROGRAM_PIN, INPUT);  // Program Button
  pinMode(EXECUTE_PIN, INPUT);  // Program Button
}

uint64_t key = 0xdeadbeefbeefdead;
uint32_t serialnumber = 0x0badfeed;
uint16_t sync = 0x0000;
uint32_t seed = 0xdaadeeee;
int btn_execute = 0;
int btn_program = 0;

void loop() {
  // This is the main loop of the HCS301 programming.
  u8x8.drawString(0, 0, "Waiting to");
  u8x8.drawString(0, 1, "program HCS301");
  btn_program = digitalRead(PROGRAM_PIN);
  btn_execute = digitalRead(EXECUTE_PIN);
  if (btn_program == HIGH) {
    int success = 1;
    u8x8.clearDisplay();
    u8x8.drawString(0, 0, "Writing EEPROM");
    u8x8.drawString(0, 1, "...");
    // Configuration for BSL/voltage/auto shutoff
    struct HCSConfig config;
    config.auto_shutoff = 1;
    config.bsl0 = 0;
    config.bsl1 = 0;
    config.voltage_low = 0;
    config.overflow_0 = 0;
    config.overflow_1 = 0;
    config.envelope_encryption = 0;
    config_eeprom_buffer(eeprom_buffer, key, sync, serialnumber, seed, config);
    Serial.print("HCS301 EEPROM layout to write:\n");
    print_eeprom_buffer(eeprom_buffer);
    Serial.print("Writing EEPROM\n");
    write_eeprom_buffer(eeprom_buffer, vbuffer);
    for (int i = 0; i < EEPROM_SIZE; i++){
      if (eeprom_buffer[i] != vbuffer[i]) {
        success = 0;
      }
    }
    Serial.print("HCS301 EEPROM verification:\n");
    print_eeprom_buffer(vbuffer);
    Serial.print("...\n");
    if (success == 0) {
      Serial.print("!! EEPROM CHECK FAILURE\n");
      u8x8.drawString(0, 2, "! CHECK FAILED");
    } else {
      Serial.print("EEPROM check OK\n");
      u8x8.drawString(0, 2, "EEPROM OK");
    }
    Serial.print("...\n");
    Serial.print("Done.\n");
    u8x8.drawString(0, 3, "Done!");
    // Increment Serial Number
    serialnumber += 1;
    delay(3000);
    u8x8.clearDisplay();
  }
  if (btn_execute == HIGH) {
    u8x8.clearDisplay();
    u8x8.drawString(0, 0, "Executing HCS301");
    pinMode(PWM_PIN, INPUT);
    delay(500);
    u8x8.drawString(0, 1, "...");
    digitalWrite(VDD_PIN, HIGH);
    digitalWrite(CLK_PIN, HIGH);
    delay(500);
    u8x8.drawString(0, 2, "...");
    delay(500);
    u8x8.drawString(0, 3, "...");
    delay(500);
    digitalWrite(VDD_PIN, LOW);
    digitalWrite(CLK_PIN, LOW);
    u8x8.drawString(0, 4, "Done!");
    delay(3000);
    u8x8.clearDisplay();
  }
  delay(100);


}
