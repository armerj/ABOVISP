#include <digitalWriteFast.h>

#define ABOV_SDAT_PIN 9
#define ABOV_SCLK_PIN 8
#define ABOV_VDD_PIN 7
#define ABOV_VPP_PIN 6
#define ABOV_REG_PIN 5 // connected to boost regulator enable pin

#define MAX_FLASH_SIZE 4096;

#define FASTINTERVAL 5


byte inputBuffer = 0;
bool msbHasBeenReceived = false;

// 0: not listening, 1: load flash, 2: load boot
int entryMode = 0;

bool powerToggleIsOn = false;

byte trimC0 = 0x00;
byte trimC1 = 0x00;
byte trimC2 = 0x00;
byte trimC3 = 0x00;
byte testReadByte = 0x00;

byte configByte = 0x24; // 8MHz, 2.7V LVR, Unlocked

// if the erase process can't find a chip, the subsequent flash process shouldn't continue until the commands finish
byte shouldNotContinueProgramming = false;

void setupPins()
{
	pinModeFast(ABOV_SDAT_PIN, 		OUTPUT);
	pinModeFast(ABOV_SCLK_PIN, 		OUTPUT);
	pinModeFast(ABOV_VDD_PIN, 		OUTPUT);
	pinModeFast(ABOV_VPP_PIN, 		OUTPUT);
	pinModeFast(ABOV_REG_PIN, 		OUTPUT);

	digitalWriteFast(ABOV_SDAT_PIN, 	LOW);
	digitalWriteFast(ABOV_SCLK_PIN, 	LOW);
	digitalWriteFast(ABOV_VDD_PIN, 		LOW);
	digitalWriteFast(ABOV_VPP_PIN, 		HIGH);
	digitalWriteFast(ABOV_REG_PIN, 		HIGH);
}

void enterIsp()
{
	// assumes all pins are LOW

	digitalWriteFast(ABOV_VDD_PIN, HIGH);
	// delay by tVD.S (min 10ms, max 20ms)
	delay(11);
	digitalWriteFast(ABOV_VPP_PIN, LOW);
	// delay by tVP.S (min 40ms)
	delay(41);
	// after this, SCLK can rise,
	digitalWriteFast(ABOV_SCLK_PIN, HIGH);
	// delay by tBG.S (min 1us) / tVC.S (min 1us)
	delayMicroseconds(1);
	// SDAT rises
	digitalWriteFast(ABOV_SDAT_PIN, HIGH);
	// tBG.H (min 1us)
	delayMicroseconds(1);

	// turn off SCLK, then SDAT
	// arbitrary 20us * 2 "just in case" - because they ask for 40us 'stable prog'
	// this is probably a first of the dummy clocks
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SCLK_PIN, LOW);
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SDAT_PIN, LOW);
	delayMicroseconds(20);

}

void exitIsp()
{
	// SDAT should never be an input here bc commands end by sending a dummy byte

	// assumes all pins are HIGH
	delayMicroseconds(20); // tED.S (min 1us)
	digitalWriteFast(ABOV_SDAT_PIN, LOW);
	delayMicroseconds(1); // tDIF (min 1us) OR tVC.H (min 1us)
	digitalWriteFast(ABOV_SCLK_PIN, LOW);
	delay(41); // tVP.H (min 40ms)
	// digitalWriteFast(ABOV_VPP_PIN, HIGH);
	delay(11); // tVD.H (min 10ms)
	// digitalWriteFast(ABOV_VDD_PIN, LOW);
}

void bootNormally()
{
	// this might break existing stuff. reset device or call setupPins(), enterIsp() again after using

	delayMicroseconds(20); // tED.S (min 1us)
	digitalWriteFast(ABOV_SDAT_PIN, LOW);
	delayMicroseconds(1); // tDIF (min 1us) OR tVC.H (min 1us)
	digitalWriteFast(ABOV_SCLK_PIN, LOW);
	delay(41); // tVP.H (min 40ms)
	digitalWriteFast(ABOV_VPP_PIN, HIGH);
	delay(11); // tVD.H (min 10ms)
	digitalWriteFast(ABOV_VDD_PIN, LOW);
	delay(300);
	digitalWriteFast(ABOV_VDD_PIN, HIGH);

	pinModeFast(ABOV_SDAT_PIN, INPUT);
	pinModeFast(ABOV_SCLK_PIN, INPUT);
}

