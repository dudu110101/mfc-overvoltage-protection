#pragma once

class CArduinoMonitorApp : public CWinApp
{
public:
    BOOL InitInstance() override;
};

extern CArduinoMonitorApp theApp;
