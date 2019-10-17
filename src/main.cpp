/*
 * main.cpp
 *
 *  Created on: 19.09.2019
 *      Author: ian
 */


#include <Homie.hpp>
#include <LoggerNode.h>
#include <HomieDoorOpener.h>
#include <Automaton.h>

#define FW_NAME "FLNDoorOpener"
#define FW_MAJOR "0"
#define COMMIT_COUNTER "1"
#define BUILD_NUMBER "3"

#define FW_VERSION FW_MAJOR "." COMMIT_COUNTER "." BUILD_NUMBER

LoggerNode LN;

HomieDoorOpener doorOpener(4, 16);

void setup() {
	Serial.begin(74880);
	Serial.println(FW_NAME " " FW_VERSION);
	Serial.flush();
	Homie_setFirmware(FW_NAME, FW_VERSION);
	Homie.disableLedFeedback();
	Homie.disableResetTrigger();
    Homie.setLoggingPrinter(&Serial);
	Homie.setup();
}

void loop() {
	Homie.loop();
	delay(1);
	automaton.run();
}
