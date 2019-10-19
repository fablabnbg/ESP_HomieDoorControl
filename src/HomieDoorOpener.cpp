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


HomieDoorOpener::HomieDoorOpener(uint8_t _pinLEDOK, uint8_t _pinLEDFAIL, uint8_t _pinBuzzer):
HomieNode("door", "Dooropener", "dooropener"),
pinLEDOK(_pinLEDOK), pinLEDFAIL(_pinLEDFAIL), pinBuzzer(_pinBuzzer), //pinDoorState(_pinDoorState),
cardreader(2, 0),
allowedUIDSCount(0)
{
	advertise("reader").setDatatype("int32").setName("Read ID");
	advertise("allow").setDatatype("int32").settable();
	advertise("deny").setDatatype("int32").settable();
	//advertise("doorstate").setName("Zustand Tür").setDatatype("enum").setFormat("CLOSED, OPEN_CARD, OPEN_REMOTE, OPEN_LONG");
	advertise("override_open").setName("Tür dauerhaft öffnen").setDatatype("bool").settable();
	advertise("opendoor").setName("Türöffner kurz betätigen").setDatatype("bool").settable();
}

void HomieDoorOpener::setup() {
	// Initialize Card Reader
	SPI.begin();				// Init SPI with default values
	cardreader.PCD_Init();		// Init MFRC522
	cardreader.PCD_DumpVersionToSerial();	// Show details of PCD - MFRC522 Card Reader details


	// uncomment to activate additional debug-output on Serial for LED/buzzer states
	buzzer.trace(Serial);
    ledOK.trace(Serial);
    //ledFail.trace(Serial).begin(pinLEDFAIL, false);

	// Switch door-opener ("buzzer") off and connect its event to the "OK" led, so it represents the  buzzer's state.
	buzzer.begin(pinBuzzer, false); // .onChange(true, ledOK, Atm_led::EVT_ON).onChange(false, ledOK, Atm_led::EVT_OFF).off();
	//initialize buzzer timmer
	timer_buz.begin(ATM_TIMER_OFF);//.trace(Serial);
	timer_prog.trace(Serial).begin(ATM_TIMER_OFF);

	// LEDs are active-low
	ledOK.begin(pinLEDOK, true);
	ledFail.begin(pinLEDFAIL, true);

	buzzer.off();
	ledOK.off();

	// Start blinking on FAIL-led, because we are not connected yet.
	ledFail.blink(500).start();


	timer_prog.begin(ProgTimer).onFinish(ledFail, Atm_led::EVT_OFF);


	// run Homie-loop also, if not connected to MQTT
	setRunLoopDisconnected(true);

	/// read allowed UIDs from Config-File
	readJSONAllowedUsers();
}

void HomieDoorOpener::onReadyToOperate() {
	ledFail.off(); // FAIL-led can be switched off now
}


