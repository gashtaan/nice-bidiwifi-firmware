/*
   https://github.com/gashtaan/nice-bidiwifi-firmware

   Copyright (C) 2024, Michal Kovacik

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3, as
   published by the Free Software Foundation.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef T4_H
#define T4_H

#include <Arduino.h>
#include <vector>
#include <memory>

struct T4Source
{
	uint8_t address;
	uint8_t endpoint;

	bool operator==(const T4Source& other) const { return address == other.address && endpoint == other.endpoint; }
};

enum T4Flags : uint8_t
{
	FIN = 0x01,
	ACK = 0x08,
	GET = 0x10,
	SET = 0x20,
	EVT = 0x40,
	REQ = 0x80,
};

enum T4Protocol : uint8_t
{
	DEP = 1,
	DMP = 8,
};

enum T4Device : uint8_t
{
	STANDARD = 0,
	OVIEW = 1,
	CONTROLLER = 4,
	SCREEN = 6,
	RADIO = 10,
};

struct T4Packet
{
	uint8_t size = 0;
	union
	{
		uint8_t data[63];
		struct
		{
			uint8_t packetType;
			uint8_t packetSize;
			struct
			{
				T4Source to;
				T4Source from;
				uint8_t protocol;
				uint8_t messageSize;
				uint8_t hash;
			} header;
			struct
			{
				uint8_t device;
				uint8_t command;
				union
				{
					struct
					{
						uint8_t flags;
						uint8_t sequence;
						uint8_t status;
						uint8_t data[0];
					} dmp;
					struct
					{
						uint8_t data[0];
					} dep;
				};
				// uint8_t hash;
			} message;
		};
	};

	uint8_t hash(uint8_t i, uint8_t c) const
	{
		uint8_t h = 0;
		while (c-- > 0)
			h ^= data[i++];
		return h;
	}

	T4Packet() = default;
	T4Packet(uint8_t type, T4Source to, T4Source from, uint8_t protocol, uint8_t* messageData, uint8_t messageSize);
};

typedef std::function<void(T4Packet& packet)> T4Callback;

constexpr T4Source T4ThisAddress = { 0x50, 0x90 };
constexpr T4Source T4BroadcastAddress = { 0xFF, 0xFF };

enum
{
	EB_REQUEST_FREE = 1,
	EB_REQUEST_PENDING = 2,
	EB_REQUEST_COMPLETE = 4
};

struct T4Unit
{
	SemaphoreHandle_t mutex;

	T4Source source = { 0xFF, 0xFF };

	std::vector<uint8_t> commands;

	std::vector<uint16_t> menu;
	bool menuComplete = false;

	std::unique_ptr<uint8_t[]> commandsInfo[256] = {};
	bool commandsInfoComplete = false;
};

class T4Client
{
public:
	T4Client(HardwareSerial& serial) : m_serial(serial) {}

	void init();
	void setCallback(T4Callback callback) { m_callback = callback; }

	void uartTask();
	static void uartTaskThunk(void* self) { ((T4Client*)self)->uartTask(); }
	void scanTask();
	static void scanTaskThunk(void* self) { ((T4Client*)self)->scanTask(); }
	void consumerTask();
	static void consumerTaskThunk(void* self) { ((T4Client*)self)->consumerTask(); }

	bool send(T4Packet& packet);
	bool sendRequest(uint8_t type, T4Source to, T4Source from, uint8_t protocol, uint8_t* messageData, uint8_t messageSize, T4Packet* reply = nullptr, uint8_t retry = 0);

	bool lockUnit() { return xSemaphoreTake(m_unit.mutex, 1000); }
	bool unlockUnit() { return xSemaphoreGive(m_unit.mutex); }
	const auto& getUnit() { return m_unit; }

private:
	HardwareSerial& m_serial;

	TaskHandle_t m_uartTaskHandle = nullptr;
	TaskHandle_t m_scanTaskHandle = nullptr;
	TaskHandle_t m_consumerTaskHandle = nullptr;

	QueueHandle_t m_rxQueue = nullptr;
	QueueHandle_t m_txQueue = nullptr;

	T4Callback m_callback = nullptr;

	EventGroupHandle_t m_requestEvent;
	T4Packet m_requestPacket;
	T4Packet* m_replyPacket = nullptr;

	T4Unit m_unit;
};

constexpr const char* T4AutomationStatusStrings[]
{
	nullptr,
	"Stopped",										// STOPPED(1)
	"Opening in progress",							// OPENING_IN_PROGRESS(2)
	"Closing in progress",							// CLOSING_IN_PROGRESS(3)
	"Stopped in opened",							// STOPPED_IN_OPENED(4)
	"Stopped in closed",							// STOPPED_IN_CLOSED(5)
	"Active preflashing",							// ACTIVE_PREFLASHING(6)
	"Stopped in pause time",						// STOPPED_IN_PAUSE_TIME(7)
	"Searching devices...",							// SEARCHING_DEVICES(8)
	"Searching positions...",						// SEARCHING_POSITIONS(9)
	"Research devices finished",					// RESEARCH_DEVICES_FINISHED(10)
	"Research positions finished",					// RESEARCH_POSITIONS_FINISHED(11)
	"Research devices error",						// RESEARCH_DEVICES_ERROR(12)
	"Research positions error",						// RESEARCH_POSITIONS_ERROR(13)
	nullptr,
	nullptr,
	"Stopped in partial 1",							// STOPPED_IN_PARTIAL_1(16)
	"Stopped in partial 2",							// STOPPED_IN_PARTIAL_2(17)
	"Stopped in partial 3",							// STOPPED_IN_PARTIAL_3(18)
};

static const char* T4ManoeuvreStatusStrings[] = {
	"OK",
	"ERROR_ON_BLUEBUS",
	"PHOTO_INTERVENTION",
	"OBSTACLE_DETECTED",
	"HALT_DETECTED",
	"INTERNAL_PARAMETERS_ERROR",
	"MAXIMUM_NUMBER_OF_MANEUVERS_PER_HOUR_EXCEEDED",
	"ELECTRIC_ANOMALY",
	"BLOCKING_COMMAND",
	"BLOCKED_AUTOMATION",
	"DETECTED_OBSTACLE_BY_ENCODER"
};

constexpr const char* T4CommandStrings[]
{
	nullptr,
	"Step by Step",									// CMD_STST(1)
	"Stop",											// CMD_STP(2)
	"Open",											// CMD_OPN(3)
	"Close",										// CMD_CLS(4)
	"Open partial 1",								// CMD_OPN_I1(5)
	"Open partial 2",								// CMD_OPN_I2(6)
	"Open partial 3",								// CMD_OPN_I3(7)
	"Close partial 1",								// CMD_CLS_I1(8)
	"Close partial 2",								// CMD_CLS_I2(9)
	"Close partial 3",								// CMD_CLS_I3(10)
	"Apartament block Step by Step",				// CMD_STST_CND(11)
	"Hi priority Step by Step",						// CMD_STST_HP(12)
	"Open and lock",								// CMD_OPN_BLK(13)
	"Close and lock",								// CMD_CLS_BLK(14)
	"Lock",											// CMD_BLK_ON(15)
	"Unlock",										// CMD_BLK_OFF(16)
	"Courtesy light on",							// CMD_LDC_ON(17)
	"Courtesy light toggle",						// CMD_LDC_PP(18)
	"Master Step by Step",							// CMD_MS_PP(19)
	"Master open",									// CMD_MS_OPN(20)
	"Master close",									// CMD_MS_CLS(21)
	"Slave Step by Step",							// CMD_SL_PP(22)
	"Slave open",									// CMD_SL_OPN(23)
	"Slave close",									// CMD_SL_CLS(24)
	"Unlock and open",								// CMD_OPN_UNB(25)
	"Unlock and close",								// CMD_CLS_UNB(26)
	"Enable photo command apartament block open",	// CMD_ACND_ON(27)
	"Disable photo command apartament block open",	// CMD_ACND_OFF(28)
	"Enable loop input",							// CMD_LOOP_ON(29)
	"Disable loop input",							// CMD_LOOP_OFF(30)
	nullptr,
	nullptr,
	"Halt",											// CMD_ALT(33)
	"Photo open command",							// CMD_FT_CMD(34)
	"Photo command",								// CMD_PHOTO(35)
	"Photo 1 command",								// CMD_PHOTO1(36)
	"Photo 2 command",								// CMD_PHOTO2(37)
	"Photo 3 command",								// CMD_PHOTO3(38)
	"Emergency Stop",								// CMD_ALT_EM(39)
	"Emergency command",							// CMD_EM(40)
	"Stop for interlocking function",				// CMD_ALT_LOCK(41)
	"SBA sensor command",							// CMD_SBA(42)
	"Emergency Open",								// CMD_EM_OPN(43)
	"Emergency Close",								// CMD_EM_CLS(44)
};

constexpr const char* T4MenuStrings[]
{
	"Type automation",								// CTRL_AUTOMATION_TYPE(0x00)
	"State automation",								// CTRL_AUTOMATION_STATUS(0x01)
	"Slave state automation",						// CTRL_AUTOMATION_SLAVE_STATUS(0x02)
	"PCB version / configuration (only barriers)",	// CTRL_STR_VERSION_CONFIGURATION(0x03)
	"Modular control unit board version",			// CTRL_STR_MODULAR_CU_VERSION(0x04)
	"Search devices", 								// CTRL_SEARCH_DEVICES(0x05)
	"Function mode",								// CTRL_FUNCTION_MODE(0x06)
	"Radio controls mode 2",						// CTRL_RADIO_CONTROLS_MODE_2(0x07)
	"Commands",										// CTRL_STR_COMMANDS(0x08)
	"Activate receiver",							// CTRL_ACTIVATE_RECEIVER(0x09)
	"Search BlueBus devices",						// CTRL_SEARCH_BLUEBUS_DEVICES(0x0A)
	"Search positions",								// CTRL_SEARCH_POSITIONS(0x0B)
	"Delete parameters",							// CTRL_DELETE_PARAMETERS(0x0C)
	"Type of installation",							// CTRL_INSTALLATIONS_TYPE(0x0D)
	nullptr,
	"Command go to position",						// CTRL_CMD_QUOTA(0x0F)
	"Transformation ratio",							// CTRL_TRANSFORMATION_RATIO(0x10)
	"Current position",								// CTRL_POSITION_CURRENT(0x11)
	"Maximum opening position",						// CTRL_POSITION_MAXIMUM_OPENING(0x12)
	"Maximum closing position",						// CTRL_POSITION_MINIMUM_CLOSING(0x13)
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"Opening position",								// CTRL_POSITION_OPENING(0x18)
	"Closing position",								// CTRL_POSITION_CLOSING(0x19)
	"Position open person",							// CTRL_STR_POSITION_OPEN_PERSON(0x1A)
	"Pedestrian opening position 1",				// CTRL_PEDESTRIAN_POSITION_OPENING_1(0x1B)
	"Pedestrian opening position 2",				// CTRL_PEDESTRIAN_POSITION_OPENING_2(0x1C)
	"Pedestrian opening position 3",				// CTRL_PEDESTRIAN_POSITION_OPENING_3(0x1D)
	nullptr,
	nullptr,
	"Intermediate position",						// CTRL_INTERMEDIATE_POSITION(0x20)
	"Intermediate position 1",						// CTRL_INTERMEDIATE_POSITION_1(0x21)
	"Intermediate position 2",						// CTRL_INTERMEDIATE_POSITION_2(0x22)
	"Intermediate position 3",						// CTRL_INTERMEDIATE_POSITION_3(0x23)
	"Deceleration opening position",				// CTRL_DECELERATION_POSITION_OPENING(0x24)
	"Deceleration closing position",				// CTRL_DECELERATION_POSITION_CLOSING(0x25)
	"Deceleration intermediate position",			// CTRL_DECELERATION_POSITION_INTERMEDIATE(0x26)
	"Delete intermediate deceleration",				// CTRL_INTERMEDIATE_DECELERATION_DELETE(0x27)
	"Opening phase shift (M2 on M1)",				// CTRL_PHASE_SHIFT_OPENING(0x28)
	"Closing phase shift (M1 on M2)",				// CTRL_PHASE_SHIFT_CLOSING(0x29)
	"Discharging opening",							// CTRL_DISCHARGING_OPENING(0x2A)
	"Discharging closing",							// CTRL_DISCHARGING_CLOSING(0x2B)
	"Management discharging opening",				// CTRL_MANAGEMENT_DISCHARGING_OPENING(0x2C)
	"Management discharging closing",				// CTRL_MANAGEMENT_DISCHARGING_CLOSING(0x2D)
	"Position recovery for sensible border",		// CTRL_POSITION_RECOVERY_FOR_SENSIBLE_BORDER(0x2E)
	"Reset encoder",								// CTRL_RESET_ENCODER(0x2F)
	"Working with 2 engines",						// CTRL_WORKING_WITH_2_ENGINES(0x30)
	"Quantity brief inversion",						// CTRL_QUANTITY_BRIEF_INVERSION(0x31)
	"Initial deceleration during opening",			// CTRL_INITIAL_DECELERATION_DURING_OPENING(0x32)
	"Initial deceleration during closing",			// CTRL_INITIAL_DECELERATION_DURING_CLOSING(0x33)
	"Balancing",									// CTRL_BALANCING(0x34)
	"Braking level",								// CTRL_BRAKING_LEVEL(0x35)
	"Brake mode",									// CTRL_BRAKE_MODE(0x36)
	"Time force operation",							// CTRL_TIME_FORCE_OPERATION(0x37)
	"Sensibility management",						// CTRL_STR_SENSIBILITY_MANAGEMENT(0x38)
	"Obstacle sensitivity",							// CTRL_SENSITIVITY_OBSTACLE(0x39)
	"Opening sensitivity",							// CTRL_SENSITIVITY_OPENING(0x3A)
	"Closing sensitivity",							// CTRL_SENSITIVITY_CLOSING(0x3B)
	"Deceleration sensitivity",						// CTRL_SENSITIVITY_DECELERATION(0x3C)
	"Deceleration opening sensitivity",				// CTRL_SENSITIVITY_DECELERATION_OPENING(0x3D)
	"Deceleration closing sensitivity",				// CTRL_SENSITIVITY_DECELERATION_CLOSING(0x3E)
	"Delete maps in memory",						// CTRL_DELETE_MAPS_IN_MEMORY(0x3F)
	"Speed management",								// CTRL_STR_SPEED_MANAGEMENT(0x40)
	"Cruise speed", 								// CTRL_SPEED_CRUISE(0x41)
	"Opening speed",								// CTRL_SPEED_OPENING(0x42)
	"Closing speed",								// CTRL_SPEED_CLOSING(0x43)
	"Deceleration speed",							// CTRL_SPEED_DECELERATION(0x44)
	"Deceleration opening speed",					// CTRL_SPEED_DECELERATION_OPENING(0x45)
	"Deceleration closing speed",					// CTRL_SPEED_DECELERATION_CLOSING(0x46)
	"Strength management",							// CTRL_STR_STRENGTH_MANAGEMENT(0x47)
	"Management force (mode)",						// CTRL_FORCE_MANAGEMENT(0x48)
	"Cruise force",									// CTRL_FORCE_CRUISE(0x49)
	"Opening force",								// CTRL_FORCE_OPENING(0x4A)
	"Closing force",								// CTRL_FORCE_CLOSING(0x4B)
	"Deceleration force",							// CTRL_DECELERATION_FORCE(0x4C)
	"Deceleration opening force",					// CTRL_DECELERATION_FORCE_OPENING(0x4D)
	"Deceleration closing force",					// CTRL_DECELERATION_FORCE_CLOSING(0x4E)
	"Manual force",									// CTRL_MANUAL_FORCE(0x4F)
	"Output",										// CTRL_OUTPUT(0x50)
	"Output 1",										// CTRL_OUTPUT_1(0x51)
	"Output 2",										// CTRL_OUTPUT_2(0x52)
	"Output 3",										// CTRL_OUTPUT_3(0x53)
	"Output 4",										// CTRL_OUTPUT_4(0x54)
	"Output 5",										// CTRL_OUTPUT_5(0x55)
	"Output 6",										// CTRL_OUTPUT_6(0x56)
	nullptr,
	"Time SCA",										// CTRL_TIME_SCA(0x58)
	"Time FLASH",									// CTRL_TIME_FLASH(0x59)
	"Time eletric lock",							// CTRL_TIME_ELECTRIC_LOCK(0x5A)
	"Time courtesy light",							// CTRL_TIME_COURTESY_LIGHT(0x5B)
	"Time suction cup",								// CTRL_TIME_SUCTION_CUP(0x5C)
	"Mode traffic light BlueBus",					// CTRL_MODE_TRAFFIC_LIGHT_BLUEBUS(0x5D)
	"Acceleration",									// CTRL_ACCELERATION(0x5E)
	"Deceleration",									// CTRL_DECELERATION(0x5F)
	"Mode command",									// CTRL_MODE_COMMAND(0x60)
	"Mode command STEP-STEP",						// CTRL_MODE_COMMAND_STEPSTEP(0x61)
	"Mode command PARTIAL OPEN",					// CTRL_MODE_COMMAND_OPEN_PARTIAL(0x62)
	"Mode command OPEN",							// CTRL_MODE_COMMAND_OPEN(0x63)
	"Mode command CLOSE",							// CTRL_MODE_COMMAND_CLOSE(0x64)
	"Mode command STOP",							// CTRL_MODE_COMMAND_STOP(0x65)
	"Mode delay inversion foto",					// CTRL_MODE_DELAY_INVERSION_FOTO(0x66)
	nullptr,
	"Mode command PHOTO CLOSE",						// CTRL_MODE_COMMAND_PHOTO_CLOSE(0x68)
	"Mode command PHOTO OPEN",						// CTRL_MODE_COMMAND_PHOTO_OPEN(0x69)
	"Mode command PHOTO 3",							// CTRL_MODE_COMMAND_PHOTO_3(0x6A)
	"Mode command ALT open",						// CTRL_MODE_COMMAND_ALT_OPEN(0x6B)
	"Mode command ALT close",						// CTRL_MODE_COMMAND_ALT_CLOSE(0x6C)
	"Mode command PHOTO 1",							// CTRL_MODE_COMMAND_PHOTO_1(0x6D)
	"Mode command ALT pre closing",					// CTRL_MODE_COMMAND_ALT_PRECLOSING(0x6E)
	"Mode command emergency",						// CTRL_MODE_COMMAND_EMERGENCY(0x6F)
	"Input",										// CTRL_INPUT(0x70)
	"Input 1",										// CTRL_INPUT_1(0x71)
	"Input 2",										// CTRL_INPUT_2(0x72)
	"Input 3",										// CTRL_INPUT_3(0x73)
	"Input 4",										// CTRL_INPUT_4(0x74)
	"Input AUX type",								// CTRL_INPUT_AUX_TYPE(0x75)
	"Mode command n. rev. obstacle during opening",	// CTRL_MODE_COMMAND_REVERSE_OBSTACLE_DURING_OPENING(0x76)
	"Mode command n. rev. obstacle during closing",	// CTRL_MODE_COMMAND_REVERSE_OBSTACLE_DURING_CLOSING(0x77)
	"Mode command REV. OBSTACLES open",				// CTRL_MODE_COMMAND_REVERSE_OBSTACLE_OPEN(0x78)
	"Mode command REV. OBSTACLES close",			// CTRL_MODE_COMMAND_REVERSE_OBSTACLE_CLOSE(0x79)
	"Mode input for Reclose after photo",			// CTRL_MODE_INPUT_RECLOSE_AFTER_PHOTO(0x7A)
	"Mode input for Pause time",					// CTRL_MODE_INPUT_PAUSE_TIME(0x7B)
	"Input 5",										// CTRL_INPUT_5(0x7C)
	"Input 6",										// CTRL_INPUT_6(0x7D)
	nullptr,
	"Buzzer enable",								// CTRL_BUZZER_ENABLE(0x7F)
	"Automatic close",								// CTRL_AUTOMATIC_CLOSE(0x80)
	"Pause time",									// CTRL_PAUSE_TIME(0x81)
	"Automatic working 1",							// CTRL_AUTOMATIC_WORKING_1(0x82)
	"Close after photo",							// CTRL_STR_CLOSE_AFTER_PHOTO(0x83)
	"Reclose after photo (activation)",				// CTRL_RECLOSE_AFTER_PHOTO(0x84)
	"Time Reclose after photo",						// CTRL_RECLOSE_AFTER_PHOTO_TIME(0x85)
	"Mode Reclose after photo",						// CTRL_RECLOSE_AFTER_PHOTO_MODE(0x86)
	"Close Always",									// CTRL_STR_CLOSE_ALWAYS(0x87)
	"Always close (activation)",					// CTRL_ALWAYS_CLOSE(0x88)
	"Time Always close",							// CTRL_ALWAYS_CLOSE_TIME(0x89)
	"Mode Always close",							// CTRL_ALWAYS_CLOSE_MODE(0x8A)
	"Stand-by",										// CTRL_STR_STAND_BY(0x8B)
	"Stand-by (activation)",						// CTRL_STANDBY_ACTIVATION(0x8C)
	"Time Stand-by",								// CTRL_STANDBY_ACTIVATION_TIME(0x8D)
	"Mode Stand-by",								// CTRL_STANDBY_ACTIVATION_MODE(0x8E)
	"Torque",										// CTRL_STR_TORQUE(0x8F)
	"Starting torque (activation)",					// CTRL_STARTING_TORQUE(0x90)
	"Time Starting torque",							// CTRL_STARTING_TORQUE_TIME(0x91)
	"Water hammer",									// CTRL_WATER_HAMMER(0x92)
	"Pre-flashing",									// CTRL_STR_PRE_FLASHING(0x93)
	"Preflashing (activation)",						// CTRL_PREFLASHING(0x94)
	"Time Preflashing open",						// CTRL_PREFLASHING_TIME_OPEN(0x95)
	"Type inversion (brief or complete)",			// CTRL_TYPE_INVERSION(0x96)
	"Compensation sensible border",					// CTRL_COMPENSATION_SENSIBLE_BORDER(0x97)
	"Mode slave",									// CTRL_MODE_SLAVE(0x98)
	"Time Preflashing close",						// CTRL_PREFLASHING_TIME_CLOSE(0x99)
	"Block automatism",								// CTRL_BLOCK_AUTOMATISM(0x9A)
	"Internal radio switch",						// CTRL_INTERNAL_RADIO_INH(0x9B)
	"Keylock",										// CTRL_KEY_LOCK(0x9C)
	"Weight",										// CTRL_WEIGHT(0x9D)
	"Heating",										// CTRL_HEATING(0x9E)
	"Anti-burglary Mode",							// CTRL_BURGLARY_MODE(0x9F)
	"Always Invert",								// CTRL_ALWAYS_INVERT(0xA0)
	"Wifi Module is present",						// CTRL_WIFI_MODULE_IS_PRESENT(0xA1)
	"Decelerations",								// CTRL_DECELERATIONS(0xA2)
	"Invert movement direction",					// CTRL_INVERT_MOVEMENT_DIRECTION(0xA3)
	"Position of amperometric exclusion",			// CTRL_POSITION_AMPEROMETRIC_EXCLUSION(0xA4)
	"Pulses per segment mapping",					// CTRL_PULSES_SEGMENT_MAPPING(0xA5)
	"Disable control",								// CTRL_DISABLE_CONTROL(0xA6)
	"Time maximum work",							// CTRL_TIME_MAXIMUM_WORK(0xA7)
	"Emergency mode",								// CTRL_EMERGENCY_MODE(0xA8)
	"Test mode",									// CTRL_TEST_MODE(0xA9)
	"Reserved 1",									// CTRL_RESERVED_1(0xAA)
	"Reserved 2",									// CTRL_RESERVED_2(0xAB)
	"Minimum frequency",							// CTRL_MINIMUM_FREQUENCY(0xAC)
	"Inverter Mode",								// CTRL_INVERTER_MODE(0xAD)
	"Emergency deceleration",						// CTRL_EMERGENCY_DECELERATION(0xAE)
	"Position of PHOTO exclusion",					// CTRL_POSITION_PHOTO_EXCLUSION(0xAF)
	"Maintenance management",						// CTRL_MAINTENANCE_MANAGEMENT(0xB0)
	"Treshold alarm mainteance",					// CTRL_THRESHOLD_ALARM_MAINTENANCE(0xB1)
	"Maintenance maneuvers counter",				// CTRL_MAINTENANCE_MANEUVERS_COUNTER(0xB2)
	"Total maneuvers counter",						// CTRL_MANEUVERS_COUNTER(0xB3)
	"Delete mainteance maneuvers",					// CTRL_MAINTENANCE_MANEUVERS_DELETE(0xB4)
	"Sensitivity Intervention Time",				// CTRL_SENSITIVITY_INTERVENTION_TIME(0xB5)
	"I/O expansion board for modular control unit", // CTRL_IO_EXP_BOARD(0xB6)
	"Modular control unit: EU or UL325 version",	// CTRL_MODULAR_BOARD_OPERATIONS(0xB7)
	"Radio codes management",						// CTRL_RADIO_CODES_MANAGEMENT(0xB8)
	"Courtesy light",								// CTRL_COURTESY_LIGHT_MNG(0xB9)
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"Installation speed",							// CTRL_INSTALLATION_SPEED(0xCF)
	"Diagnostics BlueBus devices",					// CTRL_DIAGNOSTICS_BLUEBUS_DEVICES(0xD0)
	"Diagnostics inputs / outputs",					// CTRL_DIAGNOSTICS_INPUT_OUTPUT(0xD1)
	"Diagnostics hardware",							// CTRL_DIAGNOSTICS_HARDWARE(0xD2)
	"Diagnostics other",							// CTRL_DIAGNOSTICS_OTHER(0xD3)
	"Diagnostics inverter",							// CTRL_DIAGNOSTICS_INVERTER(0xD4)
	"Diagnostics visual",							// CTRL_DIAGNOSTICS_VISUAL(0xD5)
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"Log last 8 maneuvers",							// CTRL_LOG_8_MANEUVERS(0xDA)
	"Last 8 Advanced diagnostics status",			// CTRL_ADVANCED_LOG(0xDB)
	"Log test 1",									// CTRL_LOG_TEST_1(0xDC)
	"Log test 2",									// CTRL_LOG_TEST_2(0xDD)
	"Log test 3",									// CTRL_LOG_TEST_3(0xDE)
	"Log test 4",									// CTRL_LOG_TEST_4(0xDF)
	"Minimum automatic force",						// CTRL_FORCE_AUTOMATIC_MINIMUM(0xE0)
	"Maximum automatic force",						// CTRL_FORCE_AUTOMATIC_MAXIMUM(0xE1)
	"Automatic strength for minimum slow down",		// CTRL_FORCE_AUTOMATIC_SLOWING_DOWN_MAXIMUM(0xE2)
	"Automatic strength for maximum slow down",		// CTRL_FORCE_AUTOMATIC_SLOWING_DOWN_MINIMUM(0xE3)
	"Sensibility loop",								// CTRL_LOOP_SENSIBILITY(0xE4)
	"Calibration loop",								// CTRL_LOOP_CALIBRATION(0xE5)
	"Supply loop",									// CTRL_LOOP_SUPPLY(0xE6)
	"Activate loop working",						// CTRL_LOOP_WORKING_ACTIVATE(0xE7)
	"Mode loop working",							// CTRL_LOOP_WORKING_MODE(0xE8)
	"Time loop recalibration",						// CTRL_LOOP_RECALIBRATION_TIME(0xE9)
	"Mode activation loop",							// CTRL_LOOP_ACTIVATION_MODE(0xEA)
	"Time activation loop",							// CTRL_LOOP_ACTIVATION_TIME(0xEB)
	"Loop Burglary function",						// CTRL_BURGLARY_LOOP_DETECT(0xEC)
	"View frequency loop",							// CTRL_LOOP_FREQUENCY_VIEW(0xED)
	"View pressure test",							// CTRL_PRESS_TEST(0xEE)
	"Test 4",										// CTRL_TEST4(0xEF)
	"Installation",									// CTRL_STR_INSTALLATION(0xF0)
	"Main parameters",								// CTRL_STR_PARAMETERS_MAIN(0xF1)
	"Advanced parameters",							// CTRL_STR_PARAMETERS_ADVANCED(0xF2)
	"Positions",									// CTRL_STR_POSITIONS(0xF3)
	"Security",										// CTRL_STR_SECURITY(0xF4)
	"Maintenance",									// CTRL_STR_MAINTENANCE(0xF5)
	"Diagnostics",									// CTRL_STR_DIAGNOSTICS(0xF6)
	"Options",										// CTRL_STR_OPTIONS(0xF7)
	"Inputs setup",									// CTRL_STR_SETUP_INPUTS(0xF8)
	"Outputs setup",								// CTRL_STR_SETUP_OUTPUTS(0xF9)
	"Commands setup",								// CTRL_STR_SETUP_COMMANDS(0xFA)
	"Password",										// CTRL_STR_PASSWORD(0xFB)
	nullptr,
	"Loop detector",								// CTRL_STR_LOOP_DETECTOR(0xFD)
	"Inverter",										// CTRL_STR_INVERTER(0xFE)
	nullptr,
};

constexpr const char* T4ListCommandStrings[] =
{
	"Not configured",								// NONE(0)
	"Open-Stop-Close-Stop",							// ASCS(1)
	"Open-Stop-Close-Open",							// ASCA(2)
	"Open-Close-Open-Close",						// ACAC(3)
	"Apartment block 1 Step-step",					// PP_CND1(4)
	"Apartment block 2 Step-step",					// PP_CND2(5)
	"Step-step 2",									// PP2(6)
	"Person present",								// UP(7)
	"Industrial mode",								// AS_CUP(8)
	"Open-Stop-Open",								// ASA(9)
	"Apartment block 1 open",						// ACND1(10)
	"Apartment block 2 open)",						// ACND2(11)
	"Open 2",										// A2(12)
	"Hold-to-run Open",								// AUP(13)
	"Close-Stop-Close",								// CSC(14)
	"Apartment block 1 close",						// CCND1(15)
	"Apartment block 2 close",						// CCND2(16)
	"Close 2",										// C2(17)
	"Hold-to-run Close",							// CUP(18)
	"Stop and inversion",							// INV(19)
	"Temporary Stop",								// ALT_MOV(20)
	"Stop",											// STP(21)
	"Stop and brief inversion",						// STP_INV(22)
	"Halt",											// ALT(23)
	"Halt and brief inversion",						// ALT_INV(24)
	"Halt and inversion",							// INV1(25)
	"Operation during closure and opening",			// INT_CO(26)
	"Operation during closure",						// INT_C(27)
	"Stop and inversion towards the closure",		// ALT_MOV2(28)
	"Apartament block locking",						// BLOCK_CND(29)
};

constexpr const char* T4ListInStrings[] =
{
	"No function",									// CMD_NONE(0)
	"Step by Step",									// CMD_STST(1)
	"Stop",											// CMD_STP(2)
	"Open",											// CMD_OPN(3)
	"Close",										// CMD_CLS(4)
	"Open partial 1",								// CMD_OPN_I1(5)
	"Open partial 2",								// CMD_OPN_I2(6)
	"Open partial 3",								// CMD_OPN_I3(7)
	"Close partial 1",								// CMD_CLS_I1(8)
	"Close partial 2",								// CMD_CLS_I2(9)
	"Close partial 3", // CMD_CLS_I3(10)
	"Apartament block Step by Step", // CMD_STST_CND(11)
	"Hi priority Step by Step", // CMD_STST_HP(12)
	"Open and lock", // CMD_OPN_BLK(13)
	"Close and lock", // CMD_CLS_BLK(14)
	"Lock", // CMD_BLK_ON(15)
	"Unlock", // CMD_BLK_OFF(16)
	"Courtesy light on", // CMD_LDC_ON(17)
	"Courtesy light toggle", // CMD_LDC_PP(18)
	"Master Step by Step", // CMD_MS_PP(19)
	"Master open", // CMD_MS_OPN(20)
	"Master close", // CMD_MS_CLS(21)
	"Slave Step by Step", // CMD_SL_PP(22)
	"Slave open", // CMD_SL_OPN(23)
	"Slave close", // CMD_SL_CLS(24)
	"Unlock and open", // CMD_OPN_UNB(25)
	"Unlock and close", // CMD_CLS_UNB(26)
	"Enable photo command apartament block open", // CMD_ACND_ON(27)
	"Disable photo command apartament block open", // CMD_ACND_OFF(28)
	"Enable loop input", // CMD_LOOP_ON(29)
	"Disable loop input", // CMD_LOOP_OFF(30)
	"Halt", // CMD_ALT(33)
	"Photo open command", // CMD_FT_CMD(34)
	"Photo command", // CMD_PHOTO(35)
	"Photo 1 command", // CMD_PHOTO1(36)
	"Photo 2 command", // CMD_PHOTO2(37)
	"Photo 3 command", // CMD_PHOTO3(38)
	"Emergency Stop", // CMD_ALT_EM(39)
	"Emergency command", // CMD_EM(40)
	"Stop for interlocking function", // CMD_ALT_LOCK(41)
	"SBA sensor command", // CMD_SBA(42)
	"Emergency Open", // CMD_EM_OPN(43)
	"Emergency Close", // CMD_EM_CLS(44)
	"Command for production testing", // CMD_TESTING(48)
	"Command for Buzzer testing", // CMD_BUZZER_TEST(49)
	"Courtesy light OFF", // CMD_LDC_SWITCH_OFF(65)
	"Courtesy light ON (ON time is regulated by the Hardware)", // CMD_LDC_SWITCH_ON(66)
};

constexpr const char* T4ListOutStrings[] =
{
	"No function",									// NONE(0)
	"SCA",											// SCA(1)
	"Open gate",									// MTO(2)
	"Close gate",									// MTC(3)
	"Maintenance light",							// SMN(4)
	"Lamp",											// LGT(5)
	"Courtesy light",								// LDC(6)
	"Electric lock 1",								// ELS_1(7)
	"Electric lock 2",								// ELS_2(8)
	"Electric lock 1",								// ELB_1(9)
	"Electric lock 2",								// ELB_2(10)
	"Ventosa 1",									// VNT_1(11)
	"Ventosa 2",									// VNT_2(12)
	"Red light",									// SPH_R(13)
	"Green light",									// SPH_G(14)
	"Radio Channel No 1",							// CHN_1(15)
	"Radio Channel No 2",							// CHN_2(16)
	"Radio Channel No 3",							// CHN_3(17)
	"Radio Channel No 4",							// CHN_4(18)
	"Lamp 1",										// LGT1(19)
	"SCA 1",										// SCA1(20)
	"SCA 2",										// SCA2(21)
	"Always on",									// SON(22)
	"Lamp 24V",										// LGT24V(23)
	"Output loop 1",								// OUT_LOOP_1(24)
	"Output loop 2",								// OUT_LOOP_2(25)
	"Light One Way Input",							// LGT_1_WAY_IN(26)
	"Ligth One Way Flashing",						// LGT_1_WAY_FLASH(27)
	"Light Alternative Way",						// LGT_ALT_WAY(28)
	"Output buzzer",								// OUT_BUZZ(29)
	"Output port state",							// OUT_PORT_STATE(30)
	"Output central state",							// OUT_CENTR_STATE(31)
	"Output fan",									// OUT_FAN(32)
	"Light One Way for pedestrial Canada",			// LGT_1_WAY_CANADA(33)
	"Interlocking 2 ports",							// INTLOCK_2_PORTS(34)
	"Active exit during maneuver",					// MANEUVER(35)
	"",
	"Fototest",										// FOTOTEST(37)
};

constexpr const char* T4FunctionsModeStrings[] = {
	"Off",											// OFF(0)
	"On",											// ON(1)
	nullptr,
	nullptr,
	"Manual",										// MANU(4)
	"Automatic",									// AUTO(5)
	"Semi automatic 1",								// SEMI_1(6)
	"Semi automatic 2",								// SEMI_2(7)
	"Maneuver",										// MNVR(8)
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"Open all",										// OPN_ALL(16)
	"Open disengage",								// OPN_DIS(17)
	"Stop",											// STP(18)
	"Open all 2",									// OPN_ALL_2(19)
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"Close all",									// CLS_ALL(32)
	"Save closure",									// CLS_SAVE(33)
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"Stand-by BlueBus",								// SBY_BB(48)
	"Stand-by security",							// SBY_SEC(49)
	"Stand-by all",									// SBY_HRD(50)
	"Stand-by automatic",							// SBY_AUTO(51)
	"Stand-by automatic 2",							// STAND_BY(52)
	"Photo test",									// PHOTO_TEST(53)
	"Light",										// LIGHT(54)
	"Heavy",										// HEAVY(55)
	"Stand-by, internal wifi on",					// SBY_NOT_INT_WIFI(56)
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"All",											// ALL(64)
	"Loop",											// LOOP(65)
	"Photo",										// PHOTO(66)
	"Command",										// CMD(67)
};

#endif
