/*
 Name:		PoolControllerArduinoSide.ino
 Created:	8/22/2017 8:21:58 PM
 Author:	vfilauro
*/


#include <SoftwareSerial.h>
#include <MemoryFree.h>
#include <Time.h>
#include <TimeAlarms.h>

#define DEBUG 0  //0: nothing; 1: status; 10: wip 20: protocol;  99 all

#define SSerialRX  11  //Serial Receive pin
#define SSerialTX  12  //Serial Transmit pin
#define SSerialTxControl  4  //RS485 Direction control

#define RS485Transmit    HIGH
#define RS485Receive     LOW


SoftwareSerial poolSerial = SoftwareSerial(SSerialRX, SSerialTX);

byte buffer[200];
byte* bufptr;

//Display LEDs bits from pool (to be read by Arduino)
//Byte 1
#define heaterOn          0x01
#define solarOn           0x02
#define checkSystemOn     0x04
#define poolOn            0x08
#define spaOn             0x10
#define filterOn          0x20
#define lightOn           0x40
#define spaLightOn        0x80
//Byte 2
#define aux2On            0x01
#define serviceOn         0x02
#define aux3On            0x04
#define aux4On            0x08
#define aux5On            0x10
#define aux6On            0x20
#define Valve4On          0x40
#define spilloverOn       0x80
//Byte 3
#define systemOffOn       0x01
#define aux7On            0x02
#define aux8On            0x04
#define aux9On            0x08
#define aux10On           0x10
#define aux11On           0x20
#define aux12On           0x40
#define aux13On           0x80
//Byte 4
#define aux14On           0x01
#define superChlorOn      0x02


//Arduino's internal storage bits for items to be controlled (need to fit into a Byte)
#define filterBit         0x80
#define poolSpaBit        0x40
#define solarBit          0x20
#define lightBit          0x10
#define spaLightBit       0x08
#define serviceBit        0x04
#define superChlorOnBit   0x02
#define heaterBit         0x01

//commands to be sent to pool to toggle respective item on and/or off
byte filterData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x96, 0x10, 0x03 };
byte poolSpaData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x16, 0x10, 0x03 };
byte solarData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x98, 0x10, 0x03 };
byte lightData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x98, 0x10, 0x03 };
byte spaLightData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x9A, 0x10, 0x03 };
byte serviceData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x10, 0x03 };
byte superChlorData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x9E, 0x10, 0x03 };
byte heaterData[] = { 0x10, 0x02, 0x00, 0x83, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x9E, 0x10, 0x03 };

byte poolStatusFlag = 0;  // most recent pool status
byte newStatusFlag = 0;
boolean sendPending = false;
bool okToSend = false;
byte* commandPtr = NULL;

int poolTemp = 0;
int airTemp = 0;
int poolChlor = 0;
int saltLevel = 0;
int filterSpeed = 0;

enum frameState { WAIT_FOR_START, START, DATA, POSSIBLE_END, END };
enum frameState myFrameState;


void setup()   /****** SETUP: RUNS ONCE ******/
{
	// Start the built-in serial port, probably to Serial Monitor
	Serial.begin(115200);

#if DEBUG >=1
	Serial.println("Initializing...");
#endif

	// Start the software serial port, to the pool controller
	poolSerial.begin(19200);


	pinMode(SSerialTxControl, INPUT);
	digitalWrite(SSerialTxControl, HIGH);  // set internal pull up

	pinMode(SSerialTxControl, OUTPUT);
	digitalWrite(SSerialTxControl, RS485Receive);

	memset(buffer, 0, sizeof(buffer));
	bufptr = buffer;
	commandPtr = NULL;
	sendPending = false;
	myFrameState = WAIT_FOR_START;


#if DEBUG >=10
	showMem();
#endif
#if DEBUG >=1
	Serial.println("done");
#endif

}//--(end setup )---



