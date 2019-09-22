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
#define COMMIT_COUNTER "0"
#define BUILD_NUMBER "1"

#define FW_VERSION FW_MAJOR "." COMMIT_COUNTER "." BUILD_NUMBER

/* Magic sequence for Autodetectable Binary Upload */
const char *__FLAGGED_FW_NAME = "\xbf\x84\xe4\x13\x54" FW_NAME "\x93\x44\x6b\xa7\x75";
const char *__FLAGGED_FW_VERSION = "\x6a\x3f\x3e\x0e\xe1" FW_VERSION "\xb0\x30\x48\xd4\x1a";
/* End of magic sequence for Autodetectable Binary Upload */



LoggerNode LN;

HomieDoorOpener doorOpener(D1, D2, D0, D0);

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
