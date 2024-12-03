/*==============================================================================

	GameController.cpp

	joyosc: a device event to osc bridge

	Copyright (C) 2007, 2010, 2024 Dan Wilcox <danomatika@gmail.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.

==============================================================================*/
#include "GameController.h"

#include <cmath>
#include <regex>
#include "../shared.h"
#include "GameControllerRemapping.h"
#include "GameControllerIgnore.h"
#include "Path.h"

#include <cmath> // M_2_PI

bool GameController::triggersAsAxes = false;
bool GameController::enableSensors = false;
bool GameController::normalizeSensors = false;
unsigned int GameController::sensorRateMS = 0;

GameController::GameController(std::string address) : Device(address) {
	m_triggersAsAxes = GameController::triggersAsAxes;
	m_enableSensors = GameController::enableSensors;
	m_normalizeSensors = GameController::normalizeSensors;
	m_sensorRateMS = GameController::sensorRateMS;
}

bool GameController::open(DeviceIndex index, DeviceSettings *settings) {
	if(!index.isValid()) {
		LOG_ERROR << "GameController: cannot open, index not set" << std::endl;
		return false;
	}
	m_index = index;

	if(isOpen()) {
		LOG_ERROR << "GameController: controller with index "
		         << m_index.index << " already opened" << std::endl;
		return false;
	}

	m_controller = SDL_GameControllerOpen(m_index.sdlIndex);
	if(!m_controller) {
		LOG_ERROR << "GameController: open failed for index " << m_index.index
		          << ": " << SDL_GetError() << std::endl;
		return false;
	}

	SDL_Joystick *joystick = SDL_GameControllerGetJoystick(m_controller);
	m_instanceID = SDL_JoystickInstanceID(joystick);
	m_name = SDL_GameControllerName(m_controller);

	// create prev axis values
	for(int i = 0; i < SDL_JoystickNumAxes(joystick); ++i) {
		m_prevAxisValues.push_back(0);
	}

	// apply settings?
	if(settings) {

		// try to set the address from the mapping list using the dev name,
		// replace # with index if found
		if(settings->address != "") {
			std::stringstream stream;
			stream << index.index;
			m_address = std::regex_replace(settings->address, std::regex("#"), stream.str());
		}

		// set axis dead zone if one exists
		if(settings->axisDeadZone > 0) {
			setAxisDeadZone(settings->axisDeadZone);
		}
		// set remapping if one exists
		if(settings->remap) {
			setRemapping((GameControllerRemapping *)settings->remap);
			printRemapping();
		}

		// set ignore if one exists
		if(settings->ignore) {
			setIgnore((GameControllerIgnore *)settings->ignore);
			printIgnores();
		}

		// overrides
		GameControllerSettings *gcs = (GameControllerSettings *)settings->data;
		m_triggersAsAxes = gcs->triggersAsAxes;
		m_enableSensors = gcs->enableSensors;
		m_normalizeSensors = gcs->normalizeSensors;
		m_sensorRateMS = gcs->sensorRateMS;
		m_extendedMappings = (m_remapping ? m_remapping->hasExtendedMappings() : false);

		// set color?
		if(gcs->isColorValid()) {
			setColor(gcs->ledColor[0],
			         gcs->ledColor[1],
			         gcs->ledColor[2]);
		}
	}

	if(m_enableSensors) {
		enableAvailableSensors();
	}

	if(Device::printEvents) {
		LOG << "GameController: opened ";
		print();
	}
	else {
		LOG_VERBOSE << "GameController: opened " << toString() << std::endl;
	}
	return true;
}

void GameController::close() {
	if(m_controller) {
		if(isOpen()) {
			SDL_GameControllerClose(m_controller);
		}
		if(Device::printEvents) {
			LOG << "GameController: closed " << m_index.index
			    << " " << m_name << " with address "
			    << m_address << std::endl;
		}
		else {
			LOG_VERBOSE << "GameController: closed " << m_index.index
			            << " " << m_name << " with address "
			            << m_address << std::endl;
		}
		m_controller = nullptr;
	}

	// reset variables
	m_index.clear();
	m_instanceID = -1;
	m_name = "";
	m_prevAxisValues.clear();
}