void loop()   /****** LOOP: RUNS CONSTANTLY ******/
{

	handleConsole();
	Alarm.delay(0);  //just to make the alarms work

	handlePool();

}//--(end main loop )---

 /*-----( Declare User-written Functions )-----*/

void showMem() {
	Serial.print("Free Memory = ");
	Serial.println(freeMemory());
}


void handlePool() {

	byte byteReceived;

	if (poolSerial.available())  //Look for data from the pool controller
	{
		byteReceived = (byte)poolSerial.read();    // Read received byte
		switch (myFrameState) {
		case WAIT_FOR_START:
			if (byteReceived == 0x10) {
				myFrameState = START;
			}
			else {
				//probably junk, but keep this for future debugging
				//          Serial.print (byteReceived,HEX);
				//          Serial.println (" was received after 0x10");
				//myFrameState = WAIT_FOR_START;
			}
			break;
		case START:
			if (byteReceived == 0x02) {
				myFrameState = DATA;
			}
			else {
				//probably junk, but keep this for future debugging
				myFrameState = WAIT_FOR_START;
			}
			break;
		case DATA:
			if (byteReceived == 0x10) {
				myFrameState = POSSIBLE_END;
			}
			*bufptr++ = (byte)byteReceived;  //capture the characters in the  command/data buffer
			break;
		case POSSIBLE_END:
			if (byteReceived == 0x00) {
				// per AQUAPOOL PROTOCOL MANUAL, a NULL byte is appended to any 0x10 byte which is part of command or data
				myFrameState = DATA;
				//we can discard the NULL, but keep the previous 0x10
				break;
			}
			else if (byteReceived != 0x03) {
				//probably junk, but keep this for future debugging
#if DEBUG >=1
				Serial.print(byteReceived, HEX);
				Serial.println(" was received instead of 0x02 or 0x03");
#endif
				myFrameState = POSSIBLE_END;
				break;
			}
			else { //getting the expected 0x03 end packet will get us here
				   // need to discard the previous 0x10
				bufptr--;
				*bufptr = 0;

				myFrameState = END;
				//okToSend = true;
				//do NOT break
			}
		case END:
			if (sendPending && okToSend) {
				poolSend();
#if DEBUG >=20
				Serial.println("Command Sent");
#endif
				Alarm.timerOnce(2, poolActionCheck);  //in 2 seconds, verify the positive outcome of send command
				sendPending = false;
				//okToSend = true;
			}
			processPoolFrame((int)(bufptr - buffer));
			memset(buffer, 0, sizeof(buffer));
			bufptr = buffer;
			myFrameState = WAIT_FOR_START;
			break;
		default:
			//should never be here
#if DEBUG >=1
			Serial.print("WARNING: unknown frameState: ");
			Serial.println(myFrameState);
#endif
			myFrameState = WAIT_FOR_START;
		}// end switch loop    
	}//end if loop  
}

