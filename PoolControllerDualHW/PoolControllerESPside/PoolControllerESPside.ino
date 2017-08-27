/*
 Name:		PoolControllerESPside.ino
 Created:	8/22/2017 8:27:22 PM
 Author:	vfilauro
*/

//#define BLYNK_PRINT Serial

//#include <SoftwareSerial.h>
#include <MemoryFree.h>
#include <Time.h>
#include <TimeAlarms.h>

#include <SPI.h>
//#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

#define DEBUG 0  //0: nothing; 1: status; 10: wip 20: protocol;  99 all
#define DebugSerial Serial
#define DEBUG_SERIAL_BAUD_RATE 115200

#define CommSerial Serial
#define COMM_SERIAL_BAUD_RATE 115200


char auth[] = "f9a5ffd833664abba1a90d5b4b349134";

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "Goldrake";
char pass[] = "vaffanculoatecazzone";


#define POOL_LIGHT_VIRTUAL_PIN  V2
#define SPA_LIGHT_VIRTUAL_PIN  V3
#define FILTER_STATUS_VIRTUAL_PIN  V4
#define FILTER_SPEED_VIRTUAL_PIN  V5
#define SOLAR_STATUS_VIRTUAL_PIN  V6
#define POOL_TEMP_VIRTUAL_PIN  V7
#define AIR_TEMP_VIRTUAL_PIN  V8
#define POOL_CHLOR_VIRTUAL_PIN  V9
#define SALT_LEVEL_VIRTUAL_PIN  V10
#define POOL_LIGHT_TOGGLE_VIRTUAL_PIN  V11
#define SPA_LIGHT_TOGGLE_VIRTUAL_PIN  V12

#define FILTER_TOGGLE_VIRTUAL_PIN V13
#define SOLAR_TOGGLE_VIRTUAL_PIN  V14
#define nSPAPOOL_STATUS_VIRTUAL_PIN  V15
#define SPAnPOOL_STATUS_VIRTUAL_PIN  V16
#define SPAPOOL_TOGGLE_VIRTUAL_PIN  V17

#define BLYNK_COLOR_PENDING "#CCCCCC"
#define BLYNK_COLOR_OFF "#000000"
#define BLYNK_COLOR_ON "#00FF00"


//Arduino's internal storage bits for items to be controlled (need to fit into a Byte)
#define filterBit         0x80
#define poolSpaBit        0x40
#define solarBit          0x20
#define lightBit          0x10
#define spaLightBit       0x08
#define serviceBit        0x04
#define superChlorOnBit   0x02
#define heaterBit         0x01

byte poolStatusFlag = 0;  // most recently stored pool status
byte newStatusFlag = 0;
boolean sendPending = false;
bool okToSend = false;
byte* commandPtr = NULL;

int poolTemp = 0;
int airTemp = 0;
int poolChlor = 0;
int saltLevel = 0;
int filterSpeed = 0;

bool pendingPoolLightToggle = false;
bool pendingSpaLightToggle = false;
bool pendingFilterToggle = false;
bool pendingSolarToggle = false;
bool pendingSpaToggle = false;
int pendingCmdCount = 0;

BlynkTimer timer;
int handleBlynkRunTimer;
int handleBlynkWriteHighFreqTimer;
int handleBlynkWriteLowFreqTimer;
int getPoolStatusUpdateTimer;

void setup()   /****** SETUP: RUNS ONCE ******/
{
	// Start the built-in serial port, probably to Serial Monitor
	CommSerial.begin(COMM_SERIAL_BAUD_RATE);
#if DEBUG >=1
	DebugSerial.begin(DEBUG_SERIAL_BAUD_RATE);
#endif

#if DEBUG >=1
	DebugSerial.println("Initializing...");
#endif

	//get a first read from the pool before Blynk connects (and variables all get updated)
	GetPoolStatusUpdate();

	handleBlynkWriteLowFreqTimer = timer.setInterval(25100L, handleBlynkWriteLowFreq);
	getPoolStatusUpdateTimer = timer.setInterval(5000L, GetPoolStatusUpdate);

	Blynk.begin(auth, ssid, pass);

#if DEBUG >=1
	showMem();
	DebugSerial.println("done");
#endif

}//--(end setup )---




