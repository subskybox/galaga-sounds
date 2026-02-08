// Pacman/Galaga PIC18 controller project for Arduino Uno
// Commands:
//   NN      -> Galaga sound NN
//   eNN     -> Pacman effect NN (gameSelectPin HIGH, extraPin LOW)
//   wNN     -> Pacman wave NN   (gameSelectPin HIGH, extraPin HIGH)
//   `       -> Stop Sound (0x1F)
//   r       -> Reset PIC

// Galaga sound selector pins (Arduino -> PIC18F4520 RC0–RC4)
const uint8_t selectorPins[] = {2, 3, 4, 5, 6};
const uint8_t NUM_SELECTOR_PINS = sizeof(selectorPins) / sizeof(selectorPins[0]);

// Control pins
const uint8_t triggerPin      = 11;
const uint8_t extraPin        = 10; // RC7 equivalent (effects vs waves)
const uint8_t resetPin        = 9;  // active-low reset
const uint8_t gameSelectPin   = 8;  // HIGH = Pacman, LOW = Galaga

// Galaga sound descriptions (index = sound number)
const char* galagaNames[] = {
  "Unused",                         // 0
  "Hit Capture Ship",               // 1
  "Hit Red Ship",                   // 2
  "Hit Blue Ship",                  // 3
  "Hit Green Ship",                 // 4
  "Tractor Beam Attempt",           // 5
  "Tractor Beam Retracting Ship",   // 6
  "Fighter Captured Destroyed",     // 7
  "Coin",                           // 8
  "Fighter Captured",               // 9
  "Free Life",                      // 10
  "Intro",                          // 11
  "Name Entry",                     // 12
  "Challenging Stage Begin",        // 13
  "Challenging Stage Over",         // 14
  "Shoot Laser",                    // 15
  "Game Over or High Score",        // 16
  "Ship Rescued",                   // 17
  "Free Life",                      // 18
  "Capture Ship Decending",         // 19
  "Challenging Stage Perfect",      // 20
  "Stage Indicator",                // 21
  "Unknown"                         // 22
};

const char* pacEffectNames[] = {
  "Unused",                         // 0
  "High Score or Free Life",        // 1
  "Waka or Coin",                   // 2
  "Pacman Siren 1",                 // 3
  "Pacman Siren 2",                 // 4
  "Pacman Siren 3",                 // 5
  "Pacman Siren 4",                 // 6
  "Pacman Siren 5",                 // 7
  "Mrs. Pacman Ghosts Scared",      // 8
  "Mrs. Pacman Eyes Return",        // 9
  "Waka1",                          // 10
  "Waka2",                          // 11
  "Pacman Eat Fruit",               // 12
  "Pacman Eat Ghost",               // 13
  "Pacman Dies",                    // 14
  "Mrs. Pacman Eat Dot 1",          // 15
  "Mrs. Pacman Eat Dot 2",          // 16
  "Mrs. Pacman Siren 3",            // 17
  "Mrs. Pacman Siren 4",            // 18
  "Mrs. Pacman Siren 5",            // 19
  "Pacman Ghosts Scared",           // 20
  "Pacman Eyes Return",             // 21
  "Eat Dot",                        // 22
  "Mrs. Pacman Eat Ghost",          // 23
  "Mrs. Pacman Dies",               // 24
  "Unknown",                        // 25
  "Unknown Sound",                  // 26
  "Unknown",                        // 27
  "Unknown",                        // 28
  "Unknown",                        // 29
  "Unknown",                        // 30
  "Unknown"                         // 31
};

const char* pacWaveNames[] = {
  "Pacman Start","Pacman Intro","Pacman Act 1","Pacman Act 2",
  "Mrs. Pacman Act 1","Mrs. Pacman Act 2","Mrs. Pacman Act 3","Modem?"
};

enum class Mode : uint8_t { Galaga, PacEffect, PacWave };

static void setSelector(uint8_t value) {
  for (uint8_t bit = 0; bit < 5; bit++) {
    digitalWrite(selectorPins[bit], (value >> bit) & 0x01);
  }
}

static void clearSelector() {
  for (uint8_t i = 0; i < NUM_SELECTOR_PINS; i++) {
    digitalWrite(selectorPins[i], LOW);
  }
}

static void pulseTrigger() {
  digitalWrite(triggerPin, HIGH);
  delay(50);
  digitalWrite(triggerPin, LOW);
}

