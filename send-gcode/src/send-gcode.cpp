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
#include "tclap\CmdLine.h"

using namespace std;

const char* copyright =
    "Copyright 2010  Todd Fleming\n"
    "\n"
    "This program is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU General Public License\n"
    "as published by the Free Software Foundation; either version 2\n"
    "of the License, or (at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n";

const string version = "0.1";
const string defaultPort = "COM4";
const unsigned defaultBps = 19200;

void readFile(const string& filename, shared_ptr<char>& content, size_t& size)
{
    shared_ptr<FILE> f(fopen(filename.c_str(), "rb"), [](FILE* f){if(f) fclose(f);});
    if(!f)
        throw runtime_error("can not open file " + filename);

    fseek(&*f, 0, SEEK_END);
    size = (size_t)ftell(&*f);
    fseek(&*f, 0, SEEK_SET);

    content = shared_ptr<char>(new char[size], [](char* p){delete[] p;});
    if(fread(&*content, 1, size, &*f) != size)
        throw runtime_error("can not read " + filename);
}

class StdOutput: public TCLAP::StdOutput
{
    virtual void version(TCLAP::CmdLineInterface& c);
};

void StdOutput::version(TCLAP::CmdLineInterface& c)
{
    printf("\n%s version: %s\n", c.getProgramName().c_str(), c.getVersion().c_str());
    printf("%s", copyright);
}

int main(int argc, char* argv[])
{
    try
    {
        TCLAP::CmdLine cmd("send-gcode - sends gcode commands to RepRap 5D firmware", ' ', version);
        StdOutput stdOutput;
        cmd.setOutput(&stdOutput);

        TCLAP::SwitchArg verboseArg("v","verbose","Print communications traffic", cmd, false);
        TCLAP::ValueArg<unsigned> bpsArg("b", "bps", "Serial port speed; defaults to " + toString(defaultBps), false, defaultBps, "bps", cmd);
        TCLAP::ValueArg<string> portArg("p", "port", "Serial port to use; defaults to " + defaultPort, false, defaultPort, "port", cmd);
        TCLAP::ValueArg<string> fileArg("f", "file", "File to send", true, "", "file", cmd);
        cmd.parse(argc, argv);

        shared_ptr<char> content;
        size_t size;
        readFile(fileArg.getValue(), content, size);

        GCodeSender sender(portArg.getValue().c_str(), bpsArg.getValue(), &*content, &*content + size, verboseArg.getValue());

        const std::vector<Event>& events = sender.getEvents();
        shared_ptr<HANDLE> eventHandles(new HANDLE[events.size()], [](HANDLE* p){delete[] p;});
        for(size_t i = 0; i < events.size(); ++i)
            (&*eventHandles)[i] = get<0>(events[i]);

        while(!sender.getDone())
        {
            DWORD result = WaitForMultipleObjectsEx(events.size(), &*eventHandles, false, INFINITE, true);
            if(result == WAIT_FAILED)
                throw runtime_error("wait failed");
            else if(result < events.size())
                get<1>(events[result])();
        }

        return 0;
    }
    catch(TCLAP::ArgException &e)
    {
        printf("error: %s for arg %s\n", e.error(), e.argId());
        return 1;
    }
    catch(exception& e)
    {
        printf("error: %s\n", e.what());
        return 1;
    }
}
