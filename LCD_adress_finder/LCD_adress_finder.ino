
/*
============================================================
                 A.S.E. EVM
              I2C ADDRESS SCANNER
============================================================

Purpose:
Find the I2C address of our 16x2 LCD.

Connections:
LCD VCC  -> Arduino 5V
LCD GND  -> Arduino GND
LCD SDA  -> Arduino A4
LCD SCL  -> Arduino A5

Open Serial Monitor at 9600 baud.
============================================================
*/


// ==========================================================
// 1. REQUIRED LIBRARY
// ==========================================================

#include <Wire.h>


// ==========================================================
// 2. SETUP
// ==========================================================

void setup()
{
    // Start serial communication with the computer.
    Serial.begin(9600);

    // Start I2C communication.
    Wire.begin();

    // Give the Serial Monitor time to initialize.
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("      I2C ADDRESS SCANNER");
    Serial.println("================================");

    Serial.println("Scanning for I2C devices...");
    Serial.println();
}


// ==========================================================
// 3. MAIN LOOP
// ==========================================================

void loop()
{
    byte address;

    byte error;

    byte devicesFound = 0;


    // Search valid 7-bit I2C addresses.
    for (address = 1; address < 127; address++)
    {
        // Contact the device at this address.
        Wire.beginTransmission(address);

        // Check whether a device responds.
        error = Wire.endTransmission();


        // Error code 0 means a device acknowledged us.
        if (error == 0)
        {
            Serial.print("Device found at 0x");

            // Print the address in hexadecimal.
            if (address < 16)
            {
                Serial.print("0");
            }

            Serial.println(address, HEX);

            devicesFound++;
        }
    }


    // Report the scan results.
    Serial.println();

    if (devicesFound == 0)
    {
        Serial.println("No I2C devices found.");
        Serial.println("Check wiring and power.");
    }
    else
    {
        Serial.print("Devices found: ");
        Serial.println(devicesFound);
    }

    Serial.println();
    Serial.println("Scanning again in 3 seconds...");
    Serial.println("--------------------------------");


    // Wait before repeating the scan.
    delay(3000);
}
