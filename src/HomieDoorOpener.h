/*
 * HomieDoorOpener.h
 *
 *  Created on: 18.09.2019
 *      Author: ian
 */

#ifndef SRC_HOMIEDOOROPENER_H_
#define SRC_HOMIEDOOROPENER_H_
#include <HomieNode.hpp>
#include <Automaton.h>

#include <MFRC522.h>


class HomieDoorOpener: public HomieNode {
public:
	HomieDoorOpener(uint8_t pinBuzzer, uint8_t pinLEDOK, uint8_t pinLEDFAIL, uint8_t pinDoorState);

protected:
	virtual bool handleInput(const HomieRange &range, const String &property, const String &value) override;

    virtual void onReadyToOperate() override;

	virtual void setup() override;
	virtual void loop() override;

private:
	bool readJSONAllowedUsers();
	uint8_t pinBuzzer;
	uint8_t pinLEDOK;
	uint8_t pinLEDFAIL;
	//uint8_t pinDoorState;

	Atm_bit buzzer;
	Atm_led ledOK;
	Atm_led ledFail;
	//Atm_button doorOpen;
    MFRC522 cardreader;

    Atm_timer timer_buz;

    uint32_t allowedUIDS[255];
};

#endif /* SRC_HOMIEDOOROPENER_H_ */
