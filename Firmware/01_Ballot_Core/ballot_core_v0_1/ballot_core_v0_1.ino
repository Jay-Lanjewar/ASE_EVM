/*
 * A.S.E. EVM
 * Ballot Unit - Core Firmware v0.1
 *
 * Controller:
 * Arduino UNO
 *
 * Current scope:
 * - Candidate selection
 * - NOTA selection
 * - CHANGE
 * - CONFIRM
 * - LEDs
 * - Buzzer
 *
 * NOT included yet:
 * - Control Unit communication
 * - RFID
 * - EEPROM
 * - Admin system
 */

enum Selection {
  NONE,
  CANDIDATE_A,
  CANDIDATE_B,
  CANDIDATE_C,
  NOTA
};

// ---------------- PIN DEFINITIONS ----------------

// Buttons
const byte BUTTON_A      = 2;
const byte BUTTON_B      = 3;
const byte BUTTON_C      = 4;
const byte BUTTON_NOTA   = 5;
const byte BUTTON_CHANGE = 6;
const byte BUTTON_CONFIRM = 7;

// LEDs
const byte LED_A    = 8;
const byte LED_B    = 9;
const byte LED_C    = 10;
const byte LED_NOTA = 11;

// Buzzer
const byte BUZZER = 12;

// ---------------- SYSTEM VARIABLES ----------------

// Temporary development setting.
// Later the Control Unit will enable/disable voting.
bool votingEnabled = true;

Selection selected = NONE;


// ---------------- SETUP ----------------

void setup() {

  Serial.begin(9600);

  // Buttons use Arduino's internal pull-up resistors.
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);
  pinMode(BUTTON_NOTA, INPUT_PULLUP);
  pinMode(BUTTON_CHANGE, INPUT_PULLUP);
  pinMode(BUTTON_CONFIRM, INPUT_PULLUP);

  // LEDs
  pinMode(LED_A, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(LED_C, OUTPUT);
  pinMode(LED_NOTA, OUTPUT);

  // Buzzer
  pinMode(BUZZER, OUTPUT);

  clearSelection();

  Serial.println("ASE EVM - BALL0T UNIT");
  Serial.println("System ready.");
}


// ---------------- MAIN LOOP ----------------

void loop() {

  // If voting is disabled, ignore voter input.
  if (!votingEnabled) {
    return;
  }

  // Only allow candidate selection when nothing
  // is currently selected.
  if (selected == NONE) {

    if (buttonPressed(BUTTON_A)) {
      selectCandidate(CANDIDATE_A);
    }

    else if (buttonPressed(BUTTON_B)) {
      selectCandidate(CANDIDATE_B);
    }

    else if (buttonPressed(BUTTON_C)) {
      selectCandidate(CANDIDATE_C);
    }

    else if (buttonPressed(BUTTON_NOTA)) {
      selectCandidate(NOTA);
    }
  }

  // CHANGE works after a selection.
  if (selected != NONE && buttonPressed(BUTTON_CHANGE)) {
    clearSelection();

    Serial.println("Selection changed.");
    beep(2);
  }

  // CONFIRM works after a selection.
  if (selected != NONE && buttonPressed(BUTTON_CONFIRM)) {

    confirmSelection();

    // In the final system, the Control Unit
    // will decide whether this vote is accepted.
    votingEnabled = false;
  }
}


// ---------------- BUTTON FUNCTION ----------------

bool buttonPressed(byte pin) {

  if (digitalRead(pin) == LOW) {

    // Simple debounce
    delay(30);

    if (digitalRead(pin) == LOW) {

      // Wait until button is released
      while (digitalRead(pin) == LOW) {
        delay(1);
      }

      return true;
    }
  }

  return false;
}


// ---------------- SELECTION ----------------

void selectCandidate(Selection candidate) {

  selected = candidate;

  turnOffAllLEDs();

  switch (selected) {

    case CANDIDATE_A:
      digitalWrite(LED_A, HIGH);
      Serial.println("Candidate A selected.");
      break;

    case CANDIDATE_B:
      digitalWrite(LED_B, HIGH);
      Serial.println("Candidate B selected.");
      break;

    case CANDIDATE_C:
      digitalWrite(LED_C, HIGH);
      Serial.println("Candidate C selected.");
      break;

    case NOTA:
      digitalWrite(LED_NOTA, HIGH);
      Serial.println("NOTA selected.");
      break;

    default:
      break;
  }

  beep(1);
}


// ---------------- CONFIRM ----------------

void confirmSelection() {

  Serial.print("CONFIRMED: ");

  switch (selected) {

    case CANDIDATE_A:
      Serial.println("Candidate A");
      break;

    case CANDIDATE_B:
      Serial.println("Candidate B");
      break;

    case CANDIDATE_C:
      Serial.println("Candidate C");
      break;

    case NOTA:
      Serial.println("NOTA");
      break;

    default:
      Serial.println("NONE");
      return;
  }

  // Confirmation feedback
  beep(2);

  // Clear temporary selection
  clearSelection();

  Serial.println("Ballot Unit locked.");
}


// ---------------- CLEAR SELECTION ----------------

void clearSelection() {

  selected = NONE;
  turnOffAllLEDs();
}


// ---------------- LED CONTROL ----------------

void turnOffAllLEDs() {

  digitalWrite(LED_A, LOW);
  digitalWrite(LED_B, LOW);
  digitalWrite(LED_C, LOW);
  digitalWrite(LED_NOTA, LOW);
}


// ---------------- BUZZER ----------------

void beep(byte times) {

  for (byte i = 0; i < times; i++) {

    tone(BUZZER, 2000);
    delay(80);
    noTone(BUZZER);

    if (i < times - 1) {
      delay(80);
    }
  }
}