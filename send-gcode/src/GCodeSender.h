// send-gcode - sends gcode commands to RepRap 5D firmware
// Copyright 2010  Todd Fleming
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA. 

#include "Serial.h"

std::string toString(unsigned n);

class GCodeSender
{
private:
    Serial serial;                  // Serial port
    bool verbose;                   // Print communications traffic
    const char* pos;                // Current position
    const char* contentEnd;         // End of content
    const char* lastSent;           // Position of last line sent
    unsigned lastChecksumLine;      // Last line number used for checksum
    bool sentM110;                  // Has M110 (set line number) been sent?
    bool done;                      // Last line has been sent and acknowledged

public:
    GCodeSender(
        const char* port,           // e.g. "COM2"
        unsigned bps,               // Speed (bps), *NOT* BAUD_n define!
        const char* content,        // Content; caller must keep this alive
        const char* contentEnd,     // End of content
        bool verbose);              // Print communications traffic

    // Get events for event loop
    const std::vector<Event>& getEvents() {return serial.getEvents();}

    // Has the last line been sent and acknowledged?
    bool getDone() {return done;}

private:
    // Send next line
    void send();

    // Received line
    void receiveLine(const char* b, const char* e);
};
