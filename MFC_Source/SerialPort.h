#pragma once

#include <Windows.h>
#include <afxstr.h>

class CSerialPort
{
public:
    CSerialPort() = default;
    ~CSerialPort();

    bool Open(
        const CString& portName,
        DWORD baudRate = CBR_115200);

    void Close();
    bool IsOpen() const;
    bool ReadAvailable(CStringA& receivedBytes);
    bool WriteLine(const CString& line);

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};