// main loop
void HomieDoorOpener::loop() {
	automaton.run(); // update automaton objects
	if (cardreader.PICC_IsNewCardPresent()) {
		Serial.println("New Card!");
		if (cardreader.PICC_ReadCardSerial()) { // Select one of the cards and reads it (lower 4 byte) ID
			uint32_t uid = static_cast<uint32_t>(cardreader.uid.uidByte[0] << 24
					| cardreader.uid.uidByte[1] << 16 | cardreader.uid.uidByte[2] << 8
					| cardreader.uid.uidByte[3]);

			LN.logf("HomieDoorOpener", LoggerNode::DEBUG, "Read new card with uid %x (%u).", uid, uid);

			// Set card to halt so it is not detected again
			MFRC522::StatusCode rc = cardreader.PICC_HaltA();
			if (rc != MFRC522::STATUS_OK) {
				LN.logf("HomieDoorOpener", LoggerNode::WARNING, "Warning: Cannot set card to halt state [Error %x]", rc);
			}

			setProperty("reader").send(String(uid));  // send read UID to MQTT


			bool progModeSet = false;
			for (uint_fast8_t i = 0; i < 4; i++) {
				if (uid == masterKey[i]) {
					LN.log("HomieDoorOpener", LoggerNode::INFO, "Master key read - toggle programming mode");
					timer_prog.toggle();
					 if (timer_prog.state() != Atm_timer::IDLE) {
						 ledFail.blink(950, 50).start();
					 } else {
						 ledFail.off();
					 }
					progModeSet = true;
					break;
				}
				if (masterKey[i] == 0) break;
			}

			if (!progModeSet) {
				bool progModeActive = (timer_prog.state() != Atm_timer::IDLE);
				bool access = false;
				for (uint_fast8_t i = 0; i < MaxUsers; i++) {
					if (allowedUIDS[i] == 0)
						break; // 0 is invalid - and array is pre-initialized with 0, so 0 means that last valid UID has already been read
					if (uid == allowedUIDS[i]) {
						access = true;
						break;
					}
				}
				if (progModeActive) {
					if (access) {
						LN.logf("HomieDoorOpener", LoggerNode::INFO, "Removing UID %d to list of allowed UIDs", uid);
						if (removeUser(uid)) {
							ledOK.blink(100, 100, 3).start();
							setProperty("deny").send(String(uid));
						} else {
							LN.log("HomieDoorOpener", LoggerNode::ERROR, "Cannot remove UID to list of allowed users.");
						}
					} else {
						LN.logf("HomieDoorOpener", LoggerNode::INFO, "Adding UID %d to list of allowed UIDs", uid);
						if (addUser(uid)) {
							ledOK.blink(400, 200, 2).start();
							setProperty("allow").send(String(uid));
						} else {
							LN.log("HomieDoorOpener", LoggerNode::ERROR, "Cannot add UID to list of allowed users. Table full?");
						}
					}
					timer_prog.start();	// re-trigger timer
				} else if (access) {
					LN.log("HomieDoorOpener", LoggerNode::DEBUG, "ID ok - toggling buzzer");
					buzzer.toggle();
					setProperty("opendoor").send(buzzer.state() == Atm_led::ON ? "true" : "false");
					if (buzzer.state() == Atm_led::ON) ledOK.on(); else ledOK.off();
				} else {
					Serial.println("Access denied");
					ledFail.blink(100, 200, 3).start();
				}
			}
		} else {
			LN.log("HomieDoorOpener", LoggerNode::DEBUG, "Cannot read card"); // Level DEBUG, because it is quite normal to have incomplete reads. So this is not an error situation.
		}
	}
}

bool HomieDoorOpener::handleInput(const HomieRange &range, const String &property, const String &value) {
	uint32_t uid = value.toInt();
	if (property.equals("allow")) {
		if ((uid > 0) && addUser(uid)) setProperty("allow").send(String(uid));
		return true;
	} else if (property.equals("deny")) {
		if ((uid > 0) && removeUser(uid)) setProperty("deny").send(String(uid));
		return true;
	} 	else if (property.equals("override_open")) {
		bool open = value.equalsIgnoreCase("true");
		if (open) {
			buzzer.on();
			ledOK.on();
		} else {
			buzzer.off();
			ledOK.off();
		}
		return true;
	} else if (property.equals("opendoor")) {
		// If buzzer is switched off and requested top open, open it and start timer to close it.
		// (if it is already open, nothing is done, so it stays open and no timer is started)
		LN.logf("handleInput()", LoggerNode::DEBUG, "opendoor cmd received. Current state %x", buzzer.state());
		if (buzzer.state() != Atm_led::ON && value.equalsIgnoreCase("true")) {
			buzzer.blink(5000, 100, 1).onFinish(ledOK, Atm_led::EVT_OFF).start();
			ledOK.on();
			//timer_buz.begin(5000).start().onFinish(buzzer, Atm_bit::EVT_OFF);
		}
		setProperty("opendoor").send(buzzer.state() == Atm_led::ON ? "true" : "false");
		return true;
	}
	return false;
}