void processPoolFrame(int length) {
	int i = 0;

	//calculate checksum of received packet
	unsigned int checksum = 0x12; // sum of 0x10 + 0x02
	for (i = 0; i < length - 2; i++) {
		checksum += (unsigned int)buffer[i];
	}
	if (checksum != (unsigned int)buffer[i] * 256 + (unsigned int)buffer[i + 1]) {
		//checksum failed
#if DEBUG >=1
		Serial.println("WARNING: checksum failed! discarding packet");
#endif
		return;
	}

	if (buffer[0] == 0x01) {  // Display command
		if (buffer[1] == 0x01) {  // some kind of keep-alive - ignoring for now
			okToSend = true;
			return;
		}
		// handle this like an interrupt.  Only set flags and deal with them after
		// the packet has been decoded.  There is not enough time to do serial prints
		// for information.  It's ok, to use them for debugging
		if (buffer[1] == 0x02) { // led display command
			poolStatusFlag = (buffer[2] & filterOn) ? poolStatusFlag | filterBit :
				poolStatusFlag & ~filterBit;
			poolStatusFlag = (buffer[2] & solarOn) ? poolStatusFlag | solarBit :
				poolStatusFlag & ~solarBit;
			poolStatusFlag = (buffer[2] & lightOn) ? poolStatusFlag | lightBit :
				poolStatusFlag & ~lightBit;
			poolStatusFlag = (buffer[2] & spaLightOn) ? poolStatusFlag | spaLightBit :
				poolStatusFlag & ~spaLightBit;
			//TO-DO: add checks for additional controls defined
		}
		else if (buffer[1] == 0x03) { // Display update command
#if DEBUG >= 20      
			Serial.print((char*)buffer);
			Serial.println();
#endif       
			if (strncmp((char *)buffer + 4, "Pool Temp", 9) == 0) {
				poolTemp = atoi((char *)buffer + 13);
			}
			else if (strncmp((char *)buffer + 4, "Air Temp", 8) == 0) {
				airTemp = atoi((char *)buffer + 12);
			}
			else if (strncmp((char *)buffer + 4, "Pool Chlorinator", 16) == 0) {
				poolChlor = atoi((char *)buffer + 20);
			}
			else if (strncmp((char *)buffer + 7, "Salt Level", 10) == 0) {
				saltLevel = atoi((char *)buffer + 17);
			}
			else if (strncmp((char *)buffer + 6, "Filter Speed", 12) == 0) {
				filterSpeed = atoi((char *)buffer + 18);
			}
		} //buffer[1] checks     
	} // buffer[0] check
	else if (buffer[0] == 0x00 && buffer[1] == 0x83) {
		/*
		0x00, 0x83 is the command string sent by the wireless controller
		below are the bit patterns from the various buttons.  These are repeated
		twice in the message so 00 83 00 01 00 00 00 01 00 00 would be a light button
		80 00 00 00   filter button
		20 00 00 00   plus sign  (- doesn't send sometimes)
		40 00 00 00   pool button
		01 00 00 00   right arrow
		02 00 00 00   menu button
		04 00 00 00   left arrow
		08 00 00 00   System off button
		00 01 00 00   Light
		00 02 00 00   Waterfall    (Aux 1)
		00 04 00 00   Fountain     (Aux 2)
		00 00 01 00   Solar Heater (Valve 3)
		00 00 02 00   Unused switch on bottom
		00 00 04 00   Unused switch in middle
		*/
		//    printFrameData(buffer, len); // this will print command buffers outgoing from remote
	}
	else {
#if DEBUG >=20
		Serial.print("Unrecognized Command Byte is: ");
		Serial.println(buffer[0], HEX);
#endif
	}
}


void poolActionCheck() {
	if (poolStatusFlag != newStatusFlag) {
		sendPending = true;
	}
	else {
#if DEBUG>=1
		Serial.println("Command send was verified");
#endif
	}
}


void poolSend() {
	for (int count = 0; count < 2; count++) {
		digitalWrite(SSerialTxControl, RS485Transmit);  // Enable RS485 Transmit   
		for (int i = 0; i < sizeof(lightData); i++) {
			poolSerial.write(commandPtr[i]);
		}
		digitalWrite(SSerialTxControl, RS485Receive);  // Disable RS485 Transmit       
	}
}

#if DEBUG >=1
void printFrameData(int length) {
	int i = 0;
	while (i < length) {
		byte c = (byte)buffer[i];
		Serial.print((byte)c >> 4, HEX);
		Serial.print((byte)c & 0x0f, HEX);
		Serial.print(" ");
		i++;
	}
	Serial.println();
	i = 0;
	while (i < length) {
		Serial.print(" ");
		if (isprint(buffer[i]))
			Serial.print(buffer[i]);
		else
			Serial.print(" ");
		Serial.print(" ");
		i++;
	}
	Serial.println();
}
#endif

