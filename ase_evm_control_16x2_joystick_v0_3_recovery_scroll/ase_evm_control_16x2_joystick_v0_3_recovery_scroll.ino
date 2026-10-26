// ============================================================
//                         A.S.E. EVM
//                        CONTROL UNIT
//                           v0.3
// ============================================================
//
// Arduino:
// Arduino UNO #2
//
// ------------------------------------------------------------
// PURPOSE
// ------------------------------------------------------------
//
// The Control Unit is responsible for:
//
// 1. Admin authentication
// 2. Voter registration
// 3. Voter authentication
// 4. Poll opening and closing
// 5. Preventing duplicate voting
// 6. Vote validation
// 7. Vote counting
// 8. EEPROM data storage
// 9. Optional tamper detection (disabled until a switch is available)
// 10. Startup self-test and retrying Ballot Unit connection
// 11. LCD display
// 12. Communication with the Ballot Unit
//
// ------------------------------------------------------------
// IMPORTANT
// ------------------------------------------------------------
//
// This is a competition prototype.
// It is NOT certified election equipment.
//
// ------------------------------------------------------------
//
//                    SERIAL CONNECTION
//
// D0 / RX  <- Ballot Unit TX
// D1 / TX  -> Ballot Unit RX
//
// Baud rate: 9600
//
// IMPORTANT:
// Disconnect D0 and D1 from the other UNO while uploading
// this program through USB.
//
// ------------------------------------------------------------
//
//                    ADMIN JOYSTICK
//
// A0 -> VRx (left / right)
// A1 -> VRy (up / down)
// D2 -> SW (press to select)
//
// ------------------------------------------------------------
//
//                    TAMPER / OUTPUTS
//
// D6 -> Reserved; tamper detection disabled for this build
// D7 -> Status LED
// D8 -> Buzzer
//
// Tamper switch:
//
// LOW  = Case closed
// HIGH = Case open
//
// ------------------------------------------------------------
//
//                         RC522
//
// D9  -> RST
// D10 -> SDA / SS
// D11 -> MOSI
// D12 -> MISO
// D13 -> SCK
//
// RC522 power:
//
// 3.3V -> 3.3V
// GND  -> GND
//
// IMPORTANT:
// RC522 is a 3.3V device.
// Use suitable logic-level protection between a 5V UNO
// and the RC522 signals unless the exact breakout board
// is explicitly documented as 5V-safe.
//
// ------------------------------------------------------------
//
//                       16x2 I2C LCD
//
// A4 -> SDA
// A5 -> SCL
// 5V -> VCC
// GND -> GND
//
// ============================================================


// ============================================================
//                    1. REQUIRED LIBRARIES
// ============================================================

#include <SPI.h> // RC522 RFID reader communicates with the Arduino using the SPI communication protocol.

#include <MFRC522.h> // specifically for the RC522 RFID reader.

#include <Wire.h> // for I²C communication.

#include <LiquidCrystal_I2C.h> // makes actually controlling our I²C LCD easy.

#include <EEPROM.h> // access to the UNO's internal EEPROM.

#include <stdio.h>  // snprintf() for building short LCD lines.
#include <string.h> // memset() for initializing saved election state.
#include <stdlib.h> // strtoul() for parsing serial sequence numbers.


// ============================================================
//                    2. PIN DEFINITIONS
// ============================================================


// ------------------------------------------------------------
//                    ADMIN JOYSTICK
// ------------------------------------------------------------
//
// These four buttons are used only by the administrator.
//
// MENU   -> Enter / leave admin interface
// UP     -> Move upward through menu
// DOWN   -> Move downward through menu
// SELECT -> Select current menu item
//
// ------------------------------------------------------------

const byte JOYSTICK_X_PIN  = A0;

const byte JOYSTICK_Y_PIN  = A1;

const byte JOYSTICK_SW_PIN = 2;

// Analog thresholds for detecting deliberate joystick movement.
// If the physical module's directions are reversed, swap the
// UP/DOWN or LEFT/RIGHT comparisons in readJoystickAction().
const int JOYSTICK_LOW_THRESHOLD  = 330;
const int JOYSTICK_HIGH_THRESHOLD = 700;

const int JOYSTICK_CENTER_LOW  = 400;
const int JOYSTICK_CENTER_HIGH = 624;

// No spare tamper button is currently available.
// Set this to true only after connecting a suitable case switch
// to D6 and verifying its mechanical operation.
const bool TAMPER_DETECTION_ENABLED = false;


// ------------------------------------------------------------
//                    SYSTEM CONNECTIONS
// ------------------------------------------------------------

const byte TAMPER_SWITCH = 6;

const byte STATUS_LED    = 7;

const byte BUZZER_PIN    = 8;


// ------------------------------------------------------------
//                    RFID CONNECTIONS
// ------------------------------------------------------------

const byte RFID_RST = 9;

const byte RFID_SS  = 10;


// ============================================================
//                    3. HARDWARE OBJECTS
// ============================================================


// ------------------------------------------------------------
//                        LCD
// ------------------------------------------------------------
//
// 16 columns
// 2 rows
//
// Common I2C address used by our prototype:
// 0x27
//
// ------------------------------------------------------------

LiquidCrystal_I2C lcd(
    0x27,  // Common I2C address; verify on the actual module.
    16,    // Columns
    2      // Rows
);


// ------------------------------------------------------------
//                       RC522 RFID
// ------------------------------------------------------------

MFRC522 rfid(
    RFID_SS,
    RFID_RST
);


// ============================================================
//                    4. SYSTEM CONSTANTS
// ============================================================


// ------------------------------------------------------------
// MAGIC VALUE
// ------------------------------------------------------------
//
// Used to identify a valid saved State record.
//
// ------------------------------------------------------------

const uint16_t EEPROM_MAGIC = 0xA55A;


// ------------------------------------------------------------
// DATA FORMAT VERSION
// ------------------------------------------------------------

const byte EEPROM_VERSION = 1;


// ------------------------------------------------------------
// MAXIMUM RFID UID LENGTH
// ------------------------------------------------------------

const byte MAX_UID_LENGTH = 7;


// ------------------------------------------------------------
// MAXIMUM NUMBER OF REGISTERED VOTERS
// ------------------------------------------------------------
//
// Current prototype limit.
//
// ------------------------------------------------------------

const byte MAX_REGISTERED_VOTERS = 12;


// ============================================================
//                    5. EEPROM ADDRESSES
// ============================================================
//
// The UNO has a small internal EEPROM.
//
// We reserve different addresses for different kinds of data.
//
// ============================================================


// ------------------------------------------------------------
// ADMIN CARD
// ------------------------------------------------------------

const int EEPROM_ADMIN_LENGTH = 4;

const int EEPROM_ADMIN_UID    = 5;


// ------------------------------------------------------------
// VOTER DATABASE
// ------------------------------------------------------------

const int EEPROM_VOTER_COUNT = 13;

const int EEPROM_VOTER_DATA  = 16;


// ------------------------------------------------------------
// EACH VOTER RECORD
// ------------------------------------------------------------
//
// One record contains:
//
// 1 byte  -> UID length
// 7 bytes -> UID data
// 1 byte  -> reserved
//
// Total = 9 bytes
//
// ------------------------------------------------------------

const int EEPROM_VOTER_RECORD_SIZE = 9;


// ------------------------------------------------------------
// ELECTION STATE SLOTS
// ------------------------------------------------------------
//
// Two slots are used so that election state is not dependent
// on only one EEPROM record.
//
// ------------------------------------------------------------

const int EEPROM_STATE_SLOT_1 = 128;

const int EEPROM_STATE_SLOT_2 = 148;


// ============================================================
//                    6. ELECTION STATE
// ============================================================
//
// This structure stores the persistent election information.
//
// It is written to EEPROM.
//
// ============================================================

struct __attribute__((packed)) ElectionState
{
    // Used to identify a valid record.

    uint16_t magic;


    // Identifies the structure format version.

    uint8_t version;


    // Increases every time the state is saved.

    uint32_t generation;


    // Each bit represents whether a voter has already voted.

    uint16_t usedVoterMask;


    // Vote counters:
//
// voteCount[0] -> Candidate A
// voteCount[1] -> Candidate B
// voteCount[2] -> Candidate C
// voteCount[3] -> NOTA