void sendByte(byte inByte)
{
	// assumes sdat & sclk are LOW

	// this is really slow but it probably conforms to the best minimum timings for safety
	// msb first!
	for (int i = 7; i >= 0; i--)
	{
		byte bitToSend = (inByte & (1 << i)) >> i;
		digitalWriteFast(ABOV_SDAT_PIN, bitToSend);
		delayMicroseconds(20);
		digitalWriteFast(ABOV_SCLK_PIN, HIGH);
		delayMicroseconds(20);
		digitalWriteFast(ABOV_SCLK_PIN, LOW);
		delayMicroseconds(20);
		digitalWriteFast(ABOV_SDAT_PIN, LOW);
		delayMicroseconds(20);
	}

	// send dummy bit
	digitalWriteFast(ABOV_SDAT_PIN, HIGH);
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SCLK_PIN, HIGH);
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SCLK_PIN, LOW);
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SDAT_PIN, LOW);
	delayMicroseconds(20);

}

byte readByte()
{
	pinModeFast(ABOV_SDAT_PIN, INPUT);
	byte byteResult = 0;
	for (int i = 0; i < 8; i++)
	{
		digitalWriteFast(ABOV_SCLK_PIN, HIGH);
		delayMicroseconds(20);

		byteResult = byteResult << 1;
		if (digitalReadFast(ABOV_SDAT_PIN) != 0)
		{
			byteResult = byteResult | 1;
		}
		delayMicroseconds(40);
		digitalWriteFast(ABOV_SCLK_PIN, LOW);
		delayMicroseconds(20);
	}

	pinModeFast(ABOV_SDAT_PIN, OUTPUT);

	// send dummy bit
	digitalWriteFast(ABOV_SDAT_PIN, HIGH);
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SCLK_PIN, HIGH);
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SCLK_PIN, LOW);
	delayMicroseconds(20);
	digitalWriteFast(ABOV_SDAT_PIN, LOW);
	delayMicroseconds(20);

	return byteResult;
}

byte readByteFast()
{

	pinModeFast(ABOV_SDAT_PIN, INPUT);
	byte byteResult = 0;
	for (int i = 0; i < 8; i++)
	{
		digitalWriteFast(ABOV_SCLK_PIN, HIGH);
		delayMicroseconds(FASTINTERVAL);

		byteResult = byteResult << 1;
		if (digitalReadFast(ABOV_SDAT_PIN) != 0)
		{
			byteResult = byteResult | 1;
		}
		delayMicroseconds(FASTINTERVAL);
		digitalWriteFast(ABOV_SCLK_PIN, LOW);
		delayMicroseconds(FASTINTERVAL);
	}

	pinModeFast(ABOV_SDAT_PIN, OUTPUT);

	// send dummy bit
	digitalWriteFast(ABOV_SDAT_PIN, HIGH);
	delayMicroseconds(FASTINTERVAL);
	digitalWriteFast(ABOV_SCLK_PIN, HIGH);
	delayMicroseconds(FASTINTERVAL);
	digitalWriteFast(ABOV_SCLK_PIN, LOW);
	delayMicroseconds(FASTINTERVAL);
	digitalWriteFast(ABOV_SDAT_PIN, LOW);
	delayMicroseconds(FASTINTERVAL);

	return byteResult;
}

void sendDummyByte()
{
	// 8 + 1 dummy bit
	for (int i = 0; i < 8; i++)
	{
		digitalWriteFast(ABOV_SDAT_PIN, HIGH);
		delayMicroseconds(20);
		digitalWriteFast(ABOV_SCLK_PIN, HIGH);
		delayMicroseconds(20);
		digitalWriteFast(ABOV_SCLK_PIN, LOW);
		delayMicroseconds(20);
		// digitalWriteFast(ABOV_SDAT_PIN, LOW); // dummy: leave it HIGH
		delayMicroseconds(20);
	}

	digitalWriteFast(ABOV_SCLK_PIN, HIGH);
}

