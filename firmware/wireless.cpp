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
#include <lwip/ip.h>
#include <lwip/icmp.h>
#include <lwip/inet.h>
#include <lwip/inet_chksum.h>
#include <lwip/sockets.h>
#include <lwip/err.h>

#include "wireless.h"

IPAddress ip(192, 168, 1, 20);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 1);

const char* wifiSSID = "your_ssid";
const char* wifiPassword = "your_password";

TaskHandle_t checkTaskHandle = nullptr;

const int signalLED = 25;
TaskHandle_t signalTaskHandle = nullptr;

bool ping()
{
	if (WiFi.status() != WL_CONNECTED)
		return false;

	icmp_echo_hdr icmp_request = {};

	ICMPH_TYPE_SET(&icmp_request, ICMP_ECHO);
	ICMPH_CODE_SET(&icmp_request, 0);
	icmp_request.chksum = inet_chksum((icmp_echo_hdr*)&icmp_request, sizeof(icmp_request));

	struct sockaddr_in to_address;
	to_address.sin_len = sizeof(to_address);
	to_address.sin_family = AF_INET;
	to_address.sin_addr.s_addr = gateway;

	int sock = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);

	const struct timeval timeout = { 2, 0 };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	sendto(sock, &icmp_request, sizeof(icmp_request), 0, (struct sockaddr*)&to_address, sizeof(to_address));

	// do not check response itself, it doesn't really matter what it is, any response confirms the WiFi is still online
	char response[64];
	return (recv(sock, response, sizeof(response), 0) > 0);
}

void checkTask(void*)
{
	size_t ping_misses = 0;
	size_t reconnects_fails = 0;

	for (;;)
	{
		if (ping())
		{
			ping_misses = 0;
			reconnects_fails = 0;

			// postpone next check by 10s
			vTaskDelay(10000);
		}
		else
		{
			if (++ping_misses == 10)
			{
				ping_misses = 0;

				if (++reconnects_fails == 3)
					ESP.restart();
				else
					WiFi.reconnect();
			}

			// postpone next check by 1s
			vTaskDelay(1000);
		}
	}

	checkTaskHandle = nullptr;
	vTaskDelete(nullptr);
}

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

	xTaskCreate(checkTask, "wifi_checkTask", 4096, NULL, 1, &checkTaskHandle);

	while (WiFi.status() != WL_CONNECTED)
	{
		Serial.print(".");
		delay(500);
	}
	Serial.println("Wi-Fi connected");
}
