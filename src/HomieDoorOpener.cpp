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
allowedUIDSCount(0),
masterKey(0xFFFF)
{
	advertise("reader").setDatatype("int32").setName("Read ID");
	advertise("allow").setDatatype("int32").settable();
	advertise("deny").setDatatype("int32").settable();
	advertise("doorstate").setName("Zustand Tür").setDatatype("enum").setFormat("CLOSED, OPEN_CARD, OPEN_REMOTE, OPEN_LONG");
	advertise("override_open").setName("Tür manuell dauerhaft öffnen").setDatatype("bool").settable();
	advertise("opendoor").setName("Türöffner betätigen").setDatatype("bool").settable();
}

void HomieDoorOpener::setup() {
	// Initialize Card Reader
	SPI.begin();				// Init SPI with default values
	cardreader.PCD_Init();		// Init MFRC522
	cardreader.PCD_DumpVersionToSerial();	// Show details of PCD - MFRC522 Card Reader details


	// uncomment to activate additional debug-output on Serial for LED/buzzer states
	//buzzer.trace(Serial).begin(pinDoorState);
    //ledOK.trace(Serial).begin(pinLEDOK, true);
    //ledFail.trace(Serial).begin(pinLEDFAIL, false);

	// Switch door-opener ("buzzer") off and connect its event to the "OK" led, so it represents the  buzzer's state.
	buzzer.begin(Atm_bit::OFF).onChange(true, ledOK, Atm_led::EVT_ON).onChange(false, ledOK, Atm_led::EVT_OFF).off();
	//initialize buzzer timmer
	timer_buz.begin(ATM_TIMER_OFF);//.trace(Serial);
	timer_prog.trace(Serial).begin(ATM_TIMER_OFF);

	// LEDs are active-low
	ledOK.begin(pinLEDOK, true);
	ledFail.begin(pinLEDFAIL, true);

	// Start blinking on FAIL-led, because we are not connected yet.
	ledFail.blink(500).start();


	timer_prog.begin(ProgTimer).onFinish(ledFail, Atm_led::EVT_OFF);


	// run Homie-loop also, if not connected to MQTT
	setRunLoopDisconnected(true);

	/// read allowed UIDs from Config-File
	readJSONAllowedUsers();
}

void HomieDoorOpener::onReadyToOperate() {
	ledOK.blink(2000,0,1).start(); // one long green Pulse after connection to MQTT has been established
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

			LN.logf("HomieDoorOpener", LoggerNode::DEBUG, "Read new card with uid %x (%d).", uid, uid);

			// Set card to halt so it is not detected again
			MFRC522::StatusCode rc = cardreader.PICC_HaltA();
			if (rc != MFRC522::STATUS_OK) {
				LN.logf("HomieDoorOpener", LoggerNode::WARNING, "Warning: Cannot set card to halt state [Error %x]", rc);
			}

			setProperty("reader").send(String(uid));  // send read UID to MQTT


			if (uid == masterKey) {
				LN.logf("HomieDoorOpener", LoggerNode::INFO, "Master key read - togglerogramming mode");
				ledFail.blink(1000).start();
				timer_prog.toggle();

			} else {

				bool progModeActive = (timer_prog.state() != Atm_timer::IDLE);

				bool access = false;
				Serial.println(access ? "Access true" : "Access false");
				Serial.println(uid);
				for (uint_fast8_t i = 0; i < MaxUsers; i++) {
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
					if (progModeActive) {
						LN.logf("HomieDoorOpener", LoggerNode::INFO, "Adding UID %d to list of allowed UIDs", uid);
						if (!addUser(uid)) {
							LN.log("HomieDoorOpener", LoggerNode::ERROR, "Cannot add UID to list of allowed users. Table full?");
						}
						ledOK.blink(500,0,1);
						timer_prog.start();	// extend timer
					} else {
						Serial.println("Access denied");
						ledFail.blink(100, 200, 3).start();
						buzzer.off();
					}
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
	} 	else if (property.equals("override_open")) {
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

	/* DEBUG-Code */
	String buffer;
	if(SPIFFS.exists("/data/test.json")) {
		File testfile = SPIFFS.open("/data/test.json", "r");
		while (testfile.available()) {
			buffer = testfile.readStringUntil('\n');
			Serial.println(buffer); //Printing for debugging purpose
		}
	}
	/* end DEBUG-Code */

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
	masterKey = jsonDoc["masterkey"];
	JsonArray allowedUsers = jsonDoc["allowed_users"];
	if (!copyArray(allowedUsers, allowedUIDS)) {
		LN.logf("JSONReader", LoggerNode::ERROR, "Cannot copy user database");
		return false;
	}
	Serial.print("Masterkey: ");
	Serial.println(masterKey);
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
	if (allowedUIDSCount >= sizeof(allowedUIDS)/sizeof(uint32_t)) {
		LN.log("addUser", LoggerNode::ERROR, "No more space for new users!");
		return false;
	}
	allowedUIDS[allowedUIDSCount++] = uid;
	return writeJSONFile();
}

bool HomieDoorOpener::removeUser(uint32_t uid) {
	Serial.printf("Allowed users [%d]:\n", allowedUIDSCount);
	for (uint_fast8_t i = 0; i< allowedUIDSCount ; i++) {
		Serial.print('\t');
		Serial.println(allowedUIDS[i]);
	}
	for (uint_fast8_t i = 0; i < allowedUIDSCount ; i++ ) {
		//TODO: Implement

	}


	Serial.printf("Allowed users [%d]:\n", allowedUIDSCount);
	for (uint_fast8_t i = 0; i < MaxUsers; i++) {
		Serial.print('\t');
		Serial.println(allowedUIDS[i]);
	}

	return true;

}

bool HomieDoorOpener::writeJSONFile() {
	DynamicJsonDocument  jsonDoc(JSON_ARRAY_SIZE(MaxUsers) + JSON_OBJECT_SIZE(1) + 50 );

	JsonArray js_user = jsonDoc.createNestedArray("allowed_users");
	for (uint_fast8_t i = 0; i < MaxUsers ; i++) {
		if (!allowedUIDS[i]) break;
		js_user.add(allowedUIDS[i]);
	}
	jsonDoc["masterkey"] = masterKey;
	serializeJsonPretty(jsonDoc, Serial);
	Serial.print('\n');

	/* DEBUG-Code */
	File testfile = SPIFFS.open("/data/test.json", "w");
	size_t rc_test = testfile.print("Test!\nTest 2!");
	Serial.printf("Wrote %d byte to JSON-Configfile.\n", rc_test);
	testfile.close();
	/* end DEBUG-Code */


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