void setup()
{
	Serial.begin(9600);
	setupPins();
}

byte readByteFromAddress(byte highAddr, byte lowAddr)
{
	enterIsp();
	sendByte(0xA1);
	sendByte(highAddr);
	sendByte(lowAddr);
	byte readResult = readByte();
	sendDummyByte();
	exitIsp();
	return readResult;
}

void writeByteAtAddress(byte highAddr, byte lowAddr, byte dataToWrite)
{
	enterIsp();
	sendByte(0x41);
	sendByte(highAddr);
	sendByte(lowAddr);
	sendByte(dataToWrite);
	sendDummyByte();
	exitIsp();
}

byte programErase(byte highAddr, byte lowAddr, byte command, int waitTimeMs)
{
	enterIsp();
	sendByte(command);
	sendByte(highAddr);
	sendByte(lowAddr);
	delay(waitTimeMs);


	for (int i = 0; i < 3; i++)
	{
		// send 3 dummy bits
		digitalWriteFast(ABOV_SCLK_PIN, HIGH);
		delayMicroseconds(20);
		digitalWriteFast(ABOV_SCLK_PIN, LOW);
		delayMicroseconds(20);
	}

	pinModeFast(ABOV_SDAT_PIN, INPUT);
	byte byteResult = 0;
	for (int i = 0; i < 8; i++)
	{
		digitalWriteFast(ABOV_SCLK_PIN, HIGH);
		delayMicroseconds(20);

		byteResult = byteResult << 1;
		if (digitalReadFast(ABOV_SDAT_PIN) != 0)
		{
			//result[i] = 1;
			byteResult = byteResult | 1;
		}
		delayMicroseconds(40);
		digitalWriteFast(ABOV_SCLK_PIN, LOW);
		delayMicroseconds(20);
	}

	for (int i = 0; i < 3; i++)
	{
		// send 3 dummy bits
		digitalWriteFast(ABOV_SCLK_PIN, HIGH);
		delayMicroseconds(20);
		digitalWriteFast(ABOV_SCLK_PIN, LOW);
		delayMicroseconds(20);

		if (i == 1)
		{
			// switch SDAT to output on second dummy bit (according to datasheet)
			pinModeFast(ABOV_SDAT_PIN, OUTPUT);
		}
	}

	exitIsp();
	return byteResult;
} 

void printPaddedHex(byte printMe)
{
	if (printMe < 0x10)
	{
		Serial.print(0);
	}
	Serial.print(printMe, HEX);
}

void printDivider()
{
	Serial.println("=============================================");
}

