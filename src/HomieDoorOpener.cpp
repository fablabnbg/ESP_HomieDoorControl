/*
 * HomieDoorOpener.cpp
 *
 *  Created on: 18.09.2019
 *      Author: ian
 */

#include <HomieDoorOpener.h>
#include <FS.h>
#include <Wire.h>
#include <SPI.h>
#include <LoggerNode.h>

#include <ArduinoJson.h>


HomieDoorOpener::HomieDoorOpener(uint8_t _pinBuzzer, uint8_t _pinLEDOK, uint8_t _pinLEDFAIL, uint8_t _pinDoorState):
HomieNode("door", "Dooropener", "dooropener"),
pinBuzzer(_pinBuzzer), pinLEDOK(_pinLEDOK), pinLEDFAIL(_pinLEDFAIL), //pinDoorState(_pinDoorState),
cardreader(D4, D3),
masterKey(0xFFFF)
{
	advertise("reader").setDatatype("int32").setName("Read ID");
	advertise("allow").setDatatype("int32").settable();
	advertise("deny").setDatatype("int32").settable();
	advertise("doorstate");
	advertise("override_open").setDatatype("bool").settable();
}

void HomieDoorOpener::setup() {
	SPI.begin();				// Init SPI with default values
	cardreader.PCD_Init();		// Init MFRC522
	cardreader.PCD_DumpVersionToSerial();	// Show details of PCD - MFRC522 Card Reader details


	//buzzer.trace(Serial).begin(pinDoorState);
//	ledOK.trace(Serial).begin(pinLEDOK, true);
//	ledFail.trace(Serial).begin(pinLEDFAIL, false);
	buzzer.begin(Atm_bit::OFF).onChange(true, ledOK, Atm_led::EVT_ON).onChange(false, ledOK, Atm_led::EVT_OFF).off();
	ledOK.begin(pinLEDOK, true);
	ledFail.begin(pinLEDFAIL, true);
	ledFail.blink(500).start();

	timer_buz.begin(ATM_TIMER_OFF);//.trace(Serial);
	timer_prog.trace(Serial).begin(ATM_TIMER_OFF);

	setRunLoopDisconnected(true);
	bool rc=readJSONAllowedUsers();
}

void HomieDoorOpener::onReadyToOperate() {
	ledOK.blink(2000,0,1).start();
	ledFail.off();
}

void HomieDoorOpener::loop() {
	automaton.run();
	if (cardreader.PICC_IsNewCardPresent()) {
		Serial.println("New Card!");
		// Select one of the cards
		if (cardreader.PICC_ReadCardSerial()) {
			uint32_t uid = static_cast<uint32_t>(cardreader.uid.uidByte[0] << 24
					| cardreader.uid.uidByte[1] << 16 | cardreader.uid.uidByte[2] << 8
					| cardreader.uid.uidByte[3]);
			LN.logf("HomieDoorOpener", LoggerNode::DEBUG, "Read new card with uid %x (%d).", uid, uid);

			// Set card to halt so it is not detected again
			MFRC522::StatusCode rc = cardreader.PICC_HaltA();
			if (rc != MFRC522::STATUS_OK) {
				Serial.printf("Warning: Cannot set card to halt state [Error %x]", rc);
			}
			setProperty("reader").send(String(uid));

			if (uid == masterKey) {
				LN.logf("HomieDoorOpener", LoggerNode::INFO, "Master key read - enable programming mode");
				if (timer_prog.state() != Atm_timer::IDLE) {
					// already active
					timer_prog.stop();
					ledFail.off();
				} else {
					ledFail.blink(1000).start();
					timer_prog.begin(10000,1).onFinish(ledFail, Atm_led::EVT_OFF).start();
				}
			} else {
				bool access = false;
				Serial.println(access ? "Access true" : "Access false");
				Serial.println(uid);
				for (uint_fast8_t i = 0; i < sizeof(allowedUIDS); i++) {
					if (allowedUIDS[i] == 0)
						break; // 0 is invalid - and array is pre-initialized with 0, so 0 means that last valid UID has already been read
					Serial.println(access ? "Access true" : "Access false");
					if (uid == allowedUIDS[i]) {
						access = true;
						break;
					}
				}
				Serial.println(access ? "Access true" : "Access false");
				Serial.println(uid);
				if (access) {
					Serial.println("Access granted");
					buzzer.on();
					timer_buz.begin(5000, 1).onFinish(buzzer, Atm_bit::EVT_OFF).start();
				} else {
					Serial.println("Access denied");
					ledFail.blink(100, 200, 3).start();
					buzzer.off();
				}
			}
		} else {
			LN.logf("HomieDoorOpener", LoggerNode::DEBUG, "Cannot read card"); // Level DEBUG, because it is quite normal to have incomplete reads. So this is not an error situation.
		}
	}
}

bool HomieDoorOpener::handleInput(const HomieRange &range, const String &property, const String &value) {
	if (property.equals("allow")) {
		//add to database
	} else if (property.equals("deny")) {
		// remove from database)
	} 	else if (property.equals("overrid_open")) {
		// Open Door
		bool open = value.equalsIgnoreCase("true");
		if (open) buzzer.on(); else buzzer.off();
	}
	return false;
}

bool HomieDoorOpener::readJSONAllowedUsers() {
	if (!SPIFFS.begin()) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot init SPIFFS");
		return false;
	}

	if(!SPIFFS.exists("/data/access.json")) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot find user database");
		return false;
	}
	DynamicJsonBuffer jsonBuffer(JSON_ARRAY_SIZE(10) + JSON_OBJECT_SIZE(1) + 50 );
	File file = SPIFFS.open("/data/access.json", "r");
	if (!file) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot read user database");
		return false;
	}

	JsonObject &root = jsonBuffer.parseObject(file);
	file.close();

	if (!root.success()) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot parse user database");
		return false;
	}

	masterKey = root["masterkey"];
	JsonArray& allowedUsers = root["allowed_users"];
	if (!allowedUsers.copyTo(allowedUIDS, sizeof(allowedUIDS))) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot copy user database");
		return false;
	}
	Serial.print("Masterkey: ");
	Serial.println(masterKey);
	Serial.println("Allowed users:");
	for (uint_fast8_t i = 0; i<sizeof(allowedUIDS); i++) {
		Serial.print('\t');
		Serial.println(allowedUIDS[i]);
		if (!allowedUIDS[i]) break;
	}

	return true;

}