    uint16_t voteCount[4];


    // Current voting session sequence.

    uint16_t sequence;


    // CRC of the rest of the State structure.

    uint8_t crc;
};


// ============================================================
//                    7. GLOBAL VARIABLES
// ============================================================


// ------------------------------------------------------------
// ELECTION STATE
// ------------------------------------------------------------

ElectionState electionState;


// ------------------------------------------------------------
// CURRENT EEPROM SLOT
// ------------------------------------------------------------
//
// Keeps track of which EEPROM state slot currently contains
// the latest valid data.
//
// ------------------------------------------------------------

int activeStateSlot = EEPROM_STATE_SLOT_1;


// ============================================================
//                    8. ADMIN INFORMATION
// ============================================================


// ------------------------------------------------------------
// STORED ADMIN RFID UID
// ------------------------------------------------------------

byte adminUID[MAX_UID_LENGTH];


// ------------------------------------------------------------
// LENGTH OF ADMIN RFID UID
// ------------------------------------------------------------

byte adminUIDLength = 0;


// ============================================================
//                    9. SYSTEM FLAGS
// ============================================================


// ------------------------------------------------------------
// SYSTEM READY
// ------------------------------------------------------------
//
// false -> startup/self-test not completed
// true  -> normal operation allowed
//
// ------------------------------------------------------------

bool systemReady = false;


// ------------------------------------------------------------
// BALLOT UNIT COMMUNICATION STATUS
// ------------------------------------------------------------
// The Control Unit may power up before the Ballot Unit.
// In that case, keep the user interface alive and retry PINGs
// until the Ballot Unit responds with PONG instead of freezing.
// ------------------------------------------------------------

bool ballotCommunicationReady = false;
bool ballotPingOutstanding = false;
unsigned long ballotPingSentAt = 0;
unsigned long nextBallotPingAt = 0;

const unsigned long BALLOT_PING_TIMEOUT_MS = 800;
const unsigned long BALLOT_PING_INTERVAL_MS = 1500;


// ------------------------------------------------------------
// POLL STATE
// ------------------------------------------------------------
//
// false -> voting closed
// true  -> voting open
//
// ------------------------------------------------------------

bool pollOpen = false;


// ------------------------------------------------------------
// ADMIN SESSION
// ------------------------------------------------------------
//
// true while the administrator is inside the admin menu.
//
// ------------------------------------------------------------

bool adminSessionActive = false;


// ------------------------------------------------------------
// TAMPER LOCK
// ------------------------------------------------------------
//
// true -> system is locked because the enclosure was opened
// during an active poll.
//
// ------------------------------------------------------------

bool tamperLockActive = false;


// ------------------------------------------------------------
// VOTER SESSION
// ------------------------------------------------------------
//
// true while one authenticated voter is currently voting.
//
// ------------------------------------------------------------

bool voterSessionActive = false;


// ------------------------------------------------------------
// CURRENT VOTER INDEX
// ------------------------------------------------------------
//
// Stores which registered voter is currently voting.
//
// -1 means no voter is active.
//
// ------------------------------------------------------------

int currentVoterIndex = -1;


// ------------------------------------------------------------
// CURRENT SESSION SEQUENCE
// ------------------------------------------------------------

uint32_t currentSessionSequence = 0;


// ============================================================
//                    10. ADMIN MENU VARIABLES
// ============================================================


// ------------------------------------------------------------
// CURRENT MENU ITEM
// ------------------------------------------------------------

byte currentMenuIndex = 0;


// ------------------------------------------------------------
// RESET CONFIRMATION
// ------------------------------------------------------------
//
// true means the system is currently asking:
//
// "RESET ELECTION?"
//
// ------------------------------------------------------------

bool resetConfirmationActive = false;


// ============================================================
//                    11. SERIAL BUFFER
// ============================================================
//
// Incoming characters from the Ballot Unit are stored here
// until a complete message is received.
//
// ============================================================

String receivedSerialData;


// ============================================================
//                    12. CRC-8 FOR BYTE DATA
// ============================================================
//
// Used for calculating the CRC of EEPROM State data.
//
// ============================================================

uint8_t calculateCRC8Bytes(
    const byte* data,
    size_t length
)
{
    uint8_t crc = 0;


    // Process every byte.

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];


        // Process all eight bits.

        for (byte bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
            {
                crc =
                    (uint8_t)((crc << 1) ^ 0x07);
            }

            else
            {
                crc =
                    (uint8_t)(crc << 1);
            }
        }
    }


    return crc;
}


// ============================================================
//                    13. CRC FOR TEXT
// ============================================================
//
// Used for serial communication messages.
//
// ============================================================

uint8_t calculateCRC8(
    const String& message
)
{
    return calculateCRC8Bytes(
        (const byte*)message.c_str(),
        message.length()
    );
}


// ============================================================
//                    14. STATE CRC
// ============================================================
//
// Calculates the CRC of an ElectionState structure.
//
// The CRC field itself is excluded.
//
// ============================================================

uint8_t calculateStateCRC(
    ElectionState state
)
{
    state.crc = 0;


    return calculateCRC8Bytes(
        (byte*)&state,
        sizeof(ElectionState) - 1
    );
}


// ============================================================
//                    15. PRINT HEX BYTE
// ============================================================
//
// Example:
//
// Decimal 170
// becomes
// AA
//
// Used for serial CRC output.
//
// ============================================================

void printHexByte(
    uint8_t value
)
{
    const char HEX_DIGITS[] =
        "0123456789ABCDEF";


    Serial.print(
        HEX_DIGITS[(value >> 4) & 0x0F]
    );


    Serial.print(
        HEX_DIGITS[value & 0x0F]
    );
}


// ============================================================
//                    16. PARSE HEX BYTE
// ============================================================
//
// Converts two hexadecimal characters into one byte.
//
// Example:
//
// "A7"
//   ↓
// 0xA7
//
// ============================================================

bool parseHexByte(
    const String& text,
    uint8_t& value
)
{
    if (text.length() != 2)
    {
        return false;
    }


    // --------------------------------------------------------
    // Convert one hexadecimal character into a number.
    // --------------------------------------------------------

    auto hexadecimalValue = [](char character) -> int
    {
        if (
            character >= '0' &&
            character <= '9'
        )
        {
            return character - '0';
        }


        if (
            character >= 'A' &&
            character <= 'F'
        )
        {
            return character - 'A' + 10;
        }


        if (
            character >= 'a' &&
            character <= 'f'
        )
        {
            return character - 'a' + 10;
        }


        return -1;
    };


    int highNibble =
        hexadecimalValue(text[0]);


    int lowNibble =
        hexadecimalValue(text[1]);


    if (
        highNibble < 0 ||
        lowNibble < 0
    )
    {
        return false;
    }


    value =
        (uint8_t)(
            (highNibble << 4) |
            lowNibble
        );


    return true;
}


// ============================================================
//                    17. PARSE SERIAL MESSAGE
// ============================================================
//
// Message format:
//
// COMMAND | DATA | SEQUENCE | CRC
//
// Example:
//
// VOTE|B|15|A7
//
// This function:
//
// 1. Splits the message
// 2. Extracts command
// 3. Extracts data
// 4. Extracts sequence
// 5. Reads CRC
// 6. Calculates CRC
// 7. Compares both CRC values
//
// ============================================================