bool GameController::handleEvent(SDL_Event *event) {
	if(event == nullptr) {
		return false;
	}
	switch(event->type) {

		case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
			std::string button = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)event->cbutton.button);
			return buttonPressed(button, event->cbutton.state);
		}

		case SDL_CONTROLLERAXISMOTION: {
			std::string axis = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)event->caxis.axis);

			if(m_ignore && m_ignore->isIgnored(AXIS, axis)) {
				break;
			}
			if(m_remapping) {
				axis = m_remapping->mappingFor(AXIS, axis);
			}

			// handle jitter by creating a dead zone
			int value = (int)event->caxis.value;
			if(abs(value) < m_axisDeadZone) {
				value = 0;
			}

			// trigger buttons for some devices are reported as axis values,
			// forward them as buttons unless desired as axes
			bool isButton = (!m_triggersAsAxes && (axis == "lefttrigger" || axis == "righttrigger"));
			if(isButton) {
				value = (event->caxis.value > 0 ? 1 : 0);
			}

			// make sure we don't report a value more than once
			if(m_prevAxisValues[event->caxis.axis] == value) {
				return true;
			}

			// store value
			m_prevAxisValues[event->caxis.axis] = value;

			// send
			if(isButton) {
				m_prevAxisValues[event->caxis.axis] = value;
				return buttonPressed(axis, value);
			}
			axisMoved(axis, value);

			return true;
		}

		case SDL_CONTROLLERTOUCHPADDOWN: case SDL_CONTROLLERTOUCHPADMOTION:
		case SDL_CONTROLLERTOUCHPADUP: {
			const std::string &action = nameForTouchEvent((SDL_EventType)event->type);
			sender->send(Device::deviceAddress + m_address + "/touchpad",
				"siifff", action.c_str(),
				event->ctouchpad.touchpad,
				event->ctouchpad.finger,
				event->ctouchpad.x,
				event->ctouchpad.y,
				event->ctouchpad.pressure
			);
			if(Device::printEvents) {
				LOG << m_address << " " << m_name
				    << " touchpad: " << action
				    << " " << event->ctouchpad.touchpad
				    << " " << event->ctouchpad.finger
				    << " " << event->ctouchpad.x
				    << " " << event->ctouchpad.y
				    << " " << event->ctouchpad.pressure << std::endl;
			}
			return true;
		}

		case SDL_CONTROLLERSENSORUPDATE: {
			SDL_SensorType type = (SDL_SensorType)event->csensor.sensor;
			const std::string &sensor = nameForSensor(type);
			float x = event->csensor.data[0];
			float y = event->csensor.data[1];
			float z = event->csensor.data[2];
			auto iter = m_prevSensorValues.find(type);
			if(iter != m_prevSensorValues.end()) {
				// limit sensor rate and filter value repeats
				SensorValues *prev = (&iter->second);
				if(event->csensor.timestamp - prev->timestamp < m_sensorRateMS) {
					return true;
				}
				if(abs(x - prev->x) < FLT_EPSILON &&
				   abs(y - prev->y) < FLT_EPSILON &&
				   abs(z - prev->z) < FLT_EPSILON) {
				    return true;
				}
				prev->x = x;
				prev->y = y;
				prev->z = z;
				prev->timestamp = event->csensor.timestamp;
			}
			if(m_normalizeSensors) {
				if(shared::isAccelSensor(type)) { // m/s^2 -> standard gs
					x /= SDL_STANDARD_GRAVITY;
					y /= SDL_STANDARD_GRAVITY;
					z /= SDL_STANDARD_GRAVITY;
				}
				else if(shared::isGyroSensor(type)) { // rad/2 -> rotation
					x /= M_2_PI;
					y /= M_2_PI;
					z /= M_2_PI;
				}
			}
			sender->send(Device::deviceAddress + m_address + "/sensor",
				"sfff", sensor.c_str(), x, y, z);
			if(Device::printEvents) {
				LOG << m_address << " " << m_name << " sensor: " << sensor
				    << " " << x << " " << y << " " << z << std::endl;
			}
			return true;
		}

		// extended joystick event
		case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP: {
			if(!m_remapping) {break;}
			if(SDL_GameControllerHasButton(m_controller, (SDL_GameControllerButton)event->jbutton.button) == SDL_FALSE) {
				std::string button = m_remapping->mappingForExtended(BUTTON, (int)event->jbutton.button);
				if(button != "") {
					int value = (int)event->jbutton.state;
					sender->send(Device::deviceAddress + m_address + "/button",
						"si", button.c_str(), value);
					if(Device::printEvents) {
						LOG << m_address << " " << m_name
						    << " button: " << button << " " << value << std::endl;
					}
					return true;
				}
			}
			break;
		}

		// extended joystick event
		// no ignore or dead zone handling, send raw value
		case SDL_JOYAXISMOTION: {
			if(!m_remapping) {break;}
			if(SDL_GameControllerHasAxis(m_controller, (SDL_GameControllerAxis)event->jaxis.axis) == SDL_FALSE) {
				std::string axis = m_remapping->mappingForExtended(AXIS, (int)event->jaxis.axis);
				if(axis != "") {
					int value = (int)event->caxis.value;
					if(m_prevAxisValues[event->caxis.axis] == value) {
						return true;
					}
					m_prevAxisValues[event->jaxis.axis] = value;
					axisMoved(axis, value);
					return true;
				}
			}
			break;
		}
	}
	return false;
}

