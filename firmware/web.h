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

#ifndef WEB_H
#define WEB_H

#include <Arduino.h>
#include <WebServer.h>

void webServerInit();
void webServerHandle();

#endif