bool parseMessage(
    const String& line,
    String& command,
    String& data,
    uint32_t& sequence
)
{
    int firstSeparator =
        line.indexOf('|');


    if (firstSeparator < 0)
    {
        return false;
    }


    int secondSeparator =
        line.indexOf(
            '|',
            firstSeparator + 1
        );


    if (secondSeparator < 0)
    {
        return false;
    }


    int thirdSeparator =
        line.indexOf(
            '|',
            secondSeparator + 1
        );


    if (thirdSeparator < 0)
    {
        return false;
    }


    // --------------------------------------------------------
    // Extract command
    // --------------------------------------------------------

    command =
        line.substring(
            0,
            firstSeparator
        );


    // --------------------------------------------------------
    // Extract data
    // --------------------------------------------------------

    data =
        line.substring(
            firstSeparator + 1,
            secondSeparator
        );


    // --------------------------------------------------------
    // Extract sequence text
    // --------------------------------------------------------

    String sequenceText =
        line.substring(
            secondSeparator + 1,
            thirdSeparator
        );


    // --------------------------------------------------------
    // Extract CRC text
    // --------------------------------------------------------

    String crcText =
        line.substring(
            thirdSeparator + 1
        );


    // Convert sequence text into a number.

    sequence =
        strtoul(
            sequenceText.c_str(),
            nullptr,
            10
        );


    // --------------------------------------------------------
    // Convert CRC text into a byte.
    // --------------------------------------------------------

    uint8_t receivedCRC = 0;


    if (
        !parseHexByte(
            crcText,
            receivedCRC
        )
    )
    {
        return false;
    }


    // --------------------------------------------------------
    // Rebuild message without CRC.
    // --------------------------------------------------------

    String messageWithoutCRC =
        command +
        "|" +
        data +
        "|" +
        String(sequence);


    // --------------------------------------------------------
    // Calculate CRC ourselves.
    // --------------------------------------------------------

    uint8_t calculatedCRC =
        calculateCRC8(
            messageWithoutCRC
        );


    // --------------------------------------------------------
    // Compare CRC values.
    // --------------------------------------------------------

    if (
        calculatedCRC !=
        receivedCRC
    )
    {
        return false;
    }


    return true;
}


// ============================================================
//                    18. SEND SERIAL MESSAGE
// ============================================================
//
// Sends:
//
// COMMAND | DATA | SEQUENCE | CRC
//
// Example:
//
// ENABLE|1|15|A7
//
// ============================================================

void sendMessage(
    const String& command,
    const String& data,
    uint32_t sequence
)
{
    String messageWithoutCRC =
        command +
        "|" +
        data +
        "|" +
        String(sequence);


    uint8_t crc =
        calculateCRC8(
            messageWithoutCRC
        );


    Serial.print(
        messageWithoutCRC
    );


    Serial.print('|');


    printHexByte(crc);


    Serial.print('\n');
}


// ============================================================
//                    19. BUZZER
// ============================================================
//
// Produces a number of short beeps.
//
// beep(1) -> one beep
// beep(2) -> two beeps
// beep(3) -> three beeps
//
// ============================================================

void beep(
    byte numberOfBeeps
)
{
    for (
        byte i = 0;
        i < numberOfBeeps;
        i++
    )
    {
        tone(
            BUZZER_PIN,
            2000,
            80
        );


        delay(100);
    }
}


// ============================================================
//                    20. BUTTON PRESS
// ============================================================
//
// Buttons use INPUT_PULLUP:
//
// HIGH = not pressed
// LOW  = pressed
//
// A debounce delay prevents one physical press from being
// detected multiple times.
//
// ============================================================

// ============================================================
//                    20. JOYSTICK INPUT
// ============================================================
//
// Joystick directions:
//
// UP      -> Previous menu item
// DOWN    -> Next menu item
// LEFT    -> Back / exit
// RIGHT   -> Reserved
// PRESS   -> Select / confirm
//
// A direction produces one action, then the stick must return
// to its centre position before another direction is accepted.
// The SW click is debounced without blocking the main loop.
//
// ============================================================

enum JoystickAction
{
    JOY_NONE,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
    JOY_SELECT
};


byte readJoystickAction()
{
    // --------------------------------------------------------
    // Debounce the joystick's built-in push switch.
    // INPUT_PULLUP means HIGH = released, LOW = pressed.
    // --------------------------------------------------------

    static bool lastRawSwitchState = HIGH;
    static bool stableSwitchState = HIGH;
    static unsigned long lastSwitchChangeTime = 0;

    static bool axisArmed = true;
    static unsigned long lastAxisActionTime = 0;

    bool rawSwitchState = digitalRead(JOYSTICK_SW_PIN);

    if (rawSwitchState != lastRawSwitchState)
    {
        lastRawSwitchState = rawSwitchState;
        lastSwitchChangeTime = millis();
    }

    if (
        millis() - lastSwitchChangeTime >= 25 &&
        rawSwitchState != stableSwitchState
    )
    {
        stableSwitchState = rawSwitchState;

        if (stableSwitchState == LOW)
        {
            // Avoid interpreting the physical click as a direction.
            axisArmed = false;
            lastAxisActionTime = millis();
            return JOY_SELECT;
        }
    }


    // --------------------------------------------------------
    // Read the analog X and Y axes.
    // Centre is typically close to 512 on an UNO (10-bit ADC).
    // --------------------------------------------------------

    int xValue = analogRead(JOYSTICK_X_PIN);
    int yValue = analogRead(JOYSTICK_Y_PIN);


    // Re-arm direction input only when the stick returns to centre.

    bool joystickIsCentered =
        xValue >= JOYSTICK_CENTER_LOW &&
        xValue <= JOYSTICK_CENTER_HIGH &&
        yValue >= JOYSTICK_CENTER_LOW &&
        yValue <= JOYSTICK_CENTER_HIGH;

    if (joystickIsCentered)
    {
        axisArmed = true;
    }

    if (!axisArmed)
    {
        return JOY_NONE;
    }

    if (millis() - lastAxisActionTime < 180)
    {
        return JOY_NONE;
    }


    // --------------------------------------------------------
    // Convert analog readings into one direction event.
    // --------------------------------------------------------

    JoystickAction action = JOY_NONE;

    if (yValue < JOYSTICK_LOW_THRESHOLD)
    {
        action = JOY_UP;
    }
    else if (yValue > JOYSTICK_HIGH_THRESHOLD)
    {
        action = JOY_DOWN;
    }
    else if (xValue < JOYSTICK_LOW_THRESHOLD)
    {
        action = JOY_LEFT;
    }
    else if (xValue > JOYSTICK_HIGH_THRESHOLD)
    {
        action = JOY_RIGHT;
    }

    if (action != JOY_NONE)
    {
        axisArmed = false;
        lastAxisActionTime = millis();
    }

    return action;
}


// ============================================================
//                    21. LCD HELPER
// ============================================================
//
// The school LCD has 16 columns and 2 rows.
// Each line is limited to 16 characters and padded with spaces
// so that old text cannot remain on the display.
//
// ============================================================

void printLCDLine(
    byte row,
    const char* text
)
{
    lcd.setCursor(0, row);

    byte column = 0;

    while (
        column < 16 &&
        text[column] != '\0'
    )
    {
        lcd.print(text[column]);
        column++;
    }

    while (column < 16)
    {
        lcd.print(' ');
        column++;
    }
}


void displayLCD(
    const char* line1,
    const char* line2 = ""
)
{
    lcd.clear();

    printLCDLine(0, line1);
    printLCDLine(1, line2);
}


//                    22. CASE STATUS
// ============================================================
//
// Tamper switch wiring:
//
// LOW  = case closed
// HIGH = case open
//
// ============================================================

bool isCaseClosed()
{
    // Without a connected case switch, do not read floating D6.
    // This means the optional tamper feature is not active.
    if (!TAMPER_DETECTION_ENABLED)
    {
        return true;
    }

    return digitalRead(TAMPER_SWITCH) == LOW;
}


// ============================================================
//                    23. COMPARE RFID UIDs
// ============================================================
//
// Checks:
//
// 1. UID lengths
// 2. Every UID byte
//
// ============================================================

bool areUIDsEqual(
    const byte* firstUID,
    byte firstLength,

    const byte* secondUID,
    byte secondLength
)
{
    // Different lengths cannot be equal.

    if (
        firstLength !=
        secondLength
    )
    {
        return false;
    }


    // Compare every byte.

    for (
        byte i = 0;
        i < firstLength;
        i++
    )
    {
        if (
            firstUID[i] !=
            secondUID[i]
        )
        {
            return false;
        }
    }


    return true;
}


// ============================================================
//                    24. READ RFID CARD
// ============================================================
//
// Returns:
//
// true  -> card successfully read
// false -> no card / read failed
//
// ============================================================

