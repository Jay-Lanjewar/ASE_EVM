A.S.E. EVM v0.1 LAB PACKAGE

This package contains the first integrated firmware baseline.

FILES
1. ase_evm_ballot_v0_1.ino
2. ase_evm_control_v0_1.ino
3. WIRING_AND_LAB_SHEET.txt

CONTROL UNO LIBRARIES
- MFRC522
- LiquidCrystal_I2C
- SPI
- Wire
- EEPROM

SIMULATION
Wokwi currently supports Arduino Uno and the MFRC522, but its documentation
states that multiple microcontrollers cannot currently be connected together
inside one Wokwi project. Use Wokwi to simulate/test each unit separately.
For a full two-Arduino schematic simulation, Proteus is the more suitable
option when available.

DO NOT PERMANENTLY BUILD YET.
First make the breadboard version work and test each subsystem.
