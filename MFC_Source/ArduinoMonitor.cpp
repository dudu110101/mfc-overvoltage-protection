#include "pch.h"
#include "ArduinoMonitor.h"
#include "ArduinoMonitorDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CArduinoMonitorApp theApp;

BOOL CArduinoMonitorApp::InitInstance()
{
    CWinApp::InitInstance();

    CArduinoMonitorDlg dialog;
    m_pMainWnd = &dialog;
    dialog.DoModal();

    return FALSE;
}