BLYNK_CONNECTED() {
	Blynk.virtualWrite(POOL_LIGHT_VIRTUAL_PIN, poolStatusFlag & lightBit ? 255 : 0);
	Blynk.virtualWrite(SPA_LIGHT_VIRTUAL_PIN, poolStatusFlag & spaLightBit ? 255 : 0);
	Blynk.virtualWrite(FILTER_STATUS_VIRTUAL_PIN, poolStatusFlag & filterBit ? 255 : 0);
	Blynk.virtualWrite(SOLAR_STATUS_VIRTUAL_PIN, poolStatusFlag & solarBit ? 255 : 0);
	Blynk.virtualWrite(POOL_LIGHT_TOGGLE_VIRTUAL_PIN, poolStatusFlag & lightBit ? 1 : 0);
	Blynk.virtualWrite(SPA_LIGHT_TOGGLE_VIRTUAL_PIN, poolStatusFlag & spaLightBit ? 1 : 0);
	Blynk.virtualWrite(FILTER_TOGGLE_VIRTUAL_PIN, poolStatusFlag & filterBit ? 1 : 0);
	Blynk.virtualWrite(SOLAR_TOGGLE_VIRTUAL_PIN, poolStatusFlag & solarBit ? 1 : 0);
	Blynk.virtualWrite(SPAPOOL_TOGGLE_VIRTUAL_PIN, poolStatusFlag & poolSpaBit ? "pool" : "spa");
	Blynk.virtualWrite(AIR_TEMP_VIRTUAL_PIN, airTemp);
	Blynk.virtualWrite(POOL_TEMP_VIRTUAL_PIN, poolTemp);
	Blynk.virtualWrite(FILTER_SPEED_VIRTUAL_PIN, filterSpeed);
	Blynk.virtualWrite(SALT_LEVEL_VIRTUAL_PIN, saltLevel); 
	Blynk.virtualWrite(nSPAPOOL_STATUS_VIRTUAL_PIN, poolStatusFlag & poolSpaBit ? 0 : 255);
	Blynk.virtualWrite(SPAnPOOL_STATUS_VIRTUAL_PIN, poolStatusFlag & poolSpaBit ? 255 : 0);
	Blynk.virtualWrite(POOL_CHLOR_VIRTUAL_PIN, poolChlor);	
}



void handleBlynkWriteLowFreq() {
	Blynk.virtualWrite(FILTER_SPEED_VIRTUAL_PIN, filterSpeed);
	Blynk.virtualWrite(POOL_TEMP_VIRTUAL_PIN, poolTemp);
	Blynk.virtualWrite(AIR_TEMP_VIRTUAL_PIN, airTemp); 
	Blynk.virtualWrite(POOL_CHLOR_VIRTUAL_PIN, poolChlor);
	Blynk.virtualWrite(SALT_LEVEL_VIRTUAL_PIN, saltLevel);
}
void loop()   /****** LOOP: RUNS CONSTANTLY ******/
{
	Blynk.run();
	timer.run();
	Alarm.delay(0);  //just to make the alarms work

}//--(end main loop )---

 /*-----( Declare User-written Functions )-----*/


