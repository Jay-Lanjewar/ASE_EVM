// ============================================================
//                     A.S.E. EVM
//                    BALLOT UNIT
// ============================================================
//
// Board:
// Arduino UNO #1
//
// Purpose:
// - Receive voting permission from the Control Unit
// - Allow the voter to select one candidate or NOTA
// - Allow the voter to change the selection
// - Confirm the final selection
// - Send the confirmed vote to the Control Unit
// - Wait for the Control Unit to report RECORDED or REJECT
//
// ------------------------------------------------------------
//                    SERIAL CONNECTION
// ------------------------------------------------------------
//
// D0 (RX)  <- Control Unit TX
// D1 (TX)  -> Control Unit RX
//
// Baud rate: 9600
//
// IMPORTANT:
// Disconnect the D0/D1 wires while uploading code through USB.
// Reconnect them after uploading.
//
// ------------------------------------------------------------
//                    BUTTON CONNECTION
// ------------------------------------------------------------
//
// Every button is connected:
//
// Arduino Pin ---- Button ---- GND
//
// INPUT_PULLUP is used, therefore:
//
// Button NOT pressed = HIGH
// Button pressed     = LOW
//
// ------------------------------------------------------------
//                    LED CONNECTION
// ------------------------------------------------------------
//
// Each LED should have a suitable series resistor.
//
// Arduino Pin -> Resistor -> LED -> GND
//
// ============================================================



// ============================================================
//                    1. PIN DEFINITIONS
// ============================================================

// ---------------------- Buttons -----------------------------

const byte BUTTON_CANDIDATE_A = 2;
const byte BUTTON_CANDIDATE_B = 3;
const byte BUTTON_CANDIDATE_C = 4;

const byte BUTTON_NOTA        = 5;

const byte BUTTON_CHANGE      = 6;
const byte BUTTON_CONFIRM     = 7;


// ----------------------- LEDs --------------------------------

const byte LED_CANDIDATE_A = 8;
const byte LED_CANDIDATE_B = 9;
const byte LED_CANDIDATE_C = 10;

const byte LED_NOTA        = 11;


// ---------------------- Buzzer -------------------------------

const byte BUZZER_PIN = 12;


// ============================================================
//                    2. SELECTION TYPE
// ============================================================
//
// This creates a custom data type representing the possible
// selections a voter can make.
//
// NONE = no option has been selected yet.
// A    = Candidate A
// B    = Candidate B
// C    = Candidate C
// NOTA = None Of The Above
//
// ============================================================

enum Selection
{
    NONE,
    CANDIDATE_A,
    CANDIDATE_B,
    CANDIDATE_C,
    NOTA
};


// ============================================================
//                  3. GLOBAL VARIABLES
// ============================================================

// ------------------------------------------------------------
// Voting permission
// ------------------------------------------------------------
//
// false -> Ballot Unit is locked
// true  -> voter is allowed to interact with the ballot
//
// The Control Unit decides when this becomes true.
// ------------------------------------------------------------

bool votingEnabled = false;


// ------------------------------------------------------------
// Current selection
// ------------------------------------------------------------
//
// This stores the temporary choice made by the voter.
//
// It does NOT mean that the vote has been counted.
// The vote is only sent to the Control Unit after CONFIRM.
// ------------------------------------------------------------

Selection selectedOption = NONE;


// ------------------------------------------------------------
// Voting session number
// ------------------------------------------------------------
//
// Every voter session gets a sequence number from the
// Control Unit.
//
// This allows both UNOs to know which voting session a
// particular message belongs to.
// ------------------------------------------------------------

uint32_t sessionSequence = 0;


// ------------------------------------------------------------
// Incoming serial data buffer
// ------------------------------------------------------------
//
// Characters received from the Control Unit are temporarily
// stored here until a complete message is received.
// ------------------------------------------------------------

String receivedMessage;


// ============================================================
//                    4. CRC-8 FUNCTION
// ============================================================
//
// CRC is used to detect corrupted communication.
//
// The Ballot Unit and Control Unit both calculate the CRC.
// If the calculated value does not match the received value,
// the message is ignored.
//
// ============================================================