bool readRFIDCard(
    byte* uid,
    byte& uidLength
)
{
    // No new card present.

    if (
        !rfid.PICC_IsNewCardPresent()
    )
    {
        return false;
    }


    // Card was detected but UID could not be read.

    if (
        !rfid.PICC_ReadCardSerial()
    )
    {
        return false;
    }


    // Store UID length.

    uidLength =
        rfid.uid.size;


    // Protect against oversized UID.

    if (
        uidLength >
        MAX_UID_LENGTH
    )
    {
        uidLength =
            MAX_UID_LENGTH;
    }


    // Copy UID bytes into our own array.

    for (
        byte i = 0;
        i < uidLength;
        i++
    )
    {
        uid[i] =
            rfid.uid.uidByte[i];
    }


    // Finish communication with card.

    rfid.PICC_HaltA();

    rfid.PCD_StopCrypto1();


    // Small delay so the same card isn't immediately
    // detected repeatedly.

    delay(250);


    return true;
}


// ============================================================
//                    25. WRITE VOTER TO EEPROM
// ============================================================
//
// Stores one registered voter card.
//
// ============================================================

void writeVoterToEEPROM(
    byte voterIndex,
    byte uidLength,
    const byte* uid
)
{
    int address =
        EEPROM_VOTER_DATA +
        voterIndex *
        EEPROM_VOTER_RECORD_SIZE;


    // Store UID length.

    EEPROM.update(
        address,
        uidLength
    );


    // Store exactly seven UID bytes.
//
// Unused bytes are written as zero.

    for (
        byte i = 0;
        i < MAX_UID_LENGTH;
        i++
    )
    {
        EEPROM.update(
            address + 1 + i,

            (
                i < uidLength
            )
                ? uid[i]
                : 0
        );
    }


    // Reserved byte.

    EEPROM.update(
        address + 8,
        0
    );
}


// ============================================================
//                    26. READ VOTER FROM EEPROM
// ============================================================

void readVoterFromEEPROM(
    byte voterIndex,
    byte& uidLength,
    byte* uid
)
{
    int address =
        EEPROM_VOTER_DATA +
        voterIndex *
        EEPROM_VOTER_RECORD_SIZE;


    // Read UID length.

    uidLength =
        EEPROM.read(address);


    // Invalid length protection.

    if (
        uidLength >
        MAX_UID_LENGTH
    )
    {
        uidLength = 0;
    }


    // Read UID bytes.

    for (
        byte i = 0;
        i < MAX_UID_LENGTH;
        i++
    )
    {
        uid[i] =
            EEPROM.read(
                address + 1 + i
            );
    }
}


// ============================================================
//                    27. FIND REGISTERED VOTER
// ============================================================
//
// Searches the registered voter database.
//
// Returns:
//
// voter index -> found
// -1          -> not found
//
// ============================================================

int findRegisteredVoter(
    const byte* uid,
    byte uidLength
)
{
    byte voterCount =
        EEPROM.read(
            EEPROM_VOTER_COUNT
        );


    // EEPROM contains an impossible voter count.

    if (
        voterCount >
        MAX_REGISTERED_VOTERS
    )
    {
        return -1;
    }


    // Search every registered voter.

    for (
        byte i = 0;
        i < voterCount;
        i++
    )
    {
        byte storedLength = 0;

        byte storedUID[
            MAX_UID_LENGTH
        ];


        readVoterFromEEPROM(
            i,
            storedLength,
            storedUID
        );


        if (
            areUIDsEqual(
                uid,
                uidLength,

                storedUID,
                storedLength
            )
        )
        {
            return i;
        }
    }


    return -1;
}


// ============================================================
//                    28. LOAD SYSTEM CONFIGURATION
// ============================================================
//
// Loads:
//
// - Admin UID
// - Registered voter count
//
// ============================================================

void loadSystemConfiguration()
{
    // --------------------------------------------------------
    // Load admin UID length.
    // --------------------------------------------------------

    adminUIDLength =
        EEPROM.read(
            EEPROM_ADMIN_LENGTH
        );


    // Protect against invalid data.

    if (
        adminUIDLength >
        MAX_UID_LENGTH
    )
    {
        adminUIDLength = 0;
    }


    // --------------------------------------------------------
    // Load admin UID bytes.
    // --------------------------------------------------------

    for (
        byte i = 0;
        i < MAX_UID_LENGTH;
        i++
    )
    {
        adminUID[i] =
            EEPROM.read(
                EEPROM_ADMIN_UID + i
            );
    }


    // --------------------------------------------------------
    // Check registered voter count.
    // --------------------------------------------------------

    byte voterCount =
        EEPROM.read(
            EEPROM_VOTER_COUNT
        );


    // Invalid count -> reset count to zero.

    if (
        voterCount >
        MAX_REGISTERED_VOTERS
    )
    {
        EEPROM.update(
            EEPROM_VOTER_COUNT,
            0
        );
    }
}


// ============================================================
//                    29. SAVE ADMIN CARD
// ============================================================
//
// Saves the administrator RFID card to EEPROM.
//
// ============================================================

void saveAdminCard(
    const byte* uid,
    byte uidLength
)
{
    adminUIDLength =
        uidLength;


    // Store UID length.

    EEPROM.update(
        EEPROM_ADMIN_LENGTH,
        adminUIDLength
    );


    // Store UID bytes.

    for (
        byte i = 0;
        i < MAX_UID_LENGTH;
        i++
    )
    {
        EEPROM.update(
            EEPROM_ADMIN_UID + i,

            (
                i < uidLength
            )
                ? uid[i]
                : 0
        );
    }
}


// ============================================================
//                    30. INITIALIZE ELECTION STATE
// ============================================================
//
// Creates a fresh election state.
//
// Used if neither EEPROM state slot contains valid data.
//
// ============================================================

void initializeElectionState()
{
    // Clear entire structure.

    memset(
        &electionState,
        0,
        sizeof(electionState)
    );


    // Add identifying information.

    electionState.magic =
        EEPROM_MAGIC;


    electionState.version =
        EEPROM_VERSION;


    // Calculate CRC.

    electionState.crc =
        calculateStateCRC(
            electionState
        );


    // Write initial state.

    EEPROM.put(
        EEPROM_STATE_SLOT_1,
        electionState
    );


    activeStateSlot =
        EEPROM_STATE_SLOT_1;
}


// ============================================================
//                    31. VALIDATE ELECTION STATE
// ============================================================
//
// Checks whether a saved EEPROM record is valid.
//
// ============================================================

bool isElectionStateValid(
    const ElectionState& state
)
{
    // Check magic value.

    if (
        state.magic !=
        EEPROM_MAGIC
    )
    {
        return false;
    }


    // Check version.

    if (
        state.version !=
        EEPROM_VERSION
    )
    {
        return false;
    }


    // Check CRC.

    return (
        calculateStateCRC(state)
        ==
        state.crc
    );
}


// ============================================================
//                    32. LOAD ELECTION STATE
// ============================================================
//
// There are two possible EEPROM state slots.
//
// The newest valid generation is loaded.
//
// ============================================================

void loadElectionState()
{
    ElectionState stateSlot1;

    ElectionState stateSlot2;


    // Read both slots.

    EEPROM.get(
        EEPROM_STATE_SLOT_1,
        stateSlot1
    );


    EEPROM.get(
        EEPROM_STATE_SLOT_2,
        stateSlot2
    );


    // Validate both.

    bool slot1Valid =
        isElectionStateValid(
            stateSlot1
        );


    bool slot2Valid =
        isElectionStateValid(
            stateSlot2
        );


    // --------------------------------------------------------
    // Both slots are valid.
    //
    // Load the newer one.
    // --------------------------------------------------------

    if (
        slot1Valid &&
        slot2Valid
    )
    {
        if (
            stateSlot2.generation >
            stateSlot1.generation
        )
        {
            electionState =
                stateSlot2;


            activeStateSlot =
                EEPROM_STATE_SLOT_2;
        }

        else
        {
            electionState =
                stateSlot1;


            activeStateSlot =
                EEPROM_STATE_SLOT_1;
        }


        return;
    }


    // --------------------------------------------------------
    // Only Slot 1 is valid.
    // --------------------------------------------------------

    if (slot1Valid)
    {
        electionState =
            stateSlot1;


        activeStateSlot =
            EEPROM_STATE_SLOT_1;


        return;
    }


    // --------------------------------------------------------
    // Only Slot 2 is valid.
    // --------------------------------------------------------

    if (slot2Valid)
    {
        electionState =
            stateSlot2;


        activeStateSlot =
            EEPROM_STATE_SLOT_2;


        return;
    }


    // --------------------------------------------------------
    // Neither slot is valid.
    //
    // Start with a fresh state.
    // --------------------------------------------------------

    initializeElectionState();
}