static void flushIncoming() {
  // Drain anything leftover (spaces, CR/LF, extra chars)
  unsigned long t0 = millis();
  while (millis() - t0 < 30) {
    while (Serial.available()) {
      Serial.read();
      t0 = millis(); // extend window while data is still arriving
    }
  }
}

static void printTriggered(Mode mode, uint8_t value) {
  Serial.print("Triggered ");

  if (mode == Mode::Galaga) {
    Serial.print("Galaga sound ");
    Serial.print(value);

    if (value < (sizeof(galagaNames) / sizeof(galagaNames[0])) && galagaNames[value][0] != '\0') {
      Serial.print(" (");
      Serial.print(galagaNames[value]);
      Serial.print(")");
    }
  } else if (mode == Mode::PacEffect) {
    Serial.print("Pacman effect ");
    Serial.print(value);
    Serial.print(" (");
    Serial.print(pacEffectNames[value]);
    Serial.print(")");
  } else {
    Serial.print("Pacman wave ");
    Serial.print(value);
    Serial.print(" (");
    Serial.print(pacWaveNames[value]);
    Serial.print(")");
  }

  Serial.println();
}

void setup() {

  // Selector pins
  for (uint8_t i = 0; i < NUM_SELECTOR_PINS; i++) {
    pinMode(selectorPins[i], OUTPUT);
    digitalWrite(selectorPins[i], LOW);
  }

  delay(2000);

  // Control pins
  pinMode(extraPin, OUTPUT);
  pinMode(triggerPin, OUTPUT);
  pinMode(resetPin, OUTPUT);
  pinMode(gameSelectPin, OUTPUT);

  digitalWrite(triggerPin, LOW);
  digitalWrite(resetPin, HIGH);     // active-low reset
  digitalWrite(extraPin, HIGH);     // default high (waves/normal)
  digitalWrite(gameSelectPin, LOW); // default Galaga

  Serial.begin(9600);

  // Make parseInt respond quickly when input has no line ending
  Serial.setTimeout(60); // ms

  Serial.println("Commands:");
  Serial.println("  NN      -> Galaga sound NN");
  Serial.println("  eNN     -> Pacman effect NN (extraPin LOW)");
  Serial.println("  wNN     -> Pacman wave NN   (extraPin HIGH)");
  Serial.println("  `       -> Stop Sound (0x1F)");
  Serial.println("  r       -> Reset PIC");
}

void loop() {
  static char buf[16];
  static uint8_t idx = 0;

  while (Serial.available()) {
    char c = Serial.read();

    // End of line -> process command
    if (c == '\n' || c == '\r') {
      if (idx == 0) return;   // empty line
      buf[idx] = '\0';
      idx = 0;

      // Trim leading spaces
      char* p = buf;
      while (*p == ' ' || *p == '\t') p++;

      // Stop all sounds
      if (*p == '`') {
        setSelector(0x1F);
        pulseTrigger();
        clearSelector();
        return;
      }

      // Reset
      if (*p == 'r' || *p == 'R') {
        Serial.println("Resetting PIC");
        digitalWrite(extraPin, HIGH);
        digitalWrite(resetPin, LOW);
        delay(30);
        digitalWrite(resetPin, HIGH);
        return;
      }

      Mode mode = Mode::Galaga;
      if (*p == 'e' || *p == 'E') {
        mode = Mode::PacEffect;
        p++;
      } else if (*p == 'w' || *p == 'W') {
        mode = Mode::PacWave;
        p++;
      }

      int value = atoi(p);
      if (value < 0 || value > 31) {
        Serial.println("Invalid input (use 0–31, or e0–e31, w0–w31)");
        return;
      }

      uint8_t sel = (uint8_t)value;

      // Apply mode -> pins
      if (mode == Mode::Galaga) {
        digitalWrite(gameSelectPin, LOW);
        digitalWrite(extraPin, HIGH);
      } else if (mode == Mode::PacEffect) {
        digitalWrite(gameSelectPin, HIGH);
        digitalWrite(extraPin, HIGH);
      } else { // PacWave
        digitalWrite(gameSelectPin, HIGH);
        digitalWrite(extraPin, LOW);
      }

      setSelector(sel);
      pulseTrigger();
      clearSelector();

      printTriggered(mode, sel);
      return;
    }

    // Collect characters (ignore overflow)
    if (idx < sizeof(buf) - 1) {
      buf[idx++] = c;
    }
  }
}
