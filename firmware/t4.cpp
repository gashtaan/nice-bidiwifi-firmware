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

#include "t4.h"

const int RX_LED = 26;
const int TX_LED = 27;

void T4Client::init()
{
	m_serial.setTimeout(50);

	pinMode(RX_LED, OUTPUT);
	pinMode(TX_LED, OUTPUT);

	m_rxQueue = xQueueCreate(32, sizeof(T4Packet));
	m_txQueue = xQueueCreate(32, sizeof(T4Packet));

	m_unit.mutex = xSemaphoreCreateMutex();

	m_requestEvent = xEventGroupCreate();
	xEventGroupSetBits(m_requestEvent, EB_REQUEST_FREE);

	xTaskCreate(uartTaskThunk, "t4_uartTask", 8192, this, 10, &m_uartTaskHandle);
	xTaskCreate(scanTaskThunk, "t4_scanTask", 8192, this, 5, &m_scanTaskHandle);
	xTaskCreate(consumerTaskThunk, "t4_consumerTask", 8192, this, 5, &m_consumerTaskHandle);
}

void T4Client::uartTask()
{
	T4Packet rx_packet;
	uint8_t rx_packet_checksum = 0;
	enum { WAIT = 0, TYPE, SIZE, DATA, CHECKSUM, COMPLETE, RESET } rx_state = WAIT;

	for (;;)
	{
		digitalWrite(RX_LED, rx_state == WAIT);

		uint8_t byte;
		if (m_serial.readBytes(&byte, sizeof(byte)) == sizeof(byte))
		{
			if (rx_state != WAIT)
				rx_packet.data[rx_packet.size++] = byte;

			switch (rx_state)
			{
				case WAIT:
					rx_state = (byte == 0x00) ? TYPE : RESET;
					break;

				case TYPE:
					rx_state = (byte == 0x55 || byte == 0xF0) ? SIZE : RESET;
					break;

				case SIZE:
					rx_state = (byte <= 60) ? DATA : RESET;
					break;

				case DATA:
					rx_packet_checksum ^= byte;
					if (rx_packet.size == (rx_packet.data[1] + 1))
						rx_state = CHECKSUM;
					break;

				case CHECKSUM:
					if (byte == rx_packet_checksum)
						xQueueSend(m_rxQueue, &rx_packet, portMAX_DELAY);

					rx_state = RESET;
					break;
			}
		}
		else
		{
			rx_state = RESET;
		}

		if (rx_state == RESET)
		{
			rx_packet.size = 0;
			rx_packet_checksum = 0;
			rx_state = WAIT;
		}

		T4Packet tx_packet;
		if (xQueueReceive(m_txQueue, &tx_packet, 0))
		{
			digitalWrite(TX_LED, 0);

			m_serial.write(0);
			m_serial.write(tx_packet.data, tx_packet.size);

			digitalWrite(TX_LED, 1);

			// Serial.printf("Packet transmitted: %u\r\n", tx_packet.size);
		}

		// if packet is not yet complete or some data is still waiting, do not yield
		if (rx_state != WAIT || m_serial.available())
			continue;

		// yield to let others to do their job
		vTaskDelay(2);
	}

	m_uartTaskHandle = nullptr;
	vTaskDelete(nullptr);
}

void T4Client::scanTask()
{
	T4Packet reply;

	for (;;)
	{
		if (xSemaphoreTake(m_unit.mutex, portMAX_DELAY))
		{
			bool wait = false;

			if (m_unit.source.address == 0xFF && m_unit.source.endpoint == 0xFF)
			{
				// get CTRL_AUTOMATION_TYPE
				uint8_t message[5] = { CONTROLLER, 0x00, REQ|ACK|GET|FIN, 0x00, 0x00 };
				if (sendRequest(0x55, T4BroadcastAddress, T4ThisAddress, DMP, message, sizeof(message), &reply))
				{
					// Serial.println("CTRL_AUTOMATION_TYPE[get] received");

					m_unit.source = reply.header.from;
				}
			}
			else if (m_unit.commands.empty())
			{
				// info CTRL_STR_COMMANDS
				uint8_t message[5] = { CONTROLLER, 0x08, REQ|ACK|FIN, 0x00, 0x00 };
				if (sendRequest(0x55, m_unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply))
				{
					// Serial.println("CTRL_STR_COMMANDS[info] received");

					size_t commands_count = reply.message.dmp.data[4];
					const uint8_t* commands = &reply.message.dmp.data[5];
					m_unit.commands = std::vector<uint8_t>(commands, commands + commands_count);
				}
			}
			else if (!m_unit.menuComplete)
			{
				// get STD_MENU
				uint8_t message[6] = { STANDARD, 0x10, REQ|ACK|GET|FIN, uint8_t(m_unit.menu.size() * 2), 0x01, 0x04 };
				if (sendRequest(0x55, m_unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply))
				{
					// Serial.println("STD_MENU[get] received");

					size_t records_count = (reply.header.messageSize - 6) / 2;
					size_t records_last = reply.message.dmp.sequence / 2;
					size_t records_first = records_last - records_count;
					auto records = (const uint16_t*)&reply.message.dmp.data;

					m_unit.menu.resize(records_last);
					for (size_t n = 0; n < records_count; ++n)
						m_unit.menu[records_first + n] = records[n];

					m_unit.menuComplete = (reply.message.dmp.flags & FIN);
				}
			}
			else if (!m_unit.commandsInfoComplete)
			{
				// retrieve command info for all menu items
				bool complete = true;
				for (auto menu : m_unit.menu)
				{
					if (!menu || menu & 8)
						// skip root menu and groups
						continue;

					if (!m_unit.commandsInfo[menu >> 8])
					{
						// info CTRL_*
						uint8_t message[5] = { CONTROLLER, uint8_t(menu >> 8), REQ|ACK|FIN, 0x00, 0x00 };
						if (sendRequest(0x55, m_unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply))
						{
							// Serial.printf("CTRL_%02X[info] received\r\n", reply.message.command);

							// store info, but allocate at least 24 bytes to make checks for additional range fields easier (up to 4 bytes per value)
							uint8_t info_size = reply.message.dmp.sequence;
							auto info = std::make_unique<uint8_t[]>(std::max<size_t>(24, info_size));
							memcpy(info.get(), &reply.message.dmp.data, info_size);
							m_unit.commandsInfo[reply.message.command] = std::move(info);
						}

						complete = false;
						break;
					}
				}
				if (complete)
					m_unit.commandsInfoComplete = true;
			}
			else
			{
				wait = true;
			}

			xSemaphoreGive(m_unit.mutex);

			if (wait)
				vTaskDelay(1000);
		}
	}

	m_scanTaskHandle = nullptr;
	vTaskDelete(nullptr);
}

