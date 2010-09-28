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
#include <stdexcept>

using namespace std;

Serial::Serial(LineHandler receivedLine, StatusWriter statusWriter):
    receivedLine(receivedLine),
    statusWriter(statusWriter),
    handle(INVALID_HANDLE_VALUE),
    fullyOpened(false),
    writing(false),
    bytesInReadBuffer(0)
{
    memset(&overlappedCommState, 0, sizeof(OVERLAPPED));
    memset(&overlappedRead, 0, sizeof(OVERLAPPED));
    memset(&overlappedWrite, 0, sizeof(OVERLAPPED));

    try
    {
        overlappedCommState.hEvent = CreateEvent(0, true, false, 0);
        overlappedRead.hEvent = CreateEvent(0, true, false, 0);
        overlappedWrite.hEvent = CreateEvent(0, true, false, 0);

        if(!overlappedCommState.hEvent || !overlappedRead.hEvent || !overlappedWrite.hEvent)
            throw runtime_error("CreateEvent failed");

        // Overlapped WaitCommEvent() and WriteFile() run in the background.
        // We only call overlapped ReadFile() when there is something in the hardware buffer;
        // it doesn't run in the background. If we did call ReadFile() when the hardware
        // buffer is empty, it wouldn't complete until the buffer we supply is completely filled (nasty).
        events.push_back(Event(overlappedCommState.hEvent, [this](){onCommState();}));
        events.push_back(Event(overlappedWrite.hEvent, [this](){onWrite();}));
    }
    catch(...)
    {
        if(overlappedCommState.hEvent)
            CloseHandle(overlappedCommState.hEvent);
        if(overlappedRead.hEvent)
            CloseHandle(overlappedRead.hEvent);
        if(overlappedWrite.hEvent)
            CloseHandle(overlappedWrite.hEvent);
        throw;
    }
} // Serial::Serial

Serial::~Serial()
{
    close();
    CloseHandle(overlappedCommState.hEvent);
    CloseHandle(overlappedRead.hEvent);
    CloseHandle(overlappedWrite.hEvent);
}

void Serial::open(const std::string& port, unsigned bps)
{
    if(handle != INVALID_HANDLE_VALUE)
        throw runtime_error("connection is already open");

    cleanup();

    handle = CreateFileA(port.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if(handle == INVALID_HANDLE_VALUE)
        throw runtime_error("can not open port " + port);

    DCB dcb;
    memset(&dcb, 0, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);

    if(!GetCommState(handle, &dcb) || !PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR))
    {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        throw runtime_error("can not open port " + port);
    }

    dcb.BaudRate = bps;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = false;
    dcb.fOutxDsrFlow = false;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = false;
    dcb.fErrorChar = '?';
    dcb.fOutX = false;
    dcb.fInX = false;
    dcb.fNull = false;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = false;

    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if( !SetCommState(handle, &dcb) ||
        !SetCommMask(handle, EV_ERR | EV_RXCHAR) ||
        !SetCommTimeouts(handle, &timeouts))
    {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        throw runtime_error("can not open port " + port);
    }

    if(WaitCommEvent(handle, &commEventMask, &overlappedCommState))
    {
        try
        {
            processCommState();
        }
        catch(...)
        {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw;
        }
    }
    else if(GetLastError() != ERROR_IO_PENDING)
    {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        throw runtime_error("WaitCommEvent failed");
    }

    fullyOpened = true;
} // Serial::open

void Serial::close()
{
    if(!fullyOpened)
        return;

    CloseHandle(handle);
    handle = INVALID_HANDLE_VALUE;
    cleanup();
}

void Serial::send(std::string&& data)
{
    if(!fullyOpened)
        return;
    writeQueue.push_back(move(data));
    startWrite();
}

void Serial::cleanup()
{
    ResetEvent(overlappedCommState.hEvent);
    ResetEvent(overlappedRead.hEvent);
    ResetEvent(overlappedWrite.hEvent);

    fullyOpened = false;
    writing = false;
    writeQueue.clear();
    bytesInReadBuffer = 0;
}