// ============================================================
//                    33. SAVE ELECTION STATE
// ============================================================
//
// Saves a new copy of the election state to the OTHER slot.
//
// Example:
//
// Current -> SLOT 1
//
// Save    -> SLOT 2
//
// Next save -> SLOT 1
//
// ============================================================

void persistElectionState()
{
    ElectionState nextState =
        electionState;


    // Make sure identifying values are correct.

    nextState.magic =
        EEPROM_MAGIC;


    nextState.version =
        EEPROM_VERSION;


    // Increase generation number.

    nextState.generation =
        electionState.generation + 1;


    // Calculate new CRC.

    nextState.crc =
        calculateStateCRC(
            nextState
        );


    // --------------------------------------------------------
    // Pick the opposite EEPROM slot.
    // --------------------------------------------------------

    int targetSlot;


    if (
        activeStateSlot ==
        EEPROM_STATE_SLOT_1
    )
    {
        targetSlot =
            EEPROM_STATE_SLOT_2;
    }

    else
    {
        targetSlot =
            EEPROM_STATE_SLOT_1;
    }


    // Write the new state.

    EEPROM.put(
        targetSlot,
        nextState
    );


    // Update RAM copy.

    electionState =
        nextState;


    // Remember active slot.

    activeStateSlot =
        targetSlot;
}


// ============================================================
//                    34. SHOW READY SCREEN
// ============================================================
//
// Two-line idle screen for the 16x2 LCD.
//
// ============================================================

void showReadyScreen()
{
    // Do not imply that voting is available until the Ballot Unit
    // has replied to a valid PING message.
    if (!ballotCommunicationReady)
    {
        displayLCD(
            "BALLOT OFFLINE",
            "WAITING FOR UNIT"
        );
        return;
    }

    if (pollOpen)
    {
        displayLCD(
            "POLL IS OPEN",
            "SCAN VOTER CARD"
        );
    }
    else
    {
        displayLCD(
            "POLL IS CLOSED",
            "ADMIN: SCAN CARD"
        );
    }
}
//                    35. RFID SELF-TEST
// ============================================================
//
// Checks whether the RC522 responds.
//
// ============================================================

bool testRFID()
{
    byte version =
        rfid.PCD_ReadRegister(
            MFRC522::VersionReg
        );


    // 0x00 and 0xFF normally indicate a bad/no response.

    return !(
        version == 0x00 ||
        version == 0xFF
    );
}


// ============================================================
//                    36. BALLOT UNIT SELF-TEST
// ============================================================
//
// Sends:
//
// PING
//
// and waits for:
//
// PONG
//
// ============================================================

// ============================================================
//                    37. ADMIN ENROLLMENT
// ============================================================
//
// Used only when no administrator RFID card has been stored.
//
// Flow:
//
// No admin card
//      ↓
// SCAN ADMIN CARD
//      ↓
// Save UID
//      ↓
// ADMIN REGISTERED
//
// ============================================================

void enrollAdminCard()
{
    displayLCD(
        "ADMIN SETUP",
        "SCAN ADMIN CARD"
    );


    byte uid[MAX_UID_LENGTH];

    byte uidLength = 0;


    // Wait until a card is scanned.

    while (
        !readRFIDCard(
            uid,
            uidLength
        )
    )
    {
        delay(20);
    }


    // Save scanned card.

    saveAdminCard(
        uid,
        uidLength
    );


    // Feedback.

    beep(2);


    displayLCD(
        "ADMIN CARD",
        "REGISTERED"
    );


    delay(800);
}


// ============================================================
//                    38. REGISTER VOTER
// ============================================================
//
// Admin scans a new voter card.
//
// Checks:
//
// - Poll must be closed
// - Voter limit must not be reached
// - Card cannot be the admin card
// - Card cannot already be registered
//
// ============================================================

void registerVoter()
{
    // --------------------------------------------------------
    // Registration is not allowed during an active poll.
    // --------------------------------------------------------

    if (pollOpen)
    {
        displayLCD(
            "CLOSE POLL FIRST",
            ""
        );


        delay(700);

        return;
    }


    // --------------------------------------------------------
    // Check voter limit.
    // --------------------------------------------------------

    byte voterCount =
        EEPROM.read(
            EEPROM_VOTER_COUNT
        );


    if (
        voterCount >=
        MAX_REGISTERED_VOTERS
    )
    {
        displayLCD(
            "VOTER LIMIT",
            "REACHED"
        );


        delay(700);

        return;
    }


    // --------------------------------------------------------
    // Ask admin to scan voter card.
    // --------------------------------------------------------

    displayLCD(
        "REGISTER VOTER",
        "SCAN CARD"
    );


    byte uid[MAX_UID_LENGTH];

    byte uidLength = 0;


    while (
        !readRFIDCard(
            uid,
            uidLength
        )
    )
    {
        delay(20);
    }


    // --------------------------------------------------------
    // Admin card cannot also be a voter card.
    // --------------------------------------------------------

    if (
        areUIDsEqual(
            uid,
            uidLength,

            adminUID,
            adminUIDLength
        )
    )
    {
        displayLCD(
            "ADMIN CARD",
            "CANNOT BE VOTER"
        );


        delay(700);

        return;
    }


    // --------------------------------------------------------
    // Check whether card is already registered.
    // --------------------------------------------------------

    if (
        findRegisteredVoter(
            uid,
            uidLength
        ) >= 0
    )
    {
        displayLCD(
            "CARD ALREADY",
            "REGISTERED"
        );


        delay(700);

        return;
    }


    // --------------------------------------------------------
    // Store new voter.
    // --------------------------------------------------------

    writeVoterToEEPROM(
        voterCount,
        uidLength,
        uid
    );


    EEPROM.update(
        EEPROM_VOTER_COUNT,
        voterCount + 1
    );


    // Feedback.

    beep(2);


    displayLCD(
        "VOTER CARD",
        "REGISTERED"
    );


    delay(700);
}


// ============================================================
//                    39. SHOW FINAL RESULTS
// ============================================================
//
// A 16x2 LCD cannot show all four candidate totals at once.
// Results are shown on pages controlled by the joystick:
// UP/DOWN scroll through pages; LEFT or press exits.
// Pages do not change automatically, so users can read them.
//
// ============================================================

void showResults()
{
    char lineBuffer[17];

    unsigned long totalVotes =
        (unsigned long)electionState.voteCount[0] +
        electionState.voteCount[1] +
        electionState.voteCount[2] +
        electionState.voteCount[3];

    // Page 0: Candidates A and B.
    // Page 1: Candidate C and NOTA.
    // Page 2: Total votes.
    // UP/DOWN changes pages. LEFT or pressing the stick exits.
    byte page = 0;

    while (true)
    {
        if (page == 0)
        {
            snprintf(
                lineBuffer,
                sizeof(lineBuffer),
                "A:%u B:%u",
                electionState.voteCount[0],
                electionState.voteCount[1]
            );

            displayLCD(
                "A/B 1/3 UP/DN",
                lineBuffer
            );
        }
        else if (page == 1)
        {
            snprintf(
                lineBuffer,
                sizeof(lineBuffer),
                "C:%u N:%u",
                electionState.voteCount[2],
                electionState.voteCount[3]
            );

            displayLCD(
                "C/N 2/3 UP/DN",
                lineBuffer
            );
        }
        else
        {
            snprintf(
                lineBuffer,
                sizeof(lineBuffer),
                "TOTAL: %lu",
                totalVotes
            );

            displayLCD(
                "TOTAL 3/3 UP/DN",
                lineBuffer
            );
        }

        // Wait for a deliberate joystick action. There is no
        // automatic timeout, so judges can read each result page.
        JoystickAction action = JOY_NONE;

        while (action == JOY_NONE)
        {
            action = (JoystickAction)readJoystickAction();
            delay(10);
        }

        if (action == JOY_UP)
        {
            page = (page + 2) % 3;
        }
        else if (action == JOY_DOWN)
        {
            page = (page + 1) % 3;
        }
        else if (action == JOY_LEFT || action == JOY_SELECT)
        {
            showReadyScreen();
            return;
        }

        // JOY_RIGHT is reserved and leaves the current page shown.
    }
}
//                    40. COUNT USED VOTERS
// ============================================================
//
// Counts how many registered voters have already voted.
//
// The usedVoterMask contains one bit per voter.
//
// ============================================================