uint8_t calculateCRC8(const String& message)
{
    uint8_t crc = 0;


    // Process every character in the message.

    for (size_t i = 0; i < message.length(); i++)
    {
        crc ^= (uint8_t)message[i];


        // Process all 8 bits of the current byte.

        for (byte bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
            {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }


    return crc;
}



// ============================================================
//                  5. PRINT BYTE AS HEX
// ============================================================
//
// Example:
//
// 15 decimal -> 0F hexadecimal
//
// This is used when sending the CRC.
//
// ============================================================

void printHexByte(uint8_t value)
{
    const char HEX_DIGITS[] = "0123456789ABCDEF";


    Serial.print(HEX_DIGITS[(value >> 4) & 0x0F]);
    Serial.print(HEX_DIGITS[value & 0x0F]);
}



// ============================================================
//                  6. PARSE TWO HEX CHARACTERS
// ============================================================
//
// Converts something like:
//
// "3A"
//
// into the actual byte:
//
// 0x3A
//
// This is used for reading the CRC from a received message.
//
// ============================================================

bool parseHexByte(const String& text, uint8_t& value)
{
    if (text.length() != 2)
    {
        return false;
    }


    // Convert one hexadecimal character into a number.

    auto hexadecimalValue = [](char character) -> int
    {
        if (character >= '0' && character <= '9')
        {
            return character - '0';
        }

        if (character >= 'A' && character <= 'F')
        {
            return character - 'A' + 10;
        }

        if (character >= 'a' && character <= 'f')
        {
            return character - 'a' + 10;
        }

        return -1;
    };


    int highNibble = hexadecimalValue(text[0]);
    int lowNibble  = hexadecimalValue(text[1]);


    // Invalid hexadecimal character found.

    if (highNibble < 0 || lowNibble < 0)
    {
        return false;
    }


    value = (uint8_t)((highNibble << 4) | lowNibble);

    return true;
}



// ============================================================
//                  7. PARSE RECEIVED MESSAGE
// ============================================================
//
// Our message format is:
//
// COMMAND | DATA | SEQUENCE | CRC
//
// Example:
//
// ENABLE|1|15|A7
//
// This function separates those parts and checks the CRC.
//
// ============================================================

bool parseMessage(
    const String& line,
    String& command,
    String& data,
    uint32_t& sequence
)
{
    // Find the separators '|'.

    int firstSeparator  = line.indexOf('|');

    if (firstSeparator < 0)
    {
        return false;
    }


    int secondSeparator = line.indexOf('|', firstSeparator + 1);

    if (secondSeparator < 0)
    {
        return false;
    }


    int thirdSeparator  = line.indexOf('|', secondSeparator + 1);

    if (thirdSeparator < 0)
    {
        return false;
    }


    // Extract each section.

    command = line.substring(0, firstSeparator);

    data = line.substring(
        firstSeparator + 1,
        secondSeparator
    );


    String sequenceText = line.substring(
        secondSeparator + 1,
        thirdSeparator
    );


    String crcText = line.substring(
        thirdSeparator + 1
    );


    // Convert sequence number from text to integer.

    sequence = strtoul(
        sequenceText.c_str(),
        nullptr,
        10
    );


    // Convert received CRC from hexadecimal.

    uint8_t receivedCRC = 0;

    if (!parseHexByte(crcText, receivedCRC))
    {
        return false;
    }


    // Reconstruct the original message without CRC.

    String messageWithoutCRC =
        command + "|" +
        data + "|" +
        String(sequence);


    // Calculate CRC ourselves.

    uint8_t calculatedCRC =
        calculateCRC8(messageWithoutCRC);


    // Compare the two CRC values.

    if (calculatedCRC != receivedCRC)
    {
        return false;
    }


    return true;
}



// ============================================================
//                  8. SEND MESSAGE
// ============================================================
//
// Sends:
//
// COMMAND | DATA | SEQUENCE | CRC
//
// Example:
//
// VOTE|A|15|C2
//
// The newline at the end tells the receiving Arduino that
// the complete message has arrived.
//
// ============================================================

void sendMessage(
    const String& command,
    const String& data,
    uint32_t sequence
)
{
    // Build the message before CRC.

    String messageWithoutCRC =
        command + "|" +
        data + "|" +
        String(sequence);


    // Calculate checksum.

    uint8_t crc =
        calculateCRC8(messageWithoutCRC);


    // Send complete message.

    Serial.print(messageWithoutCRC);
    Serial.print('|');

    printHexByte(crc);

    Serial.print('\n');
}



// ============================================================
//                  9. BUZZER FUNCTION
// ============================================================
//
// Produces a short beep pattern.
//
// Example:
//
// beep(1) -> one beep
// beep(2) -> two beeps
// beep(3) -> three beeps
//
// ============================================================

void beep(byte numberOfBeeps)
{
    for (byte i = 0; i < numberOfBeeps; i++)
    {
        tone(BUZZER_PIN, 2000, 80);

        delay(100);
    }
}



// ============================================================
//                 10. BUTTON PRESS DETECTION
// ============================================================
//
// Because the buttons use INPUT_PULLUP:
//
// LOW = pressed
// HIGH = not pressed
//
// A small debounce delay prevents a single physical press
// from being detected multiple times because of switch
// bouncing.
//
// ============================================================

bool buttonPressed(byte pin)
{
    // Button must initially be LOW.

    if (digitalRead(pin) != LOW)
    {
        return false;
    }


    // Small debounce delay.

    delay(25);


    // Check again.

    if (digitalRead(pin) != LOW)
    {
        return false;
    }


    // Wait until the button is released.

    while (digitalRead(pin) == LOW)
    {
        delay(1);
    }


    return true;
}



// ============================================================
//                 11. TURN ALL LEDS OFF
// ============================================================

void turnAllLEDsOff()
{
    digitalWrite(LED_CANDIDATE_A, LOW);
    digitalWrite(LED_CANDIDATE_B, LOW);
    digitalWrite(LED_CANDIDATE_C, LOW);
    digitalWrite(LED_NOTA, LOW);
}



// ============================================================
//                 12. CLEAR CURRENT SELECTION
// ============================================================
//
// This is used when:
//
// - voter presses CHANGE
// - vote is successfully recorded
// - vote is rejected
// - a new voting session starts
//
// ============================================================

void clearSelection()
{
    selectedOption = NONE;

    turnAllLEDsOff();
}



// ============================================================
//                 13. SELECT AN OPTION
// ============================================================
//
// This function stores the selected option and turns on
// exactly one corresponding LED.
//
// Only one option can be selected at a time.
//
// ============================================================

void selectOption(Selection option)
{
    // Store the new selection.

    selectedOption = option;


    // Make sure no old LED remains on.

    turnAllLEDsOff();


    // Turn on the LED corresponding to the selection.

    if (option == CANDIDATE_A)
    {
        digitalWrite(LED_CANDIDATE_A, HIGH);
    }

    else if (option == CANDIDATE_B)
    {
        digitalWrite(LED_CANDIDATE_B, HIGH);
    }

    else if (option == CANDIDATE_C)
    {
        digitalWrite(LED_CANDIDATE_C, HIGH);
    }

    else if (option == NOTA)
    {
        digitalWrite(LED_NOTA, HIGH);
    }


    // Give the voter feedback.

    beep(1);
}



// ============================================================
//                 14. CONVERT SELECTION TO CHARACTER
// ============================================================
//
// Communication uses a simple character:
//
// A -> Candidate A
// B -> Candidate B
// C -> Candidate C
// N -> NOTA
//
// ============================================================

char getSelectionCharacter(Selection option)
{
    if (option == CANDIDATE_A)
    {
        return 'A';
    }

    if (option == CANDIDATE_B)
    {
        return 'B';
    }

    if (option == CANDIDATE_C)
    {
        return 'C';
    }

    if (option == NOTA)
    {
        return 'N';
    }


    // Should never normally happen.

    return '?';
}



// ============================================================
//                 15. CONFIRM THE VOTE
// ============================================================
//
// The voter can only confirm when:
//
// 1. Voting is enabled
// 2. An option has been selected
//
// The Ballot Unit sends the vote to the Control Unit.
//
// IMPORTANT:
// The vote is NOT counted here.
//
// The Control Unit must validate and record it first.
//
// ============================================================

void confirmVote()
{
    // Do nothing if voting is disabled.

    if (!votingEnabled)
    {
        return;
    }


    // Do nothing if the voter hasn't selected anything.

    if (selectedOption == NONE)
    {
        return;
    }


    // Convert the selection to A / B / C / N.

    char selectedCharacter =
        getSelectionCharacter(selectedOption);


    // Send the vote to the Control Unit.

    sendMessage(
        "VOTE",
        String(selectedCharacter),
        sessionSequence
    );


    // Temporarily lock the ballot buttons.
    //
    // We now wait for the Control Unit to respond with:
    //
    // RECORDED
    // or
    // REJECT
    //
    // The selected LED intentionally remains ON while we wait.

    votingEnabled = false;
}



// ============================================================
//                 16. PROCESS INCOMING MESSAGE
// ============================================================
//
// The Control Unit can send commands such as:
//
// PING
// ENABLE
// DISABLE
// RESET
// RECORDED
// REJECT
//
// ============================================================

void processIncomingMessage(const String& line)
{
    String command;
    String data;

    uint32_t sequence = 0;


    // First verify the message and CRC.

    if (!parseMessage(
            line,
            command,
            data,
            sequence
        ))
    {
        // Invalid or corrupted message.
        // Ignore it.

        return;
    }


    // --------------------------------------------------------
    // PING
    // --------------------------------------------------------
    //
    // Used during communication testing / startup checks.
    //
    // --------------------------------------------------------

    if (command == "PING")
    {
        sendMessage(
            "PONG",
            "OK",
            0
        );

        return;
    }



    // --------------------------------------------------------
    // ENABLE
    // --------------------------------------------------------
    //
    // Control Unit has authenticated a voter and is giving
    // this Ballot Unit permission to begin voting.
    //
    // --------------------------------------------------------

    if (command == "ENABLE")
    {
        votingEnabled = true;

        sessionSequence = sequence;

        // New voter session starts with no selection.

        clearSelection();

        // Small feedback beep.

        beep(1);

        return;
    }



    // --------------------------------------------------------
    // DISABLE / RESET
    // --------------------------------------------------------
    //
    // Immediately lock the Ballot Unit.
    //
    // --------------------------------------------------------

    if (command == "DISABLE" || command == "RESET")
    {
        votingEnabled = false;

        sessionSequence = 0;

        clearSelection();

        return;
    }



    // --------------------------------------------------------
    // RECORDED
    // --------------------------------------------------------
    //
    // Control Unit has successfully accepted and stored the
    // vote.
    //
    // Prepare the Ballot Unit for the next voter.
    //
    // --------------------------------------------------------

    if (command == "RECORDED")
    {
        votingEnabled = false;

        sessionSequence = 0;

        clearSelection();

        // Two beeps = vote recorded.

        beep(2);

        return;
    }



    // --------------------------------------------------------
    // REJECT
    // --------------------------------------------------------
    //
    // Control Unit rejected the vote.
    //
    // Lock the Ballot Unit and clear the old selection.
    //
    // --------------------------------------------------------

    if (command == "REJECT")
    {
        votingEnabled = false;

        sessionSequence = 0;

        clearSelection();

        // Three beeps = rejected/error feedback.

        beep(3);

        return;
    }
}



// ============================================================
//                 17. SERIAL COMMUNICATION TASK
// ============================================================
//
// Serial data arrives one character at a time.
//
// This function collects the characters into a buffer until
// a newline '\n' is received.
//
// Example:
//
// ENABLE|1|15|A7\n
//
// Once the newline arrives, the complete message is sent to
// processIncomingMessage().
//
// ============================================================

void serialTask()
{
    while (Serial.available())
    {
        char receivedCharacter =
            (char)Serial.read();


        // ----------------------------------------------------
        // Complete message received
        // ----------------------------------------------------

        if (receivedCharacter == '\n')
        {
            // Remove unnecessary spaces / line endings.

            receivedMessage.trim();


            // Ignore completely empty messages.

            if (receivedMessage.length() > 0)
            {
                processIncomingMessage(receivedMessage);
            }


            // Clear buffer for the next message.

            receivedMessage = "";
        }


        // ----------------------------------------------------
        // Add character to buffer
        // ----------------------------------------------------

        else if (receivedMessage.length() < 60)
        {
            receivedMessage += receivedCharacter;
        }


        // ----------------------------------------------------
        // Safety:
        // message became too long
        // ----------------------------------------------------

        else
        {
            receivedMessage = "";
        }
    }
}



// ============================================================
//                         SETUP
// ============================================================

void setup()
{
    // --------------------------------------------------------
    // Start communication with Control Unit
    // --------------------------------------------------------

    Serial.begin(9600);


    // --------------------------------------------------------
    // Configure buttons
    // --------------------------------------------------------
    //
    // INPUT_PULLUP means:
    //
    // HIGH -> button released
    // LOW  -> button pressed
    //
    // --------------------------------------------------------

    pinMode(BUTTON_CANDIDATE_A, INPUT_PULLUP);
    pinMode(BUTTON_CANDIDATE_B, INPUT_PULLUP);
    pinMode(BUTTON_CANDIDATE_C, INPUT_PULLUP);

    pinMode(BUTTON_NOTA, INPUT_PULLUP);

    pinMode(BUTTON_CHANGE, INPUT_PULLUP);
    pinMode(BUTTON_CONFIRM, INPUT_PULLUP);


    // --------------------------------------------------------
    // Configure LEDs
    // --------------------------------------------------------

    pinMode(LED_CANDIDATE_A, OUTPUT);
    pinMode(LED_CANDIDATE_B, OUTPUT);
    pinMode(LED_CANDIDATE_C, OUTPUT);

    pinMode(LED_NOTA, OUTPUT);


    // --------------------------------------------------------
    // Configure buzzer
    // --------------------------------------------------------

    pinMode(BUZZER_PIN, OUTPUT);


    // --------------------------------------------------------
    // Start in a safe state
    // --------------------------------------------------------
    //
    // The Ballot Unit must NOT allow voting immediately after
    // startup.
    //
    // The Control Unit must explicitly send ENABLE.
    //
    // --------------------------------------------------------

    votingEnabled = false;

    sessionSequence = 0;

    clearSelection();
}



// ============================================================
//                          LOOP
// ============================================================

void loop()
{
    // --------------------------------------------------------
    // Always process communication first.
    // --------------------------------------------------------

    serialTask();


    // --------------------------------------------------------
    // Do nothing else while voting is disabled.
    // --------------------------------------------------------
    //
    // This is one of the most important safety gates in the
    // Ballot Unit.
    //
    // Even if someone presses a candidate button while the
    // poll is closed, nothing happens.
    //
    // --------------------------------------------------------

    if (!votingEnabled)
    {
        return;
    }



    // ========================================================
    //             NO OPTION SELECTED YET
    // ========================================================
    //
    // The voter may choose:
    //
    // A
    // B
    // C
    // NOTA
    //
    // Once one is selected, the candidate buttons are no
    // longer checked until CHANGE is pressed.
    //
    // ========================================================

    if (selectedOption == NONE)
    {
        if (buttonPressed(BUTTON_CANDIDATE_A))
        {
            selectOption(CANDIDATE_A);
        }

        else if (buttonPressed(BUTTON_CANDIDATE_B))
        {
            selectOption(CANDIDATE_B);
        }

        else if (buttonPressed(BUTTON_CANDIDATE_C))
        {
            selectOption(CANDIDATE_C);
        }

        else if (buttonPressed(BUTTON_NOTA))
        {
            selectOption(NOTA);
        }
    }



    // ========================================================
    //             AN OPTION IS ALREADY SELECTED
    // ========================================================
    //
    // Now the voter has only two meaningful actions:
    //
    // CHANGE  -> remove current selection and choose again
    //
    // CONFIRM -> finalize the selection and send it
    //
    // ========================================================

    else
    {
        if (buttonPressed(BUTTON_CHANGE))
        {
            clearSelection();
        }

        else if (buttonPressed(BUTTON_CONFIRM))
        {
            confirmVote();
        }
    }
}