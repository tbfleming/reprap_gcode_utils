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

#include <functional>
#include <list>
#include <string>
#include <tuple>
#include <vector>
#include <Windows.h>

// Identifies function to call whenever an event is signaled; function may throw exceptions
typedef std::tuple<HANDLE, std::function<void()>> Event;

// Line handling function
typedef std::function<void(const char* begin, const char* end)> LineHandler;

// Receive status messages (warnings)
typedef std::function<void(const char* msg)> StatusWriter;

class Serial
{
private:
    LineHandler receivedLine;                   // This function is called for every received line
    std::vector<Event> events;                  // Events needed by this class
    StatusWriter statusWriter;                  // This function is called to indicate warnings

    HANDLE handle;                              // Serial handle
    OVERLAPPED overlappedCommState;             // Async WaitCommEvent()
    OVERLAPPED overlappedRead;                  // Async ReadFile()
    OVERLAPPED overlappedWrite;                 // Async WriteFile()
    DWORD commEventMask;                        // Filled by async WaitCommEvent()
    bool fullyOpened;                           // open() is finished
    std::list<std::string> writeQueue;          // Strings to send
    bool writing;                               // Front of writeQueue is being sent
    static const size_t readBufferSize = 1024;  // Maximum size of a received line
    char readBuffer[readBufferSize];            // Receives incoming data
    size_t bytesInReadBuffer;                   // Amount of data in readBuffer

public:
    Serial(LineHandler receivedLine, StatusWriter statusWriter);
    ~Serial();

public:
    // Open port; throws exception on failure
    void open(
        const std::string& port,                // e.g. COM2
        unsigned bps);                          // Speed (bps), *NOT* BAUD_n define!

    // Close port
    void close();

    // Send data. Async; returns immediately
    void send(std::string&& data);

    // Get events for event loop
    const std::vector<Event>& getEvents() {return events;}

private:
    // Clean up internal state
    void cleanup();

    // Signaled when WaitCommEvent() is done
    void onCommState();

    // Signaled when WriteFile() is done
    void onWrite();

    // Process WaitCommEvent() result
    void processCommState();

    // Send next string in writeQueue
    void startWrite();
};