void T4Client::consumerTask()
{
	T4Packet packet;
	while (xQueueReceive(m_rxQueue, &packet, portMAX_DELAY))
	{
		// Serial.printf("Packet received: %u\r\n", packet.size);

		if (xEventGroupGetBits(m_requestEvent) & EB_REQUEST_PENDING)
		{
			if (m_requestPacket.header.from == packet.header.to &&
				m_requestPacket.header.protocol == packet.header.protocol &&
				m_requestPacket.message.device == packet.message.device &&
				m_requestPacket.message.command == packet.message.command)
			{
				xEventGroupClearBits(m_requestEvent, EB_REQUEST_PENDING);
				xEventGroupSetBits(m_requestEvent, EB_REQUEST_FREE);

				if (m_replyPacket)
					*m_replyPacket = packet;

				xEventGroupSetBits(m_requestEvent, EB_REQUEST_COMPLETE);
			}
		}

//		Serial.println("Packet received");
//		for (uint8_t n = 0; n < packet.size; ++n)
//			Serial.printf("%02X", packet.data[n]);
//		Serial.println();

		if (m_callback)
			m_callback(packet);
	}

	m_consumerTaskHandle = nullptr;
	vTaskDelete(nullptr);
}

bool T4Client::send(T4Packet& packet)
{
	return xQueueSend(m_txQueue, &packet, portMAX_DELAY);
}

bool T4Client::sendRequest(uint8_t type, T4Source to, T4Source from, uint8_t protocol, uint8_t* messageData, uint8_t messageSize, T4Packet* reply, uint8_t retry)
{
	do
	{
		xEventGroupWaitBits(m_requestEvent, EB_REQUEST_FREE, true, true, portMAX_DELAY);

		// Serial.println("Request about to transmit");

		m_replyPacket = reply;

		m_requestPacket = T4Packet(type, to, from, protocol, messageData, messageSize);
		xEventGroupSetBits(m_requestEvent, EB_REQUEST_PENDING);
		send(m_requestPacket);

		bool success = false;
		if (xEventGroupWaitBits(m_requestEvent, EB_REQUEST_COMPLETE, true, true, 500) & EB_REQUEST_COMPLETE)
		{
			// Serial.println("Reply received");

			success = true;
		}
		else
		{
			Serial.printf("Waiting for reply timed out (%u:%02X:%02X, retry:%u)\r\n", protocol, messageData[0], messageData[1], retry);
		}

		xEventGroupClearBits(m_requestEvent, EB_REQUEST_PENDING | EB_REQUEST_COMPLETE);
		xEventGroupSetBits(m_requestEvent, EB_REQUEST_FREE);

		if (success)
			return true;
	}
	while (retry-- > 0);

	return false;
}

T4Packet::T4Packet(uint8_t type, T4Source to, T4Source from, uint8_t protocol, uint8_t* messageData, uint8_t messageSize)
{
	// packet type
	packetType = type;

	// setup header
	header.to = to;
	header.from = from;
	header.protocol = protocol;
	header.messageSize = messageSize + 1;
	header.hash = hash(2, sizeof(header) - 1);

	// setup message
	memcpy(&message, messageData, messageSize);
	((uint8_t*)&message)[messageSize] = hash(9, messageSize);

	// update size(s)
	packetSize = 7 + messageSize + 1;
	size = packetSize + 3;
	data[size - 1] = packetSize;

//	Serial.println("Packet created");
//	for (uint8_t n = 0; n < size; ++n)
//		Serial.printf("%02X", data[n]);
//	Serial.println();
}