byte countUsedVoters()
{
    byte voterCount =
        EEPROM.read(
            EEPROM_VOTER_COUNT
        );


    byte usedVoterCount = 0;


    for (
        byte i = 0;
        i < voterCount;
        i++
    )
    {
        // Check bit i.

        if (
            electionState.usedVoterMask &
            (1U << i)
        )
        {
            usedVoterCount++;
        }
    }


    return usedVoterCount;
}


// ============================================================
//                    41. SHOW SYSTEM STATUS
// ============================================================
//
// Status is shown on multiple pages because the LCD has only
// two rows.
//
// ============================================================

void showSystemStatus()
{
    char lineBuffer[17];

    snprintf(
        lineBuffer,
        sizeof(lineBuffer),
        "REG:%u USED:%u",
        EEPROM.read(EEPROM_VOTER_COUNT),
        countUsedVoters()
    );

    displayLCD(
        "SYSTEM STATUS",
        lineBuffer
    );

    delay(1400);

    snprintf(
        lineBuffer,
        sizeof(lineBuffer),
        "A:%u B:%u",
        electionState.voteCount[0],
        electionState.voteCount[1]
    );

    displayLCD(
        "VOTES: A AND B",
        lineBuffer
    );

    delay(1400);

    snprintf(
        lineBuffer,
        sizeof(lineBuffer),
        "C:%u N:%u",
        electionState.voteCount[2],
        electionState.voteCount[3]
    );

    displayLCD(
        "VOTES: C / NOTA",
        lineBuffer
    );

    delay(1400);

    showReadyScreen();
}


//                    42. EXECUTE MENU OPTION
// ============================================================
//
// Menu items:
//
// 0 -> OPEN POLL
// 1 -> CLOSE POLL
// 2 -> REGISTER VOTER
// 3 -> VIEW RESULTS
// 4 -> RESET ELECTION
// 5 -> STATUS
//
// ============================================================

void executeMenuOption()
{
    // ========================================================
    // MENU ITEM 0
    // OPEN POLL
    // ========================================================

    if (
        currentMenuIndex == 0
    )
    {
        if (pollOpen)
        {
            displayLCD(
                "POLL ALREADY",
                "OPEN"
            );
        }
        else if (!ballotCommunicationReady)
        {
            displayLCD(
                "BALLOT OFFLINE",
                "POLL NOT OPENED"
            );
        }
        else
        {
            pollOpen = true;
            adminSessionActive = false;

            displayLCD(
                "POLLING ACTIVE",
                "VOTING ENABLED"
            );
        }


        delay(800);


        showReadyScreen();


        return;
    }


    // ========================================================
    // MENU ITEM 1
    // CLOSE POLL
    // ========================================================

    if (
        currentMenuIndex == 1
    )
    {
        if (!pollOpen)
        {
            displayLCD(
                "POLL ALREADY",
                "CLOSED"
            );
        }

        else
        {
            pollOpen =
                false;


            voterSessionActive =
                false;


            currentVoterIndex =
                -1;


            // Lock Ballot Unit.

            sendMessage(
                "DISABLE",
                "POLL",
                electionState.sequence
            );


            displayLCD(
                "POLL CLOSED",
                "VOTING DISABLED"
            );
        }


        adminSessionActive =
            false;


        delay(800);


        showReadyScreen();


        return;
    }


    // ========================================================
    // MENU ITEM 2
    // REGISTER VOTER
    // ========================================================

    if (
        currentMenuIndex == 2
    )
    {
        adminSessionActive =
            false;


        registerVoter();


        showReadyScreen();


        return;
    }


    // ========================================================
    // MENU ITEM 3
    // VIEW RESULTS
    // ========================================================

    if (
        currentMenuIndex == 3
    )
    {
        adminSessionActive =
            false;


        // Results must remain hidden while voting is active.

        if (pollOpen)
        {
            displayLCD(
                "RESULTS LOCKED",
                "CLOSE POLL FIRST"
            );
        }

        else
        {
            showResults();
        }


        if (pollOpen)
        {
            delay(900);
        }


        return;
    }


    // ========================================================
    // MENU ITEM 4
    // RESET ELECTION
    // ========================================================

    if (
        currentMenuIndex == 4
    )
    {
        // Reset is not allowed during an active poll.

        if (pollOpen)
        {
            displayLCD(
                "CLOSE POLL",
                "BEFORE RESET"
            );


            delay(800);


            return;
        }


        // Ask for confirmation.

        resetConfirmationActive =
            true;


        displayLCD(
            "RESET ELECTION?",
            "PRESS=YES L=NO"
        );


        return;
    }


    // ========================================================
    // MENU ITEM 5
    // STATUS
    // ========================================================

    if (
        currentMenuIndex == 5
    )
    {
        adminSessionActive =
            false;


        showSystemStatus();


        return;
    }
}


// ============================================================
//                    43. SHOW ADMIN MENU
// ============================================================
//
// The joystick selects a single menu item on the second row.
// Move UP/DOWN to scroll, LEFT to exit, and PRESS to select.
//
// ============================================================

void showAdminMenu()
{
    const char* menuLabels[6] =
    {
        "OPEN POLL",
        "CLOSE POLL",
        "REGISTER VOTER",
        "VIEW RESULTS",
        "RESET ELECTION",
        "STATUS"
    };

    char header[17];

    snprintf(
        header,
        sizeof(header),
        "ADMIN MENU %u/6",
        currentMenuIndex + 1
    );

    char selectedItem[17];

    snprintf(
        selectedItem,
        sizeof(selectedItem),
        "> %s",
        menuLabels[currentMenuIndex]
    );

    displayLCD(
        header,
        selectedItem
    );
}


//                    44. ADMIN BUTTON HANDLER
// ============================================================
//
// Handles joystick navigation and election-reset confirmation.
// UP/DOWN scroll, LEFT exits/cancels, PRESS selects/confirms.
//
// ============================================================

void handleAdminButtons()
{
    JoystickAction action = readJoystickAction();


    // ========================================================
    // RESET CONFIRMATION SCREEN
    // PRESS = YES; LEFT = NO
    // ========================================================

    if (resetConfirmationActive)
    {
        if (action == JOY_SELECT)
        {
            // Clear voter usage.
            electionState.usedVoterMask = 0;

            // Clear all four vote counters.
            for (byte i = 0; i < 4; i++)
            {
                electionState.voteCount[i] = 0;
            }

            // Reset sequence number and save clean state.
            electionState.sequence = 0;
            persistElectionState();

            resetConfirmationActive = false;
            adminSessionActive = false;

            beep(2);
            displayLCD(
                "ELECTION RESET",
                "COUNTS CLEARED"
            );

            delay(800);
            showReadyScreen();
        }
        else if (action == JOY_LEFT)
        {
            resetConfirmationActive = false;
            showAdminMenu();
        }

        return;
    }


    // ========================================================
    // NORMAL ADMIN MENU NAVIGATION
    // ========================================================

    if (action == JOY_LEFT)
    {
        // LEFT exits the admin menu.
        adminSessionActive = false;
        showReadyScreen();
        return;
    }

    if (action == JOY_UP)
    {
        currentMenuIndex = (currentMenuIndex + 5) % 6;
        showAdminMenu();
        return;
    }

    if (action == JOY_DOWN)
    {
        currentMenuIndex = (currentMenuIndex + 1) % 6;
        showAdminMenu();
        return;
    }

    if (action == JOY_SELECT)
    {
        // Pressing the joystick selects the highlighted option.
        executeMenuOption();
        return;
    }

    // JOY_RIGHT is intentionally reserved in this version.
}


// ============================================================
//                    45. ADMIN CARD HANDLER
// ============================================================
//
// This function runs when the stored admin RFID card is
// scanned.
//
// ============================================================