bool GameController::isOpen() {
	return SDL_GameControllerGetAttached(m_controller) == SDL_TRUE;
}

void GameController::setColor(int r, int g, int b) {
	if(SDL_GameControllerHasLED(m_controller) == SDL_TRUE) {
		r = CLAMP(r, 0, 255);
		g = CLAMP(g, 0, 255);
		b = CLAMP(b, 0, 255);
		SDL_GameControllerSetLED(m_controller, r, g, b);
	}
}

void GameController::rumble(float strength, int duration) {
	if(SDL_GameControllerHasRumble(m_controller) == SDL_TRUE) {
		strength = CLAMP(strength, 0, 1);
		duration = CLAMP(duration, 0, 5000);
		SDL_GameControllerRumble(m_controller, 0xFFFF * strength, 0xFFFF * strength, duration);
	}
}

void GameController::print() {
	LOG << toString() << std::endl;
	if(m_controller) {
		shared::printControllerDetails(m_controller, false);
	}
}

SDL_Joystick* GameController::getJoystick() {
	if(m_controller) {
		return SDL_GameControllerGetJoystick(m_controller);
	}
	return nullptr;
}

void GameController::setTriggersAsAxes(bool asAxes) {
	m_triggersAsAxes = asAxes;
}

int GameController::addMappingString(std::string mapping) {
	int ret = SDL_GameControllerAddMapping(mapping.c_str());
	if(ret < 0) {
		LOG_WARN << "GameController mapping string error: " << SDL_GetError() << std::endl;
	}
	return ret;
}

int GameController::addMappingFile(std::string path) {
	path = Path::absolutePath(path);
	int ret = SDL_GameControllerAddMappingsFromFile(path.c_str());
	if(ret < 0) {
		LOG_WARN << "GameController mapping file error: " << SDL_GetError() << std::endl;
	}
	return ret;
}

std::string GameController::nameForSensor(SDL_SensorType sensor) {
	return shared::nameForSensor(sensor);
}

// naming matches RjDJ/PdParty #touch events
std::string GameController::nameForTouchEvent(SDL_EventType type) {
	switch(type) {
		case SDL_CONTROLLERTOUCHPADDOWN:   return "down";
		case SDL_CONTROLLERTOUCHPADMOTION: return "xy";
		case SDL_CONTROLLERTOUCHPADUP:     return "up";
		default: return "unknown";
	}
}

// PROTECTED

void GameController::enableAvailableSensors() {
	for(unsigned int i = 0; i < SDL_arraysize(shared::s_sensors); ++i) {
		SDL_SensorType sensor = shared::s_sensors[i];
		if(SDL_GameControllerHasSensor(m_controller, sensor)) {
			int ret = SDL_GameControllerSetSensorEnabled(m_controller, sensor, SDL_TRUE);
			if(ret < 0) {
				LOG_WARN << "GameController " << m_name
				         << ": could not enable sensor " << nameForSensor(sensor)
				         << ": " << SDL_GetError() << std::endl;
				continue;
			}
			m_prevSensorValues[sensor] = SensorValues();
		}
	}
}

bool GameController::buttonPressed(std::string &button, int value) {
	if(m_ignore && m_ignore->isIgnored(BUTTON, button)) {
		return false;
	}
	if(m_remapping) {
		button = m_remapping->mappingFor(BUTTON, button);
	}

	sender->send(Device::deviceAddress + m_address + "/button",
		"si", button.c_str(), value);
	
	if(Device::printEvents) {
		LOG << m_address << " " << m_name
		    << " button: " << button << " " << value << std::endl;
	}
	return true;
}

void GameController::axisMoved(const std::string &name, int value) {
	if(m_normalizeAxes) {
		float scaled = Device::normalizeAxisValue(value);
		sender->send(Device::deviceAddress + m_address + "/axis",
			"sf", name.c_str(), scaled);
		if(Device::printEvents) {
			LOG << m_address << " " << m_name
			    << " axis" << ": " << name << " " << scaled << std::endl;
		}
	}
	else {
		sender->send(Device::deviceAddress + m_address + "/axis",
			"si", name.c_str(), value);
		if(Device::printEvents) {
			LOG << m_address << " " << m_name
			    << " axis" << ": " << name << " " << value << std::endl;
		}
	}
}
