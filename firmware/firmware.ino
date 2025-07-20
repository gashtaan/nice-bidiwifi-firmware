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

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <AsyncUDP.h>
#include <WebServer.h>

#include "t4.h"
#include "wireless.h"
#include "web.h"

const int RESET_BUTTON = 5;

T4Client t4(Serial2);
AsyncUDP udp;

void onT4Packet(T4Packet& t4Packet)
{
	if (udp)
		udp.broadcast(t4Packet.data, t4Packet.packetSize);
}

void onUDPPacket(AsyncUDPPacket& udpPacket)
{
	if (udpPacket.length() == 5 && !memcmp(udpPacket.data(), "RESET", 5))
		ESP.restart();

	T4Packet t4_packet;
	t4_packet.packetSize = udpPacket.length();
	memcpy(t4_packet.data, udpPacket.data(), udpPacket.length());
	t4.send(t4_packet);
}

void IRAM_ATTR resetButtonHandler()
{
	while (!digitalRead(RESET_BUTTON));
	ESP.restart();
}

void setup()
{
	pinMode(RESET_BUTTON, INPUT);
	attachInterrupt(RESET_BUTTON, resetButtonHandler, FALLING);

	Serial.begin(115200);
	Serial2.begin(19200, SERIAL_8N1, 18, 21);

	t4.setCallback(onT4Packet);
	t4.init();

	wifiInit();

	if (udp.listen(5090))
		udp.onPacket(onUDPPacket);

	ArduinoOTA.begin();

	webServerInit();
}

void loop()
{
	ArduinoOTA.handle();

	webServerHandle();

	vTaskDelay(2);
}