void handleAdminCard()
{
    // --------------------------------------------------------
    // Do not allow admin actions in the middle of voting.
    // --------------------------------------------------------

    if (
        voterSessionActive
    )
    {
        displayLCD(
            "SYSTEM BUSY",
            "VOTE IN PROGRESS"
        );


        delay(700);


        return;
    }


    // --------------------------------------------------------
    // Enclosure must be closed.
    // --------------------------------------------------------

    if (
        !isCaseClosed()
    )
    {
        displayLCD(
            "CASE OPEN",
            "CLOSE CASE FIRST"
        );


        delay(700);


        return;
    }


    // ========================================================
    // TAMPER RECOVERY
    // ========================================================

    if (
        tamperLockActive
    )
    {
        // Clear tamper lock.

        tamperLockActive =
            false;


        // Poll remains closed after tamper recovery.

        pollOpen =
            false;


        // Reset Ballot Unit.

        sendMessage(
            "RESET",
            "TAMPER",
            electionState.sequence
        );


        displayLCD(
            "POLL CLOSED",
            "REOPEN TO VOTE"
        );


        delay(900);


        showReadyScreen();


        return;
    }


    // ========================================================
    // NORMAL ADMIN LOGIN
    // ========================================================

    adminSessionActive =
        true;


    currentMenuIndex =
        0;


    showAdminMenu();
}


// ============================================================
//                    46. RFID HANDLER
// ============================================================
//
// Every RFID scan comes through this function.
//
// Possible outcomes:
//
// ADMIN CARD
//     ↓
// Admin menu
//
// UNKNOWN CARD
//     ↓
// Reject
//
// REGISTERED UNUSED VOTER
//     ↓
// Start voting session
//
// REGISTERED USED VOTER
//     ↓
// Reject
//
// ============================================================

void handleRFID()
{
    static unsigned long lastCardReadTime =
        0;


    // Prevent immediate repeated reads.

    if (
        millis() - lastCardReadTime < 250
    )
    {
        return;
    }


    byte uid[MAX_UID_LENGTH];

    byte uidLength = 0;


    // Try reading a card.

    if (
        !readRFIDCard(
            uid,
            uidLength
        )
    )
    {
        return;
    }


    lastCardReadTime =
        millis();


    // ========================================================
    // CHECK FOR ADMIN CARD
    // ========================================================

    if (
        areUIDsEqual(
            uid,
            uidLength,

            adminUID,
            adminUIDLength
        )
    )
    {
        handleAdminCard();


        return;
    }


    // ========================================================
    // TAMPER LOCK
    // ========================================================
    //
    // No normal voter operations are allowed while locked.
    //
    // ========================================================

    if (
        tamperLockActive
    )
    {
        return;
    }


    // ========================================================
    // BALLOT UNIT MUST BE CONNECTED
    // ========================================================

    if (!ballotCommunicationReady)
    {
        displayLCD(
            "BALLOT OFFLINE",
            "TRY AGAIN LATER"
        );
        delay(600);
        return;
    }


    // ========================================================
    // POLL MUST BE OPEN
    // ========================================================

    if (!pollOpen)
    {
        displayLCD(
            "POLL CLOSED",
            "NOT ACCEPTING"
        );


        delay(600);


        return;
    }


    // ========================================================
    // ONLY ONE VOTER SESSION AT A TIME
    // ========================================================

    if (
        voterSessionActive
    )
    {
        displayLCD(
            "SYSTEM BUSY",
            "FINISH VOTE NOW"
        );


        delay(600);


        return;
    }


    // ========================================================
    // FIND VOTER
    // ========================================================

    int voterIndex =
        findRegisteredVoter(
            uid,
            uidLength
        );


    // Unknown card.

    if (
        voterIndex < 0
    )
    {
        displayLCD(
            "UNKNOWN CARD",
            "ACCESS DENIED"
        );


        beep(2);


        delay(600);


        return;
    }


    // ========================================================
    // CHECK WHETHER VOTER ALREADY VOTED
    // ========================================================

    if (
        electionState.usedVoterMask &
        (1U << voterIndex)
    )
    {
        displayLCD(
            "ALREADY VOTED",
            "ACCESS DENIED"
        );


        beep(3);


        delay(600);


        return;
    }


    // ========================================================
    // START NEW VOTER SESSION
    // ========================================================

    voterSessionActive =
        true;


    currentVoterIndex =
        voterIndex;


    // Create a new voting session number.

    electionState.sequence++;


    // Save updated sequence.

    persistElectionState();


    // Store sequence for this active voter.

    currentSessionSequence =
        electionState.sequence;


    // ========================================================
    // ENABLE BALLOT UNIT
    // ========================================================

    sendMessage(
        "ENABLE",
        "1",
        currentSessionSequence
    );


    // Tell voter what to do.

    displayLCD(
        "VOTER VERIFIED",
        "SELECT CANDIDATE"
    );


    beep(1);


    delay(400);
}


// ============================================================
//                    47. RECEIVE VOTE
// ============================================================
//
// Called when the Ballot Unit sends:
//
// VOTE|A|sequence|CRC
//
// or:
//
// VOTE|B|sequence|CRC
//
// etc.
//
// This function decides whether the vote is valid.
//
// ============================================================

void processReceivedVote(
    const String& data,
    uint32_t sequence
)
{
    // ========================================================
    // CHECK FOR ACTIVE VOTER SESSION
    // ========================================================

    if (!voterSessionActive)
    {
        sendMessage(
            "REJECT",
            "NOSESSION",
            sequence
        );


        return;
    }


    // ========================================================
    // CHECK WHETHER POLL IS STILL OPEN
    // ========================================================

    if (!pollOpen)
    {
        sendMessage(
            "REJECT",
            "POLLCLOSED",
            sequence
        );


        voterSessionActive =
            false;


        currentVoterIndex =
            -1;


        return;
    }


    // ========================================================
    // CHECK SESSION SEQUENCE
    // ========================================================
//
// The vote must belong to the currently active voter session.
//
// ========================================================

    if (
        sequence !=
        currentSessionSequence
    )
    {
        sendMessage(
            "REJECT",
            "BADSEQ",
            sequence
        );


        voterSessionActive =
            false;


        currentVoterIndex =
            -1;


        return;
    }


    // ========================================================
    // IDENTIFY CANDIDATE
    // ========================================================
//
// A -> index 0
// B -> index 1
// C -> index 2
// N -> index 3
//
// ========================================================

    int candidateIndex =
        -1;


    if (data == "A")
    {
        candidateIndex =
            0;
    }


    else if (data == "B")
    {
        candidateIndex =
            1;
    }


    else if (data == "C")
    {
        candidateIndex =
            2;
    }


    else if (data == "N")
    {
        candidateIndex =
            3;
    }


    // ========================================================
    // INVALID CANDIDATE
    // ========================================================

    if (
        candidateIndex < 0
    )
    {
        sendMessage(
            "REJECT",
            "BADCAND",
            sequence
        );


        voterSessionActive =
            false;


        currentVoterIndex =
            -1;


        return;
    }


    // ========================================================
    // COUNT THE VOTE
    // ========================================================

    electionState.voteCount[
        candidateIndex
    ]++;


    // ========================================================
    // MARK CURRENT VOTER AS USED
    // ========================================================

    electionState.usedVoterMask |=
        (1U << currentVoterIndex);


    // ========================================================
    // SAVE UPDATED ELECTION STATE
    // ========================================================

    persistElectionState();


    // ========================================================
    // TELL BALLOT UNIT THAT VOTE WAS RECORDED
    // ========================================================

    sendMessage(
        "RECORDED",
        "OK",
        sequence
    );


    // ========================================================
    // END CURRENT VOTER SESSION
    // ========================================================

    voterSessionActive =
        false;


    currentVoterIndex =
        -1;


    // ========================================================
    // USER FEEDBACK
    // ========================================================

    displayLCD(
        "VOTE RECORDED",
        ""
    );


    beep(2);


    delay(700);


    showReadyScreen();
}


// ============================================================
//                    BALLOT CONNECTION RETRY
// ============================================================
// Sends non-blocking PING attempts while the Ballot Unit is
// offline. This lets the Control Unit boot first without locking
// forever on a communications self-test.
// ============================================================