bool HomieDoorOpener::readJSONAllowedUsers() {
	if (!SPIFFS.begin()) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot init SPIFFS");
		return false;
	}

	String buffer;

	if(!SPIFFS.exists("/data/access.json")) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot find user database");
		return false;
	}
	File file = SPIFFS.open("/data/access.json", "r");
	if (!file) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot read user database");
		return false;
	}


	while (file.available()) {
		buffer = file.readStringUntil('\n');
		Serial.println(buffer); //Printing for debugging purpose
	}
	file.seek(0);

	DynamicJsonDocument  jsonDoc(JSON_ARRAY_SIZE(100) + JSON_OBJECT_SIZE(1) + 50 );
    DeserializationError error = deserializeJson(jsonDoc, file);
    file.close();

	if (error) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot parse user database [error: %x]", error);
		return false;
	}
	JsonArray mkeyArray = jsonDoc["masterkey"];
	JsonArray allowedUsers = jsonDoc["allowed_users"];
	size_t mkey = copyArray(mkeyArray, masterKey, 4);
	if (mkey == 0) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot copy master keys");
	}
	size_t uidc = copyArray(allowedUsers, allowedUIDS, MaxUsers);
	if (uidc == 0) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot copy user database");
	}
	Serial.println("Masterkey: ");
	for (uint_fast8_t i = 0; i < mkey ; i++) {
		Serial.print('\t');
		Serial.println(masterKey[i]);
	}
	Serial.println("Allowed users:");
	for (uint_fast8_t i = 0; i < MaxUsers; i++) {
		Serial.print('\t');
		Serial.println(allowedUIDS[i]);
		if (!allowedUIDS[i]) {
			allowedUIDSCount = i;
			break;
		}
	}

	return true;

}

bool HomieDoorOpener::addUser(uint32_t uid) {
	if (allowedUIDSCount >= MaxUsers) {
		LN.log("addUser", LoggerNode::ERROR, "No more space for new users!");
		return false;
	}
	allowedUIDS[allowedUIDSCount++] = uid;
	return writeJSONFile();
}

bool HomieDoorOpener::removeUser(uint32_t uid) {
	bool found = false;
	Serial.printf("Allowed users [%d]:\n", allowedUIDSCount);
	for (uint_fast8_t i = 0; i < MaxUsers; i++ ) {
		if (allowedUIDS[i] == 0) break;
		if (uid == allowedUIDS[i]) {
			LN.logf("removeUser", LoggerNode::DEBUG, "Found uid %d to remove as index %d", uid, i);
			allowedUIDS[i] = 0; // just to make sure
			found = true;
			for (uint_fast8_t j = i; j <= allowedUIDSCount; j++) {
				allowedUIDS[j] = allowedUIDS[j+1];
			}
			allowedUIDS[allowedUIDSCount] = 0;
			allowedUIDSCount--;
			break;
		}
	}
	Serial.printf("Allowed users [%d]:\n", allowedUIDSCount);
	if (found) {
		return writeJSONFile();
	} else {
		return false;
	}
}

bool HomieDoorOpener::writeJSONFile() {
	DynamicJsonDocument  jsonDoc(JSON_ARRAY_SIZE(MaxUsers) + JSON_OBJECT_SIZE(1) + 50 );

	JsonArray js_user = jsonDoc.createNestedArray("allowed_users");
	for (uint_fast8_t i = 0; i < MaxUsers ; i++) {
		if (!allowedUIDS[i]) break;
		js_user.add(allowedUIDS[i]);
	}
	JsonArray js_masters = jsonDoc.createNestedArray("masterkey");
	copyArray(masterKey, 4, js_masters);

	serializeJsonPretty(jsonDoc, Serial);
	Serial.print('\n');
	File file = SPIFFS.open("/data/access.json", "w");
	if (!file) {
		LN.logf("addUsertoJSON", LoggerNode::ERROR, "Cannot open user database for writing");
		return false;
	}
	size_t rc = serializeJson(jsonDoc, file);
	file.close();
	Serial.printf("Wrote %d byte to JSON-Configfile.\n", rc);
	return (rc > 0);
}