void Serial::onCommState()
{
    if(handle == INVALID_HANDLE_VALUE)
        return;
    DWORD dummy;
    if(GetOverlappedResult(handle, &overlappedCommState, &dummy, true))
        processCommState();
    else
    {
        close();
        throw runtime_error("serial error");
    }
}

void Serial::onWrite()
{
    if(handle == INVALID_HANDLE_VALUE || !writing)
        return;
    DWORD numWritten = 0;
    if(!GetOverlappedResult(handle, &overlappedWrite, &numWritten, true) || numWritten != writeQueue.front().size())
    {
        close();
        throw runtime_error("serial write error");
    }
    else
    {
        ResetEvent(overlappedWrite.hEvent);
        writing = false;
        writeQueue.pop_front();
        startWrite();
    }
}

void Serial::processCommState()
{
    while(1)
    {
        if(commEventMask & EV_ERR)
        {
            DWORD errors;
            if(!ClearCommError(handle, &errors, 0))
                throw runtime_error("ClearCommError failed");
            if(errors & CE_BREAK)
                statusWriter("Received break\n");
            if(errors & CE_FRAME)
                statusWriter("Frame error\n");
            if(errors & CE_OVERRUN)
                statusWriter("Overrun\n");
            if(errors & CE_RXOVER)
                statusWriter("Input buffer overflow\n");
            if(errors & CE_RXPARITY)
                statusWriter("Parity error\n");
        }

        if(commEventMask & EV_RXCHAR)
        {
            // The hardware receive buffer has something in it. ReadFile sometimes returns an immediate success and sometimes
            // immediately schedules the completion routine. Either way, we can get the result now.
            DWORD numRead = 0;
            if(!ReadFile(handle, readBuffer + bytesInReadBuffer, readBufferSize - bytesInReadBuffer, &numRead, &overlappedRead))
            {
                DWORD err = GetLastError();
                if(err != ERROR_IO_PENDING || !GetOverlappedResult(handle, &overlappedRead, &numRead, true))
                    throw runtime_error("Serial read failed");
            }

            if(numRead)
            {
                bytesInReadBuffer += numRead;
                while(1)
                {
                    size_t p = 0;
                    while(p < bytesInReadBuffer && readBuffer[p] != '\r' && readBuffer[p] != '\n')
                        ++p;
                    if(p < bytesInReadBuffer)
                    {
                        if(p)
                            receivedLine(readBuffer, readBuffer + p);
                        while(p < bytesInReadBuffer && (readBuffer[p] == '\r' || readBuffer[p] == '\n'))
                            ++p;
                    
                        memmove(readBuffer, readBuffer+p, bytesInReadBuffer-p);
                        bytesInReadBuffer -= p;
                    }
                    else if(bytesInReadBuffer == readBufferSize)
                    {
                        statusWriter("buffer overfilled with garbage; dumping\n");
                        bytesInReadBuffer = 0;
                        break;
                    }
                    else
                        break;
                }
            }
        }

        if(WaitCommEvent(handle, &commEventMask, &overlappedCommState))
            ;                   // We have a new result; continue through loop
        else if(GetLastError() == ERROR_IO_PENDING)
            return;             // We are done until next event is signaled
        else
            throw runtime_error("WaitCommEvent failed");
    } // while(1)
} // Serial::processCommState

void Serial::startWrite()
{
    if(writing || writeQueue.empty())
        return;

    // I've never seen WriteFile return true for overlapped serial writes,
    // so I'll assume it's an error for now, despite the API docs.
    if(WriteFile(handle, writeQueue.front().c_str(), writeQueue.front().size(), 0, &overlappedWrite) || GetLastError() != ERROR_IO_PENDING)
    {
        close();
        throw runtime_error("Serial write error");
    }
    else
        writing = true;
}
