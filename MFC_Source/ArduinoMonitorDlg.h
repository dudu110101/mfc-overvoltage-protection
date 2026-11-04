#pragma once

#include "afxdialogex.h"
#include "resource.h"
#include "SerialPort.h"
#include <deque>

class CArduinoMonitorDlg : public CDialogEx
{
public:
    explicit CArduinoMonitorDlg(CWnd* parent = nullptr);

    enum { IDD = IDD_ARDUINOMONITOR_DIALOG };

protected:
    void DoDataExchange(CDataExchange* dataExchange) override;
    BOOL OnInitDialog() override;

    afx_msg void OnBnClickedRefresh();
    afx_msg void OnBnClickedConnect();
    afx_msg void OnBnClickedDisconnect();
    afx_msg void OnBnClickedApply();
    afx_msg void OnBnClickedLoadOn();
    afx_msg void OnBnClickedLoadOff();
    afx_msg void OnBnClickedReset();
    afx_msg void OnTimer(UINT_PTR eventId);
    afx_msg void OnDestroy();
    afx_msg void OnDrawItem(int controlId, LPDRAWITEMSTRUCT drawItem);
    afx_msg void OnPaint();
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT type);

    DECLARE_MESSAGE_MAP()

private:
    double m_testVoltage = 0.0;
    CString m_state = _T("DISCONNECTED");
    CString m_relayState = _T("OFF");
    CString m_tripCause = _T("NONE");
    CSerialPort m_serial;
    CStringA m_receiveBuffer;
    unsigned int m_requestId = 100;
    bool m_usbTestMode = false;
    bool m_pauseSupported = false;
    bool m_displayFrozen = false;
    ULONGLONG m_graphFrozenAt = 0;
    bool m_startPending = false;
    unsigned int m_startSettingsId = 0;
    ULONGLONG m_startRequestedAt = 0;
    CBrush m_backgroundBrush;
    CBrush m_cardBrush;
    CFont m_titleFont;
    CFont m_valueFont;
    struct VoltagePoint { ULONGLONG timeMs; double voltage; };
    std::deque<VoltagePoint> m_voltageHistory;

    bool ProcessReceivedLine(const CString& line);
    void UpdateDisplay();
    void RefreshPortList();
    void SetConnectedUi(bool connected);
    void AddEvent(const CString& message);
    bool SendCommand(const CString& commandBody);
    bool ApplyThresholds(bool startAfterAck);
};