void ballotCommunicationTask()
{
    if (ballotCommunicationReady)
    {
        return;
    }

    unsigned long now = millis();

    if (ballotPingOutstanding)
    {
        if (now - ballotPingSentAt < BALLOT_PING_TIMEOUT_MS)
        {
            return;
        }

        // The response did not arrive this time. Keep the UI
        // responsive and try again after the retry interval.
        ballotPingOutstanding = false;
    }

    if ((long)(now - nextBallotPingAt) >= 0)
    {
        sendMessage("PING", "OK", 0);
        ballotPingOutstanding = true;
        ballotPingSentAt = now;
        nextBallotPingAt = now + BALLOT_PING_INTERVAL_MS;
    }
}


// ============================================================
//                    48. SERIAL TASK
// ============================================================
//
// Receives characters from the Ballot Unit.
//
// Example incoming message:
//
// VOTE|B|15|A7
//
// Characters are collected until '\n' is received.
//
// ============================================================

void serialTask()
{
    while (
        Serial.available()
    )
    {
        char receivedCharacter =
            (char)Serial.read();


        // ====================================================
        // COMPLETE MESSAGE RECEIVED
        // ====================================================

        if (
            receivedCharacter == '\n'
        )
        {
            receivedSerialData.trim();


            if (
                receivedSerialData.length() > 0
            )
            {
                String command;

                String data;

                uint32_t sequence = 0;


                // Validate message and CRC.

                bool valid =
                    parseMessage(
                        receivedSerialData,
                        command,
                        data,
                        sequence
                    );


                if (valid && command == "PONG" && data == "OK")
                {
                    // The Ballot Unit is now online. PONG is only
                    // accepted after the message CRC has validated.
                    ballotCommunicationReady = true;
                    ballotPingOutstanding = false;

                    if (systemReady && !adminSessionActive && !voterSessionActive)
                    {
                        showReadyScreen();
                    }
                }
                else if (valid && command == "VOTE")
                {
                    processReceivedVote(
                        data,
                        sequence
                    );
                }
            }


            // Clear buffer for next message.

            receivedSerialData = "";
        }


        // ====================================================
        // STORE RECEIVED CHARACTER
        // ====================================================

        else if (
            receivedSerialData.length() < 60
        )
        {
            receivedSerialData +=
                receivedCharacter;
        }


        // ====================================================
        // MESSAGE TOO LONG
        // ====================================================

        else
        {
            receivedSerialData = "";
        }
    }
}


// ============================================================
//                    49. TAMPER MONITOR
// ============================================================
//
// Detects:
//
// CASE CLOSED
//      ↓
// CASE OPEN
//
// while the poll is active.
//
// ============================================================

void tamperTask()
{
    // Nothing to monitor until a physical tamper switch is fitted.
    if (!TAMPER_DETECTION_ENABLED)
    {
        return;
    }

    // Remember previous state.

    static bool wasCaseClosed = true;


    // Read current state.

    bool isNowCaseClosed =
        isCaseClosed();


    // ========================================================
    // CASE HAS JUST BEEN OPENED DURING ACTIVE POLL
    // ========================================================

    if (
        pollOpen &&
        wasCaseClosed &&
        !isNowCaseClosed
    )
    {
        // Lock system.

        tamperLockActive =
            true;


        // Close poll immediately.

        pollOpen =
            false;


        // Cancel active voter session.

        voterSessionActive =
            false;


        currentVoterIndex =
            -1;


        // Tell Ballot Unit to disable voting.

        sendMessage(
            "DISABLE",
            "TAMPER",
            electionState.sequence
        );


        // Alert administrator.

        displayLCD(
            "TAMPER DETECTED",
            "SYSTEM LOCKED"
        );


        beep(4);
    }


    // Save current case state for next loop.

    wasCaseClosed =
        isNowCaseClosed;
}


// ============================================================
//                    50. SETUP
// ============================================================
//
// Runs ONCE when the Control Unit powers on.
//
// ============================================================

void setup()
{
    // ========================================================
    // ADMIN JOYSTICK SETUP
    // ========================================================

    pinMode(JOYSTICK_X_PIN, INPUT);
    pinMode(JOYSTICK_Y_PIN, INPUT);
    pinMode(JOYSTICK_SW_PIN, INPUT_PULLUP);


    // ========================================================
    // OPTIONAL TAMPER SWITCH
    // ========================================================
    // Disabled for this build because no spare switch is available.
    // D6 is reserved. Do not claim tamper detection is active.
    // ========================================================

    if (TAMPER_DETECTION_ENABLED)
    {
        pinMode(TAMPER_SWITCH, INPUT_PULLUP);
    }


    // ========================================================
    // OUTPUTS
    // ========================================================

    pinMode(
        STATUS_LED,
        OUTPUT
    );


    pinMode(
        BUZZER_PIN,
        OUTPUT
    );


    // ========================================================
    // SERIAL COMMUNICATION
    // ========================================================

    Serial.begin(9600);


    // ========================================================
    // LCD
    // ========================================================

    lcd.init();

    lcd.backlight();


    // ========================================================
    // RFID
    // ========================================================

    SPI.begin();


    rfid.PCD_Init();


    delay(50);


    // ========================================================
    // LOAD SAVED DATA
    // ========================================================

    loadSystemConfiguration();


    loadElectionState();


    // ========================================================
    // STARTUP SCREEN
    // ========================================================

    displayLCD(
        "A.S.E. EVM",
        "SYSTEM STARTING"
    );


    delay(500);


    // ========================================================
    // RFID SELF-TEST
    // ========================================================

    if (
        !testRFID()
    )
    {
        displayLCD(
            "SELF TEST FAILED",
            "RFID ERROR"
        );


        // Stay here permanently.

        while (true)
        {
            delay(100);
        }
    }


    // ========================================================
    // BALLOT COMMUNICATION
    // ========================================================
    // Do not halt setup if the Ballot Unit is powered later.
    // loop() retries PING until a valid PONG is received.
    // Poll opening and voter authentication remain blocked
    // until ballotCommunicationReady becomes true.
    // ========================================================

    ballotCommunicationReady = false;
    ballotPingOutstanding = false;
    nextBallotPingAt = millis();

    displayLCD(
        "WAIT FOR BALLOT",
        "CHECK POWER/LINK"
    );


    // ========================================================
    // CASE STATUS SELF-TEST
    // ========================================================

    if (
        !isCaseClosed()
    )
    {
        displayLCD(
            "SELF TEST FAILED",
            "CLOSE CASE"
        );


        while (true)
        {
            delay(100);
        }
    }


    // ========================================================
    // SYSTEM STATUS LED
    // ========================================================

    digitalWrite(
        STATUS_LED,
        HIGH
    );


    // ========================================================
    // FIRST-BOOT ADMIN ENROLLMENT
    // ========================================================

    if (
        adminUIDLength == 0
    )
    {
        enrollAdminCard();
    }


    // ========================================================
    // SYSTEM READY
    // ========================================================

    systemReady =
        true;


    showReadyScreen();
}


// ============================================================
//                    51. MAIN LOOP
// ============================================================
//
// The Control Unit repeatedly performs its main background jobs:
//
// 1. Receive messages from Ballot Unit
// 2. Monitor tamper switch
// 3. Handle admin menu
// 4. Handle RFID cards
//
// ============================================================

void loop()
{
    // ========================================================
    // 1. SERIAL COMMUNICATION
    // ========================================================

    serialTask();

    ballotCommunicationTask();


    // ========================================================
    // 2. TAMPER MONITORING
    // ========================================================

    tamperTask();


    // ========================================================
    // SYSTEM MUST BE READY
    // ========================================================

    if (!systemReady)
    {
        return;
    }


    // ========================================================
    // TAMPER LOCK MODE
    // ========================================================
    //
    // During tamper lock:
    //
    // - Normal voters are blocked
    // - Admin card can recover the system
    //
    // ========================================================

    if (
        tamperLockActive
    )
    {
        handleRFID();


        delay(5);


        return;
    }


    // ========================================================
    // ADMIN MENU
    // ========================================================

    if (
        adminSessionActive
    )
    {
        handleAdminButtons();
    }


    // ========================================================
    // RFID
    // ========================================================

    handleRFID();
}