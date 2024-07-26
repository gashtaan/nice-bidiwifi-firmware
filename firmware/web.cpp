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

#include <WiFi.h>

#include "web.h"
#include "t4.h"

extern T4Client t4;

WebServer web_server(80);

void authenticate()
{
	if ((web_server.client().remoteIP() & 0x00FFFFFF) == (WiFi.gatewayIP() & 0x00FFFFFF))
		// requests from local network require no authentication
		return;

	static const char* login = "login";
	static const char* password = "fa9beb99e4029ad5a6615399e7bbae21356086b3"; // SHA1 hash of "changeme"
	if (web_server.authenticateBasicSHA1(login, password))
		// already authenticated
		return;

	web_server.requestAuthentication();
}

String header(const char* title = nullptr)
{
	return R"(
<meta name="viewport" content="width=device-width, initial-scale=0.6"/>
<title>Nice T4 Web-Access)" + (title ? " " + String(title) : "") + R"(</title>
<style>
	* { font-family: sans-serif }
	H1 { background-color: #01569D; color:white; padding:5px }
	A { color: #01569D; text-decoration:none }
	BUTTON { background-color: #01569D; color:white; border:0; padding:50px; margin-bottom:5px; width:100%; display-block }
	INPUT[type=submit] { background-color: #01569D; color:white; border:0; padding:10px }
	#footer, #footer A { color:#808080; font-size:12px }
</style>)";
}

String footer()
{
	return R"(
<br/>
<div id="footer">
	Nice T4 Web-Access<br/>
	<a href="https://github.com/gashtaan/nice-bidiwifi-firmware">https://github.com/gashtaan/nice-bidiwifi-firmware</a>
</div>)";
}

String createSelect(uint8_t command, uint8_t value, const char* const strings[], size_t stringsCount, uint8_t* list, uint8_t listSize)
{
	String html;
	html += "<select id=\"p" + String(command) + "\" onchange=\"this.name=this.id\">";

	for (size_t n = 0; n < listSize; ++n)
	{
		auto m = list[n];
		html += "<option value=\"" + String(m) + "\"" + (value == m ? " selected" : "") + ">";
		if (m < stringsCount)
			html += strings[m];
		html += "</option>";
	}

	html += "</select>";
	return html;
}

void web_root()
{
	authenticate();

	if (!t4.lockUnit())
		return web_server.send(500, "text/plain", "Error");

	auto& unit = t4.getUnit();

	String html = header();
	html += "<h1>Nice T4 Web-Access</h1>";
	html += "Wi-Fi RSSI: " + String(WiFi.RSSI()) + " dBm<br/><br/>";
	html += "Control unit address: " + String(unit.source.address) + ":" + String(unit.source.endpoint) + "<br/>";

	T4Packet reply;

	// CTRL_POSITION_CURRENT(0x11)
	uint8_t message[5] = { CONTROLLER, 0x11, REQ|GET|ACK|FIN, 0x00, 0x00 };
	if (t4.sendRequest(0x55, unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply, 3))
		html += "Current position: " + String((reply.message.dmp.data[0] << 8) | reply.message.dmp.data[1]) + "<br/>";

	// CTRL_AUTOMATION_STATUS(0x01)
	message[1] = 0x01;
	if (t4.sendRequest(0x55, unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply, 3))
	{
		uint8_t automation_status = reply.message.dmp.data[0];
		if (automation_status < std::size(T4AutomationStatusStrings) && T4AutomationStatusStrings[automation_status])
			html += "Automation status: " + String(T4AutomationStatusStrings[automation_status]) + "<br/>";
	}

	html += "<br/>";
	html += "<a href=\"/configure\">Configure</a><br/>";
	html += "<a href=\"/log\">Log</a><br/>";
	html += "<a href=\"/status\">Status</a><br/>";
	html += "<br/>";

	for (auto command : unit.commands)
		html += "<button onclick=\"location='/execute?command=" + String(command) + "'\">" + String(T4CommandStrings[command]) + "</button>\n";

	t4.unlockUnit();

	html += footer();

	web_server.send(200, "text/html", html);
}

void web_configure_get()
{
	authenticate();

	if (!t4.lockUnit())
		return web_server.send(500, "text/plain", "Error");

	auto& unit = t4.getUnit();

	auto root = web_server.arg("root").toInt();

	auto root_menu_it = std::find_if(unit.menu.begin(), unit.menu.end(), [root](const auto& m) { return (m >> 8) == root; });
	if (root_menu_it == unit.menu.end())
	{
		web_server.send(400, "text/plain", "Bad request");
		return;
	}

	String html = header("Configuration");

	html += "<h1>";
	html += "Configuration";
	if (root && T4MenuStrings[root])
	{
		html += " / ";
		html += T4MenuStrings[root];
	}
	html += "</h1>\n";

	html += "<form method=\"post\">\n";
	html += "<table>\n";

	bool show_save = false;
	uint8_t current_indent = 0;

	// iterate over menu items backward and find the group for link to upper level
	uint8_t upper_root = 0;
	for (auto menu_it = root_menu_it - 1; menu_it >= unit.menu.begin(); --menu_it)
	{
		if ((*menu_it & 8) && (*menu_it & 7) < (*root_menu_it & 7))
		{
			upper_root = (*menu_it >> 8);
			break;
		}
	}

	// iterate over menu items forward
	for (auto menu_it = root_menu_it + 1; menu_it < unit.menu.end(); ++menu_it)
	{
		uint8_t command = (*menu_it >> 8);
		uint8_t indent = (*menu_it & 7);
		bool group = (*menu_it & 8);

		if (!current_indent)
			current_indent = indent;

		if (indent > current_indent)
			continue;
		if (indent < current_indent)
			break;

		html += "<tr><td>";

		auto command_info = unit.commandsInfo[command].get();

		if (group)
		{
			// link to nested group
			html += "<a href=\"/configure?root=" + String(command) + "\">" + T4MenuStrings[command] + "</a>";
		}
		else if (command_info && (command_info[2] & 0xF0) == 0xE0)
		{
			// link to diagnostics
			html += "<a href=\"/diagnostics?root=" + String(command) + "\">" + T4MenuStrings[command] + "</a>";
		}
		else
		{
			// form input
			html += T4MenuStrings[command];

			html += "</td><td>";
			if (command_info)
			{
				T4Packet reply;
				uint8_t message[5] = { CONTROLLER, command, REQ|GET|ACK|FIN, 0x00, 0x00 };
				if (t4.sendRequest(0x55, unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply, 3))
				{
					size_t value_size = command_info[0] & 0x7F;
					uint64_t value = 0;
					for (uint8_t n = 0; n < value_size; ++n)
						value = (value << 8) | reply.message.dmp.data[n];

					if (command_info[3] & 0x40)
					{
						// list
						if (command_info[2] == 0x01)
						{
							static const char* on_off_strings[] = { "Off", "On" };
							html += createSelect(command, uint8_t(value), on_off_strings, std::size(on_off_strings), &command_info[5], command_info[4]);
						}
						else if (command_info[2] == 0xF2)
						{
							html += createSelect(command, uint8_t(value), T4ListInStrings, std::size(T4ListInStrings), &command_info[5], command_info[4]);
						}
						else if (command_info[2] == 0xF3)
						{
							html += createSelect(command, uint8_t(value), T4ListCommandStrings, std::size(T4ListCommandStrings), &command_info[5], command_info[4]);
						}
						else if (command_info[2] == 0xF4)
						{
							html += createSelect(command, uint8_t(value), T4ListOutStrings, std::size(T4ListOutStrings), &command_info[5], command_info[4]);
						}
						else if (command_info[2] == 0xF5)
						{
							html += createSelect(command, uint8_t(value), T4FunctionsModeStrings, std::size(T4FunctionsModeStrings), &command_info[5], command_info[4]);
						}
						else if (command_info[2] == 0xF7)
						{
							const char* delete_strings[256] = { "Nothing", "Positions", "Devices", "Functions" };
							delete_strings[0x7D] = "All";
							html += createSelect(command, uint8_t(value), delete_strings, std::size(delete_strings), &command_info[5], command_info[4]);
						}
					}
					else if (command_info[1] == 0x03)
					{
						// text
						html += "<input value=\"" + String((const char*)&reply.message.dmp.data[1]) + "\" disabled/>";
					}
					else
					{
						// range
						html += "<input id=\"p" + String(command) + "\" value=\"" + String(value) + "\" onchange=\"this.name=this.id\"/>";

						if (command_info[1] == 0x25)
						{
							// function type VIRTUAL_POSITION(0x25) seems to be always in millimeters, don't know why the unit doesn't report correct type info
							html += " mm";
						}
						else
						{
							switch (command_info[2])
							{
								case 0x0A: html += " %"; break;		// PCT
								case 0x10: html += " m"; break;		// MINUTES
								case 0x11: html += " s"; break;		// SECONDS
								case 0x12: html += " ms"; break;	// MILLISECONDS
								case 0x14: html += " m"; break;		// METERS
								case 0x15: html += " cm"; break;	// CENTIMETERS
								case 0x17: html += " &deg;"; break;	// DEGREES
								case 0x18: html += " N"; break;		// NEWTON
								case 0x19: html += " A"; break;		// AMPERE
								case 0x1A: html += " mA"; break;	// MILLIAMP
								case 0x1B: html += " V"; break;		// VOLT
								case 0x1C: html += " mV"; break;	// MILLIVOLT
								case 0x1D: html += " W"; break;		// WATT
								case 0x1E: html += " mW"; break;	// MILLIWATT
							}
						}

						uint8_t* info_ptr = &command_info[4];
						auto info_pop = [&](size_t n)
						{
							uint64_t value = 0;
							while (n-- > 0)
								value = (value << 8) | *info_ptr++;
							return value;
						};
						uint64_t min = info_pop(value_size);
						uint64_t max = info_pop(value_size);
						uint64_t step = info_pop(value_size);
						uint64_t scale = info_pop(sizeof(uint16_t));
						bool divide = command_info[3] & 0x10;
						bool multiply = command_info[3] & 0x20;

						if (max)
						{
							html += " (" + String(min) + "&#8209;" + String(max);
							if (step)
								html += ":" + String(step);
							if (divide)
								html += "/" + String(scale);
							else if (multiply)
								html += "*" + String(scale);
							html += ")";
						}
					}
				}

				show_save = true;
			}
			else
			{
				html += "Unknown command";
			}
		}

		html += "</td></tr>\n";
	}

	t4.unlockUnit();

	html += "</table>\n";

	if (show_save)
		html += "<input type=\"submit\" value=\"Save\"/>";

	html += "</form>\n";

	html += "<a href=\"";
	if (root)
		html += "?root=" + String(upper_root);
	else
		html += "/";
	html += "\">&Ll; Back</a><br/>";

	html += footer();

	web_server.send(200, "text/html", html);
}

void web_configure_post()
{
	authenticate();

	if (!t4.lockUnit())
		return web_server.send(500, "text/plain", "Error");

	auto& unit = t4.getUnit();

	auto root = web_server.arg("root").toInt();

	for (size_t n = 0; n < web_server.args(); ++n)
	{
		auto arg_name = web_server.argName(n);
		if (arg_name[0] != 'p')
			continue;
		char* number_end;
		auto command = strtol(arg_name.c_str() + 1, &number_end, 10);
		if (!command || command > 0xFF || *number_end)
			continue;

		auto arg_value = web_server.arg(n).toInt();

		auto command_info = unit.commandsInfo[command].get();
		if (!command_info)
			continue;

		uint8_t message[5 + 32] = { CONTROLLER, uint8_t(command), REQ|SET|ACK|FIN, 0x00, 0x00 };

		size_t value_size = command_info[0] & 0x7F;
		for (uint8_t n = 0; n < value_size; ++n)
			message[5 + n] = ((const uint8_t*)&arg_value)[value_size - n - 1];

		T4Packet reply;
		t4.sendRequest(0x55, unit.source, T4ThisAddress, DMP, message, 5 + value_size, &reply, 3);
	}

	t4.unlockUnit();

	web_server.sendHeader("Location", "?root=" + String(root));
	web_server.send(303, "text/plain", "Redirect");
}

void web_diagnostics()
{
	authenticate();

	auto root = web_server.arg("root").toInt();

	if (!t4.lockUnit())
		return web_server.send(500, "text/plain", "Error");

	auto& unit = t4.getUnit();

	auto command_info = unit.commandsInfo[root].get();

	T4Packet reply;
	uint8_t message[5] = { CONTROLLER, uint8_t(root), REQ|GET|ACK|FIN, 0x00, 0x00 };
	bool reply_ok = t4.sendRequest(0x55, unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply, 3);

	if (reply_ok && command_info)
	{
		String html = header("Diagnostics");

		switch (command_info[2])
		{
			case 0xE1:
			{
				html += "<h1>Diagnostics / Inputs/Outputs</h1>\n";

				const auto data = reply.message.dmp.data;
				const auto info = &command_info[5];

				html += "<table>\n";

				if (info[0] & 0x01)
					html += "<tr><td>Input halt</td><td>" + String((data[0] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[0] & 0x02)
					html += "<tr><td>Input 1 PP</td><td>" + String((data[0] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[0] & 0x04)
					html += "<tr><td>Input 2 AP</td><td>" + String((data[0] & 0x04) ? "On" : "Off") + "</td></tr>";
				if (info[0] & 0x08)
					html += "<tr><td>Input 3 CH</td><td>" + String((data[0] & 0x08) ? "On" : "Off") + "</td></tr>";
				if (info[0] & 0x10)
					html += "<tr><td>Loop 1</td><td>" + String((data[0] & 0x10) ? "On" : "Off") + "</td></tr>";
				if (info[0] & 0x20)
					html += "<tr><td>Loop 2</td><td>" + String((data[0] & 0x20) ? "On" : "Off") + "</td></tr>";

				if (info[1] & 0x01)
					html += "<tr><td>Button 1</td><td>" + String((data[1] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[1] & 0x02)
					html += "<tr><td>Button 2</td><td>" + String((data[1] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[1] & 0x04)
					html += "<tr><td>Button 3</td><td>" + String((data[1] & 0x04) ? "On" : "Off") + "</td></tr>";

				if (info[2] & 0x01)
					html += "<tr><td>Fca M1</td><td>" + String((data[2] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[2] & 0x02)
					html += "<tr><td>Fcc M1</td><td>" + String((data[2] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[2] & 0x04)
					html += "<tr><td>Fca M2</td><td>" + String((data[2] & 0x04) ? "On" : "Off") + "</td></tr>";
				if (info[2] & 0x08)
					html += "<tr><td>Fcc M2</td><td>" + String((data[2] & 0x08) ? "On" : "Off") + "</td></tr>";
				if (info[2] & 0x10)
					html += "<tr><td>Unlock M1</td><td>" + String((data[2] & 0x10) ? "On" : "Off") + "</td></tr>";
				if (info[2] & 0x20)
					html += "<tr><td>Unlock M2</td><td>" + String((data[2] & 0x20) ? "On" : "Off") + "</td></tr>";
				if (info[2] & 0x40)
					html += "<tr><td>Selection direction</td><td>" + String((data[2] & 0x40) ? "Left" : "Right") + "</td></tr>";
				if (info[2] & 0x80)
					html += "<tr><td>Selection engine</td><td>" + String((data[2] & 0x80) ? "Left" : "Right") + "</td></tr>";

				if (info[3] & 0x01)
					html += "<tr><td>State enc M1</td><td>" + String((data[3] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[3] & 0x02)
					html += "<tr><td>State enc M2</td><td>" + String((data[3] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[3] & 0x04)
					html += "<tr><td>Input enc M1</td><td>" + String((data[3] & 0x04) ? "On" : "Off") + "</td></tr>";
				if (info[3] & 0x08)
					html += "<tr><td>Input enc M2</td><td>" + String((data[3] & 0x08) ? "On" : "Off") + "</td></tr>";

				if (info[4] & 0x01)
					html += "<tr><td>Output M1</td><td>" + String((data[4] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[4] & 0x02)
					html += "<tr><td>Output M2</td><td>" + String((data[4] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[4] & 0x04)
					html += "<tr><td>Output 1</td><td>" + String((data[4] & 0x04) ? "On" : "Off") + "</td></tr>";
				if (info[4] & 0x08)
					html += "<tr><td>Output 2</td><td>" + String((data[4] & 0x08) ? "On" : "Off") + "</td></tr>";
				if (info[4] & 0x10)
					html += "<tr><td>Output 3</td><td>" + String((data[4] & 0x10) ? "On" : "Off") + "</td></tr>";
				if (info[4] & 0x20)
					html += "<tr><td>Output fan</td><td>" + String((data[4] & 0x20) ? "On" : "Off") + "</td></tr>";
				if (info[4] & 0x40)
					html += "<tr><td>Green light signal</td><td>" + String((data[4] & 0x40) ? "On" : "Off") + "</td></tr>";
				if (info[4] & 0x80)
					html += "<tr><td>Red light signal</td><td>" + String((data[4] & 0x80) ? "On" : "Off") + "</td></tr>";

				if (info[5] == 0xFF)
				{
					html += "<tr><td>State halt</td><td>";
					switch (data[5])
					{
						case 0:
							html += "Not set";
							break;
						case 1:
							html += "B1";
							break;
						case 2:
							html += "B2";
							break;
						case 3:
							html += "NC";
							break;
						case 4:
							html += "NO";
							break;
						case 5:
							html += "Out of range";
							break;
						case 6:
							html += "Border OSE";
							break;
						default:
							html += "-";
							break;
					}
					html += "</td></tr>";
				}

				if (info[6] & 0x01)
					html += "<tr><td>Input radio 1</td><td>" + String((data[6] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[6] & 0x02)
					html += "<tr><td>Input radio 2</td><td>" + String((data[6] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[6] & 0x04)
					html += "<tr><td>Input radio 3</td><td>" + String((data[6] & 0x04) ? "On" : "Off") + "</td></tr>";
				if (info[6] & 0x08)
					html += "<tr><td>Input radio 4</td><td>" + String((data[6] & 0x08) ? "On" : "Off") + "</td></tr>";

				if (info[7] & 0x01)
					html += "<tr><td>Input T4 mode 1/1</td><td>" + String((data[7] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[7] & 0x02)
					html += "<tr><td>Input T4 mode 1/2</td><td>" + String((data[7] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[7] & 0x04)
					html += "<tr><td>Input T4 mode 1/3</td><td>" + String((data[7] & 0x04) ? "On" : "Off") + "</td></tr>";
				if (info[7] & 0x08)
					html += "<tr><td>Input T4 mode 1/4</td><td>" + String((data[7] & 0x08) ? "On" : "Off") + "</td></tr>";

				if (info[8] == 0xFF)
					html += "<tr><td>Input T4 mode 2</td><td>" + String(data[8]) + "</td></tr>";

				if (info[9] & 0x01)
					html += "<tr><td>Thermal</td><td>" + String((data[9] & 0x01) ? "On" : "Off") + "</td></tr>";
				if (info[9] & 0x02)
					html += "<tr><td>Heating</td><td>" + String((data[9] & 0x02) ? "On" : "Off") + "</td></tr>";
				if (info[9] & 0x04)
					html += "<tr><td>Stand-by</td><td>" + String((data[9] & 0x04) ? "On" : "Off") + "</td></tr>";
				if (info[9] & 0x08)
					html += "<tr><td>Battery</td><td>" + String((data[9] & 0x08) ? "On" : "Off") + "</td></tr>";
				if (info[9] & 0x10)
					html += "<tr><td>Power supply requency</td><td>" + String((data[9] & 0x10) ? "60 Hz" : "50 Hz") + "</td></tr>";
				if (info[9] & 0x20)
					html += "<tr><td>Automatic opening</td><td>" + String((data[9] & 0x20) ? "On" : "Off") + "</td></tr>";

				if (info[10] & 0x01)
					html += "<tr><td>Error positions</td><td>" + String((data[10] & 0x01) ? "KO" : "OK") + "</td></tr>";
				if (info[10] & 0x02)
					html += "<tr><td>Error BlueBus</td><td>" + String((data[10] & 0x02) ? "KO" : "OK") + "</td></tr>";
				if (info[10] & 0x04)
					html += "<tr><td>Error halt</td><td>" + String((data[10] & 0x04) ? "KO" : "OK") + "</td></tr>";
				if (info[10] & 0x08)
					html += "<tr><td>Error function</td><td>" + String((data[10] & 0x08) ? "KO" : "OK") + "</td></tr>";
				if (info[10] & 0x10)
					html += "<tr><td>Error regulations</td><td>" + String((data[10] & 0x10) ? "KO" : "OK") + "</td></tr>";
				if (info[10] & 0x20)
					html += "<tr><td>Error map 1</td><td>" + String((data[10] & 0x20) ? "KO" : "OK") + "</td></tr>";
				if (info[10] & 0x40)
					html += "<tr><td>Error map 2</td><td>" + String((data[10] & 0x40) ? "KO" : "OK") + "</td></tr>";

				if (info[11] == 0xFF)
				{
					html += "<tr><td>State manoeuvre limiter</td><td>";
					switch (data[11])
					{
						case 0:
							html += "OK";
							break;
						case 1:
							html += "Threshold 1";
							break;
						case 2:
							html += "Threshold 2";
							break;
						case 3:
							html += "Alarm engine";
							break;
						default:
							html += "-";
							break;
					}
					html += "</td></tr>";
				}

				if (info[12] & 0x01)
					html += "<tr><td>Overload output 1</td><td>" + String((data[12] & 0x01) ? "OK" : "KO") + "</td></tr>";
				if (info[12] & 0x02)
					html += "<tr><td>Overload output 2</td><td>" + String((data[12] & 0x02) ? "OK" : "KO") + "</td></tr>";
				if (info[12] & 0x04)
					html += "<tr><td>Overload output 3</td><td>" + String((data[12] & 0x04) ? "OK" : "KO") + "</td></tr>";
				if (info[12] & 0x10)
					html += "<tr><td>Overtravel low enc M1</td><td>" + String((data[12] & 0x10) ? "On" : "Off") + "</td></tr>";
				if (info[12] & 0x20)
					html += "<tr><td>Overtravel high enc M1</td><td>" + String((data[12] & 0x20) ? "On" : "Off") + "</td></tr>";
				if (info[12] & 0x40)
					html += "<tr><td>Overtravel low enc M2</td><td>" + String((data[12] & 0x40) ? "On" : "Off") + "</td></tr>";
				if (info[12] & 0x80)
					html += "<tr><td>Overtravel high enc M2</td><td>" + String((data[12] & 0x80) ? "On" : "Off") + "</td></tr>";

				if ((reply.header.messageSize - 6) >= 14)
				{
					if (info[14] & 0x01)
						html += "<tr><td>Input 4</td><td>" + String((data[14] & 0x01) ? "On" : "Off") + "</td></tr>";
					if (info[14] & 0x02)
						html += "<tr><td>Input 5</td><td>" + String((data[14] & 0x02) ? "On" : "Off") + "</td></tr>";
					if (info[14] & 0x04)
						html += "<tr><td>Input 6</td><td>" + String((data[14] & 0x04) ? "On" : "Off") + "</td></tr>";
					if (info[15] & 0x01)
						html += "<tr><td>Output 4</td><td>" + String((data[15] & 0x01) ? "On" : "Off") + "</td></tr>";
					if (info[15] & 0x02)
						html += "<tr><td>Output 5</td><td>" + String((data[15] & 0x02) ? "On" : "Off") + "</td></tr>";
					if (info[15] & 0x04)
						html += "<tr><td>Output 6</td><td>" + String((data[15] & 0x04) ? "On" : "Off") + "</td></tr>";
				}

				html += "</table>\n";
				break;
			}

			case 0xE2:
			{
				html += "<h1>Diagnostics / Hardware</h1>";

				const auto data = (uint16_t*)reply.message.dmp.data;
				const auto info = (uint16_t*)&command_info[5];

				html += "<table>\n";

				if (info[0] & 0x0080)
					html += "<tr><td>Work time</td><td>" + String(std::byteswap(data[0])) + " s</td></tr>";
				if (info[1] & 0x0080)
					html += "<tr><td>Pause time</td><td>" + String(std::byteswap(data[1])) + " s</td></tr>";
				if (info[2] & 0x0080)
					html += "<tr><td>Courtesy light</td><td>" + String(std::byteswap(data[2])) + " s</td></tr>";
				if (info[3] & 0x0080)
					html += "<tr><td>Bus average current</td><td>" + String(std::byteswap(data[3])) + " %</td></tr>";
				if (info[4] & 0x0080)
					html += "<tr><td>Service voltage</td><td>" + String(std::byteswap(data[4])) + " V</td></tr>";
				if (info[5] & 0x0080)
					html += "<tr><td>Torque M1</td><td>" + String(std::byteswap(data[5])) + " %</td></tr>";
				if (info[6] & 0x0080)
					html += "<tr><td>Torque M2</td><td>" + String(std::byteswap(data[6])) + " %</td></tr>";
				if (info[7] & 0x0080)
					html += "<tr><td>Temperature</td><td>" + String(std::byteswap(data[7])) + " &deg;C</td></tr>";
				if (info[8] & 0x0080)
					html += "<tr><td>Voltage M1</td><td>" + String(std::byteswap(data[8])) + " V</td></tr>";
				if (info[9] & 0x0080)
					html += "<tr><td>Voltage M2</td><td>" + String(std::byteswap(data[9])) + " V</td></tr>";
				if (info[10] & 0x0080)
					html += "<tr><td>Speed M1</td><td>" + String(std::byteswap(data[10])) + " %</td></tr>";
				if (info[11] & 0x0080)
					html += "<tr><td>Speed M2</td><td>" + String(std::byteswap(data[11])) + " %</td></tr>";

				html += "</table>\n";
				break;
			}

			default:
				html += "Not supported";
				break;
		}

		html += "<br/><a href=\"/configure?root=246\">&Ll; Back</a><br/>";
		html += footer();

		web_server.send(200, "text/html", html);
	}
	else
	{
		web_server.send(500, "text/plain", "Error");
	}

	t4.unlockUnit();
}

void web_log()
{
	authenticate();

	if (!t4.lockUnit())
		return web_server.send(500, "text/plain", "Error");

	auto& unit = t4.getUnit();

	// CTRL_LOG_8_MANEUVERS(0xDA)
	T4Packet reply;
	uint8_t message[5] = { CONTROLLER, 0xDA, REQ|GET|ACK|FIN, 0x00, 0x00 };
	bool reply_ok = t4.sendRequest(0x55, unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply, 3);

	t4.unlockUnit();

	if (reply_ok)
	{
		String html = header("Log");

		html += "<h1>Manoeuvers log</h1>";

		for (size_t n = 0; n < 8; ++n)
		{
			auto log = reply.message.dmp.data[n];
			if (log < std::size(T4ManoeuvreStatusStrings))
				html += String(T4ManoeuvreStatusStrings[log]);
			else
				html += "UNKNOWN(" + String(log) + ")";
			html += "<br/>";
		}

		html += "<br/><a href=\"/\">&Ll; Back</a><br/>";
		html += footer();

		web_server.send(200, "text/html", html);
	}
	else
	{
		web_server.send(500, "text/plain", "Error");
	}
}

void web_status()
{
	authenticate();

	if (!t4.lockUnit())
		return web_server.send(500, "text/plain", "Error");

	auto& unit = t4.getUnit();

	// CTRL_AUTOMATION_STATUS(0x01)
	T4Packet reply;
	uint8_t message[5] = { CONTROLLER, 0x01, REQ|GET|ACK|FIN, 0x00, 0x00 };
	bool reply_ok = t4.sendRequest(0x55, unit.source, T4ThisAddress, DMP, message, sizeof(message), &reply, 3);

	t4.unlockUnit();

	if (reply_ok)
	{
		String html = header("Status");

		html += "<h1>Status</h1>\n";

		html += "<table>\n";

		auto status = reply.message.dmp.data[0];
		auto flags = reply.message.dmp.data[1];
		auto log = reply.message.dmp.data[2];

		if (status < std::size(T4AutomationStatusStrings) && T4AutomationStatusStrings[status])
			html += "<tr><td>Automation status</td><td>" + String(T4AutomationStatusStrings[status]) + "</td></tr>";

		html += "<tr><td>Last manoeuvre status</td><td>";
		if (log < std::size(T4ManoeuvreStatusStrings))
			html += String(T4ManoeuvreStatusStrings[log]);
		else
			html += "UNKNOWN(" + String(log) + ")";
		html += "</td></tr>";

		html += "<tr><td>Devices search</td><td>" + String(flags & 0x01 ? "Not in progress" : "In progress") + "</td></tr>";
		html += "<tr><td>Posititons search</td><td>" + String(flags & 0x02 ? "Not in progress" : "In progress") + "</td></tr>";
		html += "<tr><td>First learning manoeuvers</td><td>" + String(flags & 0x04 ? "Completed" : "Not completed") + "</td></tr>";
		html += "<tr><td>Configuration</td><td>" + String(flags & 0x08 ? "Not in progress" : "In progress") + "</td></tr>";
		html += "<tr><td>EEPROM errors</td><td>" + String(flags & 0x10 ? "No errors found" : "Errors found") + "</td></tr>";
		html += "</table>\n";

		html += "<br/><a href=\"/\">&Ll; Back</a><br/>";
		html += footer();

		web_server.send(200, "text/html", html);
	}
	else
	{
		web_server.send(500, "text/plain", "Error");
	}
}

void web_execute()
{
	authenticate();

	if (!t4.lockUnit())
		return web_server.send(500, "text/plain", "Error");

	auto& unit = t4.getUnit();

	// send DEP packet to execute the command
	uint8_t message[4] = { OVIEW, 0x82, uint8_t(web_server.arg("command").toInt()), 100 };
	T4Packet packet(0x55, unit.source, T4ThisAddress, 1, message, sizeof(message));
	t4.send(packet);

	t4.unlockUnit();

	web_server.sendHeader("Location", "/");
	web_server.send(303, "text/plain", "Redirect");
}

void webServerInit()
{
	web_server.on("/", web_root);
	web_server.on("/configure", HTTP_GET, web_configure_get);
	web_server.on("/configure", HTTP_POST, web_configure_post);
	web_server.on("/diagnostics", web_diagnostics);
	web_server.on("/log", web_log);
	web_server.on("/status", web_status);
	web_server.on("/execute", web_execute);
	web_server.begin();
}

void webServerHandle()
{
	web_server.handleClient();
}