#if DEBUG >= 1
void poolReport() {
	Serial.print("Pool, ");
	Serial.print("Filter ");
	Serial.print(poolStatusFlag & filterBit ? "On, " : "Off, ");
	Serial.print(" Speed, ");
	Serial.print("Spa Light ");
	Serial.print(poolStatusFlag & spaLightBit ? "On, " : "Off, ");
	Serial.print("Light ");
	Serial.print(poolStatusFlag & lightBit ? "On, " : "Off, ");
	Serial.print("Solar ");
	Serial.print(poolStatusFlag & solarBit ? "On, " : "Off, ");
	Serial.print("Pool Temp ");
	Serial.print(poolTemp);
	Serial.print(", ");
	Serial.print("Air Temp ");
	Serial.print(airTemp);
	Serial.println(", ");
	Serial.print("Pool Chlorinator ");
	Serial.print(poolChlor);
	Serial.print(", ");
	Serial.print("Salt Level ");
	Serial.print(saltLevel);
	Serial.print(", ");
	Serial.print("Filter Speed ");
	Serial.print(filterSpeed);
	//TO-DO: add checks for additional controls defined
	Serial.println();
}
#endif

void poolFrameReport() {
	Serial.write('H');
	Serial.write(poolStatusFlag);
	Serial.write(lowByte(poolTemp));
	Serial.write(highByte(poolTemp));
	Serial.write(lowByte(airTemp));
	Serial.write(highByte(airTemp));
	Serial.write(lowByte(poolChlor));
	Serial.write(highByte(poolChlor));
	Serial.write(lowByte(saltLevel));
	Serial.write(highByte(saltLevel));
	Serial.write(lowByte(filterSpeed));
	Serial.write(highByte(filterSpeed));
	//TO-DO: add checks for additional controls defined
	Serial.flush();
}


//NONE
//*********( THE END )***********


void handleConsole(void)
{
	byte byteReceived;
	if (Serial.available())
	{
		//if (!sendPending) 
		newStatusFlag = poolStatusFlag;
		byteReceived = (byte)Serial.read();

		switch (byteReceived) {
#if DEBUG >= 1
		case 'R':  // print report
			poolReport();
			commandPtr = NULL;
			break;
#endif
		case 'r':  // print report
			poolFrameReport();
			commandPtr = NULL;
			break;
		case 'l':  // toggle pool Lights on/off
				   // if (!sendPending) 
		{
			//newStatusFlag = poolStatusFlag;
			commandPtr = lightData;
			newStatusFlag ^= lightBit;
			//sendPending = true; 
		}
		break;
		case 'j':  // toggle Jacuzzi light on/off
				   //if (!sendPending) 
		{
			//newStatusFlag = poolStatusFlag;
			commandPtr = spaLightData;
			newStatusFlag ^= spaLightBit;
			//   sendPending = true;       
		}
		break;
		case 'f':  // toggle Filter on/off
				   // if (!sendPending) 
		{
			//newStatusFlag = poolStatusFlag;
			commandPtr = filterData;
			newStatusFlag ^= filterBit;
			//sendPending = true;
		}
		break;
		case 's':  // toggle Solar on/off
				   // if (!sendPending) 
		{
			//newStatusFlag = poolStatusFlag;
			commandPtr = solarData;
			newStatusFlag ^= solarBit;
			//sendPending = true;   
		}
		break;
		case 'p':  // toggle Pool/Spa
				   // if (!sendPending) 
		{
			//newStatusFlag = poolStatusFlag;
			commandPtr = poolSpaData;
			newStatusFlag ^= poolSpaBit;
			//sendPending = true;   
		}
		break;
		//TO-DO: add commands for additional controls defined
		default:  //capture it all
			commandPtr = 0;
			Serial.println("Command not recognized");
		}
		//    Serial.flush();
		Serial.flush();
		if (commandPtr != 0)
			sendPending = true;
	}
}
