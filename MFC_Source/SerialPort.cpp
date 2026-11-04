#include "pch.h"
#include "SerialPort.h"
#include <atlconv.h>

CSerialPort::~CSerialPort()
{
    Close();
}

bool CSerialPort::Open(
    const CString& portName,
    DWORD baudRate)
{
    Close();

    CString devicePath;
    devicePath.Format(
        _T("\\\\.\\%s"),
        portName.GetString());

    m_handle = CreateFile(
        devicePath,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (m_handle == INVALID_HANDLE_VALUE)
        return false;

    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(m_handle, &dcb))
    {
        Close();
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;

    if (!SetCommState(m_handle, &dcb))
    {
        Close();
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(m_handle, &timeouts))
    {
        Close();
        return false;
    }

    SetupComm(m_handle, 4096, 4096);
    PurgeComm(
        m_handle,
        PURGE_RXCLEAR | PURGE_TXCLEAR);

    return true;
}

void CSerialPort::Close()
{
    if (m_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

bool CSerialPort::IsOpen() const
{
    return m_handle != INVALID_HANDLE_VALUE;
}

bool CSerialPort::ReadAvailable(CStringA& receivedBytes)
{
    receivedBytes.Empty();

    if (!IsOpen())
        return false;

    COMSTAT status = {};
    DWORD errors = 0;

    if (!ClearCommError(m_handle, &errors, &status))
        return false;

    while (status.cbInQue > 0)
    {
        char buffer[257] = {};
        const DWORD requested = status.cbInQue < 256 ? status.cbInQue : 256;
        DWORD bytesRead = 0;

        if (!ReadFile(
                m_handle,
                buffer,
                requested,
                &bytesRead,
                nullptr))
        {
            return false;
        }

        if (bytesRead == 0)
            break;

        receivedBytes.Append(buffer, static_cast<int>(bytesRead));

        if (!ClearCommError(m_handle, &errors, &status))
            return false;
    }

    return true;
}

bool CSerialPort::WriteLine(const CString& line)
{
    if (!IsOpen())
        return false;

    CW2A converted(line, CP_UTF8);
    const char* bytes = converted;
    const DWORD length = static_cast<DWORD>(strlen(bytes));
    DWORD bytesWritten = 0;

    return WriteFile(
               m_handle,
               bytes,
               length,
               &bytesWritten,
               nullptr) != FALSE &&
           bytesWritten == length;
}