void GetPoolStatusUpdate() {

	byte newPoolStatusFlag = 0; // newly read pool status

	// Needs to match sequence on the ArduinoSide ( poolFrameReport() )
	CommSerial.println('r');
	CommSerial.flush();

	enum frameState { WAIT, START, END, COMPLETED };
	enum frameState myFrameState = WAIT;
	unsigned long startclock = millis();
	
	while ((millis() - startclock < 5000L) && (myFrameState != COMPLETED)) {
		switch (myFrameState) {
		case WAIT:
			if (CommSerial.read() == 'H')
				myFrameState = START;
		case START:
			if (CommSerial.available() > 11) {
				//this is the data we are expecting
				newPoolStatusFlag = CommSerial.read();
				poolTemp = CommSerial.read();
				poolTemp = poolTemp + CommSerial.read() << 8;
				airTemp = CommSerial.read();
				airTemp = airTemp + CommSerial.read() << 8;
				poolChlor = CommSerial.read();
				poolChlor = poolChlor + CommSerial.read() << 8;
				saltLevel = CommSerial.read();
				saltLevel = saltLevel + CommSerial.read() << 8;
				filterSpeed = CommSerial.read();
				filterSpeed = filterSpeed + CommSerial.read() << 8;
				myFrameState = END;
			}
		case END:
			//clear any remaining data
			while (CommSerial.available() > 0)
				CommSerial.read();
			myFrameState = COMPLETED;
		case COMPLETED:
			//update Blynk App/Server if something changed from current status
			if (newPoolStatusFlag != poolStatusFlag) {
				int tmpFlag;
				if ((tmpFlag = newPoolStatusFlag&lightBit) != poolStatusFlag&lightBit) {
					Blynk.setProperty(POOL_LIGHT_TOGGLE_VIRTUAL_PIN, "color", tmpFlag ? BLYNK_COLOR_ON : BLYNK_COLOR_OFF);
					Blynk.virtualWrite(POOL_LIGHT_VIRTUAL_PIN, tmpFlag ? 255 : 0);
					if (pendingPoolLightToggle) {
						pendingPoolLightToggle = false;
						pendingCmdCount--;
					}
				}
				if ((tmpFlag = newPoolStatusFlag&spaLightBit) != poolStatusFlag&spaLightBit) {
					Blynk.setProperty(SPA_LIGHT_TOGGLE_VIRTUAL_PIN, "color", tmpFlag ? BLYNK_COLOR_ON : BLYNK_COLOR_OFF);
					Blynk.virtualWrite(SPA_LIGHT_VIRTUAL_PIN, tmpFlag ? 255 : 0);
					if (pendingSpaToggle) {
						pendingSpaToggle = false;
						pendingCmdCount--;
					}
				}
				if ((tmpFlag = newPoolStatusFlag&filterBit) != poolStatusFlag&filterBit) {
					Blynk.setProperty(FILTER_TOGGLE_VIRTUAL_PIN, "color", tmpFlag ? BLYNK_COLOR_ON : BLYNK_COLOR_OFF);
					Blynk.virtualWrite(FILTER_STATUS_VIRTUAL_PIN, tmpFlag ? 255 : 0);
					if (pendingFilterToggle) {
						pendingFilterToggle = false;
						pendingCmdCount--;
					}
				}
				if ((tmpFlag = newPoolStatusFlag&solarBit) != poolStatusFlag&solarBit) {
					Blynk.setProperty(SOLAR_TOGGLE_VIRTUAL_PIN, "color", tmpFlag ? BLYNK_COLOR_ON : BLYNK_COLOR_OFF);
					Blynk.virtualWrite(SOLAR_STATUS_VIRTUAL_PIN, tmpFlag ? 255 : 0);
					if (pendingSolarToggle) {
						pendingSolarToggle = false;
						pendingCmdCount--;
					}
				}
				if ((tmpFlag = newPoolStatusFlag&poolSpaBit) != poolStatusFlag&poolSpaBit) {
					Blynk.setProperty(SPAPOOL_TOGGLE_VIRTUAL_PIN, "color", tmpFlag ? BLYNK_COLOR_ON : BLYNK_COLOR_OFF);
					Blynk.virtualWrite(SPAnPOOL_STATUS_VIRTUAL_PIN, tmpFlag ? 255 : 0);
					if (pendingSpaToggle) {
						pendingSpaToggle = false;
						pendingCmdCount--;
					}
				}
				poolStatusFlag = newPoolStatusFlag;
#if DEBUG>=1
				if (pendingCmdCount < 0)
					DebugSerial.println("DBG: pendingCmdCount cannot be negative!");
#endif
			}
			break;
		}
	}
}



