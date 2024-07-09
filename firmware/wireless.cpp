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
#include <WiFi.h>

#include "wireless.h"

IPAddress ip(192, 168, 1, 20);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 1);

const char* wifiSSID = "your_ssid";
const char* wifiPassword = "your_password";

const int signalLED = 25;
TaskHandle_t signalTaskHandle = nullptr;
void signalTask(void*)
{
	for (;;)
	{
		if (WiFi.status() != WL_CONNECTED)
			digitalWrite(signalLED, !digitalRead(signalLED));
		else
			digitalWrite(signalLED, 0);

		vTaskDelay(500);
	}

	signalTaskHandle = nullptr;
	vTaskDelete(nullptr);
}

void wifiInit()
{
	pinMode(signalLED, OUTPUT);
	xTaskCreate(signalTask, "wifi_signalTask", 4096, NULL, 1, &signalTaskHandle);

	WiFi.persistent(false);
	WiFi.mode(WIFI_STA);
	WiFi.config(ip, gateway, subnet, dns);
	WiFi.hostname("Nice-T4-WebAccess");
	WiFi.begin(wifiSSID, wifiPassword);
	WiFi.setAutoReconnect(true);

	while (WiFi.status() != WL_CONNECTED)
	{
		Serial.print(".");
		delay(500);
	}
	Serial.println("Wi-Fi connected");
}
