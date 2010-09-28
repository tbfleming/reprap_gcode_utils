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

#include "GCodeSender.h"

#include <algorithm>
#include <stdint.h>

using namespace std;

std::string toString(unsigned n)
{
    if(!n)
        return "0";

    string s;
    while(n)
    {
        s = string(1, char('0' + n%10)) + s;
        n /= 10;
    }
    return s;
}

GCodeSender::GCodeSender(
    const char* port,
    unsigned bps,
    const char* content,
    const char* contentEnd,
    bool verbose):
        serial(
            [this](const char* b, const char* e){receiveLine(b, e);},
            [](const char* s){printf("%s", s);}),
        verbose(verbose),
        pos(content),
        contentEnd(contentEnd),
        lastSent(content),
        lastChecksumLine(0),
        sentM110(false),
        done(false)
{
    serial.open(port, bps);
    send();
}

void GCodeSender::send()
{
    if(!sentM110)
    {
        string s = "N" + toString(++lastChecksumLine) + " M110";
        uint8_t cs = 0;
        for_each(s.begin(), s.end(), [&cs](char ch){
            cs = cs ^ ch;});
        s += "*" + toString(cs) + "\n";
        if(verbose)
            printf("send: %s", s.c_str());
        serial.send(move(s));
        sentM110 = true;
        return;
    }

    bool sentLine = false;
    while(pos != contentEnd && !sentLine)
    {
        while(pos != contentEnd && isspace((unsigned char)*pos))
            ++pos;
        const char* e = pos;
        while(e != contentEnd && *e != '\r' && *e != '\n' && *e != '(' && *e != ';')
            ++e;
        if(pos != e)
        {
            lastSent = pos;
            string s = "N" + toString(++lastChecksumLine) + " " + string(pos, e);
            uint8_t cs = 0;
            for_each(s.begin(), s.end(), [&cs](char ch){
                cs = cs ^ ch;});
            s += "*" + toString(cs) + "\n";
            if(verbose)
                printf("send: %s", s.c_str());
            serial.send(move(s));
            sentLine = true;
        }
        while(e != contentEnd && *e != '\r' && *e != '\n')
            ++e;
        pos = e;
    }
    if(!sentLine)
        done = true;
}

void GCodeSender::receiveLine(const char* b, const char* e)
{
    if(verbose)
        printf("recv: %s\n", string(b, e).c_str());
    if(e-b == 5 && !strncmp(b, "start", 5))
    {
        pos = lastSent;
        sentM110 = false;
        lastChecksumLine += 20;
        send();
    }
    else if(e-b >= 6 && !strncmp(b, "Resend", 6))
    {
        pos = lastSent;
        --lastChecksumLine;
    }
    else if(e-b >= 2 && !strncmp(b, "ok", 2))
        send();
}
