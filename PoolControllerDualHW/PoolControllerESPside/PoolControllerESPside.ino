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
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

#define DEBUG 0  //0: nothing; 1: status; 10: wip 20: protocol;  99 all
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



//User-written Functions
//void showMem();


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

BlynkTimer timer;
int handleBlynkRunTimer;
int handleBlynkWriteHighFreqTimer;
int handleBlynkWriteLowFreqTimer;
int getPoolStatusUpdateTimer;

void setup()   /****** SETUP: RUNS ONCE ******/
{
	// Start the built-in serial port, probably to Serial Monitor
	Serial.begin(115200);

#if DEBUG >=1
	Serial.println("Initializing...");
#endif

	handleBlynkRunTimer = timer.setInterval(4000L, handleBlynkRun);
	handleBlynkWriteHighFreqTimer = timer.setInterval(5000L, handleBlynkWriteHighFreq);
	handleBlynkWriteLowFreqTimer = timer.setInterval(23456L, handleBlynkWriteLowFreq);
	getPoolStatusUpdateTimer = timer.setInterval(5000L, GetPoolStatusUpdate);

	Blynk.begin(auth, ssid, pass);


#if DEBUG >=1
	showMem();
	Serial.println("done");
#endif

}//--(end setup )---


void handleBlynkRun() {
	Blynk.run();
}

void handleBlynkWriteHighFreq() {

	Blynk.virtualWrite(POOL_LIGHT_VIRTUAL_PIN, poolStatusFlag & lightBit ? 255 : 0);
	Blynk.virtualWrite(SPA_LIGHT_VIRTUAL_PIN, poolStatusFlag & spaLightBit ? 255 : 0);
	Blynk.virtualWrite(SPAnPOOL_STATUS_VIRTUAL_PIN, poolStatusFlag & poolSpaBit ? 255 : 0);
	Blynk.virtualWrite(nSPAPOOL_STATUS_VIRTUAL_PIN, poolStatusFlag & poolSpaBit ? 0 : 255);
	Blynk.virtualWrite(FILTER_STATUS_VIRTUAL_PIN, poolStatusFlag & filterBit ? 255 : 0);
	Blynk.virtualWrite(FILTER_SPEED_VIRTUAL_PIN, filterSpeed);
	Blynk.virtualWrite(SOLAR_STATUS_VIRTUAL_PIN, poolStatusFlag & solarBit ? 255 : 0);
	Blynk.virtualWrite(POOL_TEMP_VIRTUAL_PIN, poolTemp);
	Blynk.virtualWrite(AIR_TEMP_VIRTUAL_PIN, airTemp);
}

void handleBlynkWriteLowFreq() {
	Blynk.virtualWrite(POOL_CHLOR_VIRTUAL_PIN, poolChlor);
	Blynk.virtualWrite(SALT_LEVEL_VIRTUAL_PIN, saltLevel);
	Blynk.virtualWrite(POOL_LIGHT_TOGGLE_VIRTUAL_PIN, poolStatusFlag & lightBit ? 1 : 0);
	Blynk.virtualWrite(SPA_LIGHT_TOGGLE_VIRTUAL_PIN, poolStatusFlag & spaLightBit ? 1 : 0);
	Blynk.virtualWrite(FILTER_TOGGLE_VIRTUAL_PIN, poolStatusFlag & filterBit ? 1 : 0);
	Blynk.virtualWrite(SOLAR_TOGGLE_VIRTUAL_PIN, poolStatusFlag & solarBit ? 1 : 0);
	Blynk.virtualWrite(SPAPOOL_TOGGLE_VIRTUAL_PIN, poolStatusFlag & poolSpaBit ? "pool" : "spa");

}
void loop()   /****** LOOP: RUNS CONSTANTLY ******/
{

	/*
	* Need to develop controls to:
	* 1) periodically get updated poolStatus frames from Arduino
	* 2) depending on Blynk command, write to Serial/Console
	*/
	timer.run();
	Alarm.delay(0);  //just to make the alarms work

}//--(end main loop )---

 /*-----( Declare User-written Functions )-----*/


void GetPoolStatusUpdate() {

	byte byteReceived;
	byte buffer[20];
	byte *bufPtr;

	// Needs to match sequence on the ArduinoSide ( poolFrameReport() )
	Serial.println('r');
	Serial.flush();
	while (Serial.available() < 12) {
		//if timeout is reached then exit
	}

	if (Serial.read() == 'H') {
		poolStatusFlag = Serial.read();
		poolTemp = Serial.read();
		poolTemp = poolTemp + Serial.read() << 8;
		airTemp = Serial.read();
		airTemp = airTemp + Serial.read() << 8;
		poolChlor = Serial.read();
		poolChlor = poolChlor + Serial.read() << 8;
		saltLevel = Serial.read();
		saltLevel = saltLevel + Serial.read() << 8;
		filterSpeed = Serial.read();
		filterSpeed = filterSpeed + Serial.read() << 8;
	}
	//clear any remaining data
	while (Serial.available()>0) Serial.read();
	//if something changed from current status should it initiate BlynkWrite???
}

void sendCommandToPool(byte cmd) {
	Serial.println((char)cmd);
}


//NONE
//*********( THE END )***********




BLYNK_WRITE(POOL_LIGHT_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	sendCommandToPool('l');
}

BLYNK_WRITE(SPA_LIGHT_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	sendCommandToPool('j');

}

BLYNK_WRITE(FILTER_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	sendCommandToPool('f');

}

BLYNK_WRITE(SOLAR_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	sendCommandToPool('s');

}

BLYNK_WRITE(SPAPOOL_TOGGLE_VIRTUAL_PIN)
{
	int pinValue = param.asInt();
	sendCommandToPool('p');

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