void loop()
{
	if (Serial.available() > 0) {
		Serial.println("Press 1 to read infomation on connected board...");
		Serial.println("Press 2 to attempt to edit on connected board...");
		Serial.println("Press 3 to attempt to flash only part of firmware to remove lock bit...");
		Serial.println("Press 4 to attempt to read interrupt and reset vectors...");
		Serial.println("Press 5 to attempt to read full firmware...");
		char inChar = Serial.read();

		if (inChar == '1') {
			setupPins();

			// Check DeviceID
			Serial.println("Checking DeviceID");
			if (readByteFromAddress(0x21, 0x06) == 0xF0) {
				Serial.println("DeviceID is valid (0xF0)");

			} else  {
				Serial.println("DeviceID is not 0xF0 (MC81F4204) - exiting");
				shouldNotContinueProgramming = true;
				return;
			}

			// Save Trim values
			trimC0 = readByteFromAddress(0x20, 0xC0);
			trimC1 = readByteFromAddress(0x20, 0xC1);
			trimC2 = readByteFromAddress(0x20, 0xC2);
			trimC3 = readByteFromAddress(0x20, 0xC3);
			testReadByte= readByteFromAddress(0x20, 0xC7);

			Serial.println("Trim values are as follows");
			printPaddedHex(trimC0);
			printPaddedHex(trimC1);
			printPaddedHex(trimC2);
			printPaddedHex(trimC3);

			Serial.println("Lock byte value is as follows");
			printPaddedHex(testReadByte);

		} else if (inChar == '2') {
			setupPins();
			Serial.println("Attempting to read firmware");

			testReadByte = readByteFromAddress(0x0F, 0xFF);
			Serial.println(Entry point Upper byte");
			printPaddedHex(testReadByte);
			testReadByte = readByteFromAddress(0x0F, 0xFE);
			Serial.println(Entry point Lower byte");
			printPaddedHex(testReadByte);

			Serial.println("Attempting to edit config");
			writeByteAtAddress(0x20, 0xC7, 0x24); // 0x2c -> 0x24 or 0x6c (2c: locked, 24: unlocked)
			// 0x24 = b100100, 4th bit is lock bit, lock bit = 0 means unlocked
			// 0x2c = b101100
			// "As already mentioned, the default state for NOR flash and other non-volatile memories 
			// like NAND flash, EEPROMs and even EPROMs is a logic 1. You cannot program 1's into these 
			// devices, you can only program 0's" from
			// https://electronics.stackexchange.com/questions/179701/why-do-most-of-the-non-volatile-memories-have-logical-1-as-the-default-state

			Serial.println("Rereading firmware");

			testReadByte = readByteFromAddress(0x0F, 0xFF);
			Serial.println(Entry point Upper byte");
			printPaddedHex(testReadByte);
			testReadByte = readByteFromAddress(0x0F, 0xFE);
			Serial.println(Entry point Lower byte");
			printPaddedHex(testReadByte);
		} else if (inChar == '3') {
			setupPins();
			Serial.println("Attempting to flash as little as possible");

			// Save Trim values
			trimC0 = readByteFromAddress(0x20, 0xC0);
			trimC1 = readByteFromAddress(0x20, 0xC1);
			trimC2 = readByteFromAddress(0x20, 0xC2);
			trimC3 = readByteFromAddress(0x20, 0xC3);

			// Send preprogram command
			Serial.print("Sending preprogram command.. ");
			// 4K byte is 112,370us, we are gonna try for (10+64)*27us + 50us = 2,048us

			programErase(0x20, 0xC0, 0xEA, 2); // send preprogram command
			// don't care if success 
			programErase(0x00, 0x00, 0x81, 3) // erase for least amount of time

			// Write back Trim values
			Serial.print("Restoring Trim values..");
			writeByteAtAddress(0x20, 0xC0, trimC0); // LVR Vref Trim (bits 7654: Vref Up, bits 3210 Vref Down) @ 0x00FE
			writeByteAtAddress(0x20, 0xC1, trimC1); // OSC Trim @ 0x00FF
			writeByteAtAddress(0x20, 0xC2, trimC2); // Op-amp Trim (bits 7: IDBLE, bits 6543210 Op-amp trim data) @ 0x00E2
			writeByteAtAddress(0x20, 0xC3, trimC3); // Comparator0 Trim (bits 7: IHALF, bits 654321: Comparator0 trim data) @ 0x00E3

			Serial.println("Attempting to set unlock byte");
			writeByteAtAddress(0x20, 0xC7, 0x24); // 0x2c -> 0x24 or 0x6c (2c: locked, 24: unlocked)
			
			Serial.println("Trying to reset device and read firmware");
			bootNormally();
			delay(500);

			Serial.println("Reading reset vector");

			testReadByte = readByteFromAddress(0x0F, 0xFF);
			Serial.println(Entry point Upper byte");
			printPaddedHex(testReadByte);
			testReadByte = readByteFromAddress(0x0F, 0xFE);
			Serial.println(Entry point Lower byte");
			printPaddedHex(testReadByte);

		} else if (inChar == '4') {
			setupPins();
			Serial.println("Attempting to read interrupt and reset vectors");
			
			for (i = 0xE0; i <= 0xFF; i++) {
				testReadByte = readByteFromAddress(0xFF, i);
				printPaddedHex(testReadByte);
			}
		} else if (inChar == '5') {
			setupPins();
			Serial.println("Attempting to read full firmware");
			for (i = 0xF0; i <= 0xFF; i++) {
				for (j = 0x00; j <= 0xFF; j++) {
					testReadByte = readByteFromAddress(i, f);
					printPaddedHex(testReadByte);
				}
			}
		}
	}				
}