void sendCommandToPool(byte cmd) {
	switch ((char)cmd) {
	case 'l':
	case 'j':
	case 'f':
	case 's':
	case 'p':
		pendingCmdCount++;
		CommSerial.println((char)cmd);
		break;
	default:
#if DEBUG >=1
		DebugSerial.println("DBG: Command not supported!");
#endif
		;
	}
	return;
}



BLYNK_WRITE(POOL_LIGHT_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	if (pendingPoolLightToggle == false) {
		Blynk.setProperty(POOL_LIGHT_TOGGLE_VIRTUAL_PIN, "color", BLYNK_COLOR_PENDING);
		sendCommandToPool('l');
		pendingPoolLightToggle = true;
	}
}

BLYNK_WRITE(SPA_LIGHT_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	if (pendingSpaLightToggle == false) {
		Blynk.setProperty(SPA_LIGHT_TOGGLE_VIRTUAL_PIN, "color", BLYNK_COLOR_PENDING);
		sendCommandToPool('j');
		pendingSpaLightToggle = true;
	}
}

BLYNK_WRITE(FILTER_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	if (pendingFilterToggle == false) {
		Blynk.setProperty(FILTER_TOGGLE_VIRTUAL_PIN, "color", BLYNK_COLOR_PENDING);
		sendCommandToPool('f');
		pendingFilterToggle = true;
	}

}

BLYNK_WRITE(SOLAR_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	if (pendingSolarToggle == false) {
		Blynk.setProperty(SOLAR_TOGGLE_VIRTUAL_PIN, "color", BLYNK_COLOR_PENDING);
		sendCommandToPool('s');
		pendingSolarToggle = true;
	}

}

BLYNK_WRITE(SPAPOOL_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	if (pendingSpaToggle == false) {
		Blynk.setProperty(SPAPOOL_TOGGLE_VIRTUAL_PIN, "color", BLYNK_COLOR_PENDING);
		sendCommandToPool('p');
		pendingSpaToggle = true;
	}

}

BLYNK_READ(POOL_LIGHT_VIRTUAL_PIN)
{
	Blynk.virtualWrite(POOL_LIGHT_VIRTUAL_PIN, poolStatusFlag & lightBit ? 255 : 0);
}

BLYNK_READ(SPA_LIGHT_VIRTUAL_PIN)
{
	Blynk.virtualWrite(SPA_LIGHT_VIRTUAL_PIN, poolStatusFlag & spaLightBit ? 255 : 0);
}
BLYNK_READ(FILTER_STATUS_VIRTUAL_PIN)
{
	Blynk.virtualWrite(FILTER_STATUS_VIRTUAL_PIN, poolStatusFlag & filterBit ? 255 : 0);
}

BLYNK_READ(FILTER_SPEED_VIRTUAL_PIN)
{
	Blynk.virtualWrite(FILTER_SPEED_VIRTUAL_PIN, filterSpeed);
}

BLYNK_READ(SOLAR_STATUS_VIRTUAL_PIN)
{
	Blynk.virtualWrite(SOLAR_STATUS_VIRTUAL_PIN, poolStatusFlag & solarBit ? 255 : 0);
}
BLYNK_READ(POOL_TEMP_VIRTUAL_PIN)
{
	Blynk.virtualWrite(POOL_TEMP_VIRTUAL_PIN, poolTemp);
}
BLYNK_READ(AIR_TEMP_VIRTUAL_PIN)
{
	Blynk.virtualWrite(AIR_TEMP_VIRTUAL_PIN, airTemp);
}
BLYNK_READ(POOL_CHLOR_VIRTUAL_PIN)
{
	Blynk.virtualWrite(POOL_CHLOR_VIRTUAL_PIN, poolChlor);
}
BLYNK_READ(SALT_LEVEL_VIRTUAL_PIN)
{
	Blynk.virtualWrite(SALT_LEVEL_VIRTUAL_PIN, saltLevel);
}


