#include "pch.h"
#include "ArduinoMonitor.h"
#include "ArduinoMonitorDlg.h"
#include "afxdialogex.h"
#include <atlconv.h>
#include <cmath>
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    constexpr UINT_PTR SERIAL_TIMER_ID = 1;
    constexpr UINT SERIAL_TIMER_MS = 50;
    CString StateLabel(const CString& state)
    {
        if (state == _T("OFF")) return _T("정지");
        if (state == _T("NORMAL")) return _T("정상");
        if (state == _T("WARNING")) return _T("경고");
        if (state == _T("DANGER")) return _T("위험");
        if (state == _T("STARTING")) return _T("시작 준비 중");
        if (state == _T("DISCONNECTED")) return _T("연결 안 됨");
        return state;
    }
    CString CommandLabel(const CString& command)
    {
        if (command == _T("HELLO")) return _T("장치 확인");
        if (command == _T("LOAD")) return _T("시험 제어");
        if (command == _T("RESET")) return _T("초기화");
        if (command == _T("SET")) return _T("임계값 설정");
        return command;
    }
    CString ErrorLabel(const CString& error)
    {
        if (error == _T("TRIP_LATCHED")) return _T("차단 상태입니다. RESET 후 다시 시작하세요.");
        if (error == _T("INVALID_VALUE")) return _T("입력값이 허용 범위를 벗어났습니다.");
        if (error == _T("INVALID_RANGE")) return _T("0 < 경고 전압 < 차단 전압 ≤ 24V 조건을 확인하세요.");
        if (error == _T("MISSING_VALUE")) return _T("필요한 입력값이 없습니다.");
        if (error == _T("INVALID_FRAME")) return _T("명령 형식이 올바르지 않습니다.");
        if (error == _T("LINE_TOO_LONG")) return _T("명령이 너무 깁니다.");
        if (error == _T("UNKNOWN_COMMAND")) return _T("지원하지 않는 명령입니다.");
        return error;
    }
}

CArduinoMonitorDlg::CArduinoMonitorDlg(CWnd* parent)
    : CDialogEx(IDD_ARDUINOMONITOR_DIALOG, parent)
{
}

void CArduinoMonitorDlg::DoDataExchange(CDataExchange* dataExchange)
{
    CDialogEx::DoDataExchange(dataExchange);
}

BEGIN_MESSAGE_MAP(CArduinoMonitorDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_REFRESH, &CArduinoMonitorDlg::OnBnClickedRefresh)
    ON_BN_CLICKED(IDC_BTN_CONNECT, &CArduinoMonitorDlg::OnBnClickedConnect)
    ON_BN_CLICKED(IDC_BTN_DISCONNECT, &CArduinoMonitorDlg::OnBnClickedDisconnect)
    ON_BN_CLICKED(IDC_BTN_APPLY, &CArduinoMonitorDlg::OnBnClickedApply)
    ON_BN_CLICKED(IDC_BTN_LOAD_ON, &CArduinoMonitorDlg::OnBnClickedLoadOn)
    ON_BN_CLICKED(IDC_BTN_LOAD_OFF, &CArduinoMonitorDlg::OnBnClickedLoadOff)
    ON_BN_CLICKED(IDC_BTN_RESET, &CArduinoMonitorDlg::OnBnClickedReset)
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_WM_DRAWITEM()
    ON_WM_PAINT()
    ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CArduinoMonitorDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowText(_T("VOLTAGE STUDIO · 실시간 시험 대시보드"));
    m_backgroundBrush.CreateSolidBrush(RGB(255, 255, 255));
    m_cardBrush.CreateSolidBrush(RGB(255, 255, 255));
    LOGFONT titleFont = {};
    titleFont.lfHeight = -170;
    titleFont.lfWeight = FW_BOLD;
    _tcscpy_s(titleFont.lfFaceName, _T("맑은 고딕"));
    m_titleFont.CreatePointFontIndirect(&titleFont);
    m_valueFont.CreatePointFont(130, _T("맑은 고딕"));
    GetDlgItem(IDC_TITLE)->SetFont(&m_titleFont);
    for (int id : { IDC_STATIC_VOLTAGE_VALUE, IDC_STATIC_STATE_VALUE,
                    IDC_STATIC_RELAY_VALUE, IDC_STATIC_TRIP_VALUE })
        GetDlgItem(id)->SetFont(&m_valueFont);

    SetDlgItemText(IDC_EDIT_WARN_V, _T("10.0"));
    SetDlgItemText(IDC_EDIT_TRIP_V, _T("11.0"));

    RefreshPortList();
    SetConnectedUi(false);
    UpdateDisplay();
    AddEvent(_T("프로그램 시작 - Arduino 연결 대기 중"));

    SetTimer(SERIAL_TIMER_ID, SERIAL_TIMER_MS, nullptr);
    return TRUE;
}

void CArduinoMonitorDlg::RefreshPortList()
{
    auto* combo = static_cast<CComboBox*>(GetDlgItem(IDC_COMBO_PORT));
    if (combo == nullptr)
        return;

    CString previous;
    const int oldSelection = combo->GetCurSel();
    if (oldSelection != CB_ERR)
        combo->GetLBText(oldSelection, previous);

    combo->ResetContent();

    TCHAR deviceTarget[512] = {};
    for (int portNumber = 1; portNumber <= 256; ++portNumber)
    {
        CString portName;
        portName.Format(_T("COM%d"), portNumber);
        if (QueryDosDevice(portName, deviceTarget, _countof(deviceTarget)) != 0)
            combo->AddString(portName);
    }

    int selection = previous.IsEmpty() ? CB_ERR : combo->FindStringExact(-1, previous);
    if (selection == CB_ERR && combo->GetCount() > 0)
        selection = 0;
    combo->SetCurSel(selection);

    CString message;
    message.Format(_T("COM 포트 검색 완료: %d개"), combo->GetCount());
    AddEvent(message);
}

void CArduinoMonitorDlg::SetConnectedUi(bool connected)
{
    GetDlgItem(IDC_COMBO_PORT)->EnableWindow(!connected);
    GetDlgItem(IDC_BTN_REFRESH)->EnableWindow(!connected);
    GetDlgItem(IDC_BTN_CONNECT)->EnableWindow(!connected);
    GetDlgItem(IDC_BTN_DISCONNECT)->EnableWindow(connected);
    GetDlgItem(IDC_BTN_APPLY)->EnableWindow(connected);
    GetDlgItem(IDC_BTN_LOAD_ON)->EnableWindow(connected);
    GetDlgItem(IDC_BTN_LOAD_OFF)->EnableWindow(connected);
    GetDlgItem(IDC_BTN_RESET)->EnableWindow(connected);

    SetDlgItemText(
        IDC_STATIC_CONNECTION,
        connected ? _T("장치 응답 대기 중") : _T("연결 안 됨"));
}

void CArduinoMonitorDlg::AddEvent(const CString& message)
{
    auto* list = static_cast<CListBox*>(GetDlgItem(IDC_LIST_EVENT));
    if (list == nullptr)
        return;

    CString entry;
    entry.Format(_T("[%s] %s"), CTime::GetCurrentTime().Format(_T("%H:%M:%S")).GetString(), message.GetString());
    if (list->GetCount() >= 500) list->DeleteString(0);
    list->AddString(entry);
    list->SetCurSel(list->GetCount() - 1);
}

void CArduinoMonitorDlg::OnBnClickedRefresh()
{
    RefreshPortList();
}

void CArduinoMonitorDlg::OnBnClickedConnect()
{
    auto* combo = static_cast<CComboBox*>(GetDlgItem(IDC_COMBO_PORT));
    if (combo == nullptr || combo->GetCurSel() == CB_ERR)
    {
        AfxMessageBox(_T("연결할 COM 포트를 선택하세요."));
        return;
    }

    CString portName;
    combo->GetLBText(combo->GetCurSel(), portName);

    if (!m_serial.Open(portName))
    {
        CString error;
        error.Format(_T("%s 포트를 열 수 없습니다. Arduino IDE의 시리얼 모니터를 닫았는지 확인하세요."), portName.GetString());
        AfxMessageBox(error);
        AddEvent(error);
        return;
    }

    m_receiveBuffer.Empty();
    SetConnectedUi(true);
    m_voltageHistory.clear();
    m_usbTestMode = false;
    m_startPending = false;
    m_state = _T("DISCONNECTED");
    m_displayFrozen = false;
    m_pauseSupported = false;
    GetDlgItem(IDC_STATIC_GRAPH)->Invalidate(FALSE);

    CString eventText;
    eventText.Format(_T("%s 연결 성공 (115200bps)"), portName.GetString());
    AddEvent(eventText);
    SendCommand(_T("HELLO"));
}

void CArduinoMonitorDlg::OnBnClickedDisconnect()
{
    m_startPending = false;
    if (m_serial.IsOpen())
        SendCommand(m_pauseSupported ? _T("LOAD,DISABLE") : _T("LOAD,OFF"));

    m_serial.Close();
    m_receiveBuffer.Empty();
    SetConnectedUi(false);
    m_state = _T("DISCONNECTED");
    m_relayState = _T("OFF");
    UpdateDisplay();
    AddEvent(_T("연결 해제"));
}

bool CArduinoMonitorDlg::SendCommand(const CString& commandBody)
{
    if (!m_serial.IsOpen())
    {
        AddEvent(_T("명령 실패: Arduino가 연결되지 않았습니다."));
        return false;
    }

    CString frame;
    frame.Format(_T("CMD,%u,%s\n"), m_requestId++, commandBody.GetString());

    if (!m_serial.WriteLine(frame))
    {
        AddEvent(_T("명령 전송 실패"));
        return false;
    }

    CString log;
    if (commandBody == _T("HELLO")) log = _T("[요청] Arduino 응답 확인 중");
    else if (commandBody == _T("LOAD,ON")) log = _T("[요청] 시험 시작 — 장치 응답 대기");
    else if (commandBody == _T("LOAD,OFF")) log = m_pauseSupported
        ? _T("[요청] 일시정지 — 화면·그래프·릴레이 현재 상태 유지, 과전압 보호 계속")
        : _T("[요청] 시험 정지 — 장치 응답 대기");
    else if (commandBody == _T("LOAD,DISABLE")) log = _T("[요청] 연결 해제 전 릴레이 OFF");
    else if (commandBody == _T("RESET")) log = _T("[요청] 초기화 — 장치 응답 대기");
    else if (commandBody.Left(4) == _T("SET,")) {
        int pos = 4;
        CString warning = commandBody.Tokenize(_T(","), pos);
        CString trip = commandBody.Tokenize(_T(","), pos);
        log.Format(_T("[요청] 임계값 설정 — 경고 %s V / 차단 %s V"), warning.GetString(), trip.GetString());
    } else log = _T("[요청] ") + commandBody;
    AddEvent(log);
    return true;
}

void CArduinoMonitorDlg::OnBnClickedApply()
{
    ApplyThresholds(false);
}

bool CArduinoMonitorDlg::ApplyThresholds(bool startAfterAck)
{
    if (m_startPending) {
        AddEvent(_T("[대기] 시작 설정을 확인하고 있습니다. 중복 요청은 보내지 않습니다."));
        return false;
    }
    CString warningText;
    CString tripText;
    GetDlgItemText(IDC_EDIT_WARN_V, warningText);
    GetDlgItemText(IDC_EDIT_TRIP_V, tripText);

    LPTSTR warningEnd = nullptr;
    LPTSTR tripEnd = nullptr;
    const double warningVoltage = _tcstod(warningText, &warningEnd);
    const double tripVoltage = _tcstod(tripText, &tripEnd);

    if (warningEnd == warningText.GetString() || *warningEnd != _T('\0') ||
        tripEnd == tripText.GetString() || *tripEnd != _T('\0') ||
        !std::isfinite(warningVoltage) || !std::isfinite(tripVoltage) ||
        warningVoltage <= 0.0 || tripVoltage <= warningVoltage || tripVoltage > 24.0)
    {
        AfxMessageBox(_T("0 < 경고 전압 < 차단 전압 <= 24 조건으로 입력하세요."));
        return false;
    }

    CString body;
    body.Format(_T("SET,%.2f,%.2f"), warningVoltage, tripVoltage);
    m_startSettingsId = m_requestId;
    if (!SendCommand(body)) return false;
    m_startPending = startAfterAck;
    m_startRequestedAt = GetTickCount64();
    return true;
}

void CArduinoMonitorDlg::OnBnClickedLoadOn()
{
    ApplyThresholds(true);
}

void CArduinoMonitorDlg::OnBnClickedLoadOff()
{
    m_startPending = false;
    SendCommand(_T("LOAD,OFF"));
}

void CArduinoMonitorDlg::OnBnClickedReset()
{
    m_startPending = false;
    SendCommand(_T("RESET"));
}

void CArduinoMonitorDlg::OnTimer(UINT_PTR eventId)
{
    if (eventId != SERIAL_TIMER_ID || !m_serial.IsOpen())
    {
        CDialogEx::OnTimer(eventId);
        return;
    }

    CStringA received;
    if (m_startPending && GetTickCount64() - m_startRequestedAt > 3000) {
        m_startPending = false;
        AddEvent(_T("[시작 취소] 임계값 적용 응답이 없습니다. 연결 상태를 확인하세요."));
    }
    if (!m_serial.ReadAvailable(received))
    {
        AddEvent(_T("시리얼 수신 오류 - 연결을 종료합니다."));
        m_serial.Close();
        m_startPending = false;
        SetConnectedUi(false);
        CDialogEx::OnTimer(eventId);
        return;
    }

    if (!received.IsEmpty())
        m_receiveBuffer += received;

    GetDlgItem(IDC_STATIC_GRAPH)->Invalidate(FALSE);

    if (m_receiveBuffer.GetLength() > 1024)
    {
        m_receiveBuffer.Empty();
        AddEvent(_T("수신 버퍼 초과 - 데이터를 폐기했습니다."));
    }

    int newlinePosition = -1;
    while ((newlinePosition = m_receiveBuffer.Find('\n')) >= 0)
    {
        CStringA lineA = m_receiveBuffer.Left(newlinePosition);
        m_receiveBuffer.Delete(0, newlinePosition + 1);
        lineA.TrimRight("\r");

        if (lineA.IsEmpty())
            continue;

        CA2W convertedLine(lineA, CP_UTF8);
        ProcessReceivedLine(CString(convertedLine));
    }

    CDialogEx::OnTimer(eventId);
}

bool CArduinoMonitorDlg::ProcessReceivedLine(const CString& line)
{
    if (line.Left(5).CompareNoCase(_T("DATA,")) != 0)
    {
        int pos = 0;
        CString kind = line.Tokenize(_T(","), pos);
        if (kind == _T("HELLO")) {
            if (m_startPending) {
                m_startPending = false;
                AddEvent(_T("[시작 취소] 장치가 다시 응답했습니다. 입력값을 확인하고 다시 시작하세요."));
            }
            m_usbTestMode = line.Find(_T("mode=USB_TEST")) >= 0;
            m_pauseSupported = line.Find(_T("behavior=PAUSE_HOLD")) >= 0;
            m_displayFrozen = false;
            SetDlgItemText(IDC_STATIC_CONNECTION, _T("Arduino 연결 정상"));
            AddEvent(m_usbTestMode
                ? _T("[연결 완료] USB 테스트 모드 — 전압은 모의값, 릴레이는 구동하지 않습니다.")
                : line.Find(_T("mode=REAL_12V")) >= 0
                    ? _T("[연결 완료] 실제 측정 모드 — 시작 전 실시간 표시, 일시정지 지원")
                    : _T("[연결 완료] Arduino가 정상 응답했습니다."));
        } else if (kind == _T("CONFIG")) {
            CString warning = line.Tokenize(_T(","), pos);
            CString trip = line.Tokenize(_T(","), pos);
            CString message;
            message.Format(_T("[장치 설정] 경고 %s V / 차단 %s V"), warning.GetString(), trip.GetString());
            AddEvent(message);
            if (line.Find(_T("relay_active_low=1")) >= 0)
                AddEvent(_T("[릴레이 설정] LOW=켜짐 / HIGH=꺼짐. 실물이 반대로 움직이면 입력 극성을 확인하세요."));
            if (line.Find(_T("relay_active_low=0")) >= 0)
                AddEvent(_T("[릴레이 설정] HIGH=켜짐 / LOW=꺼짐. 실물이 반대로 움직이면 입력 극성을 확인하세요."));
        } else if (kind == _T("ACK") || kind == _T("ERR")) {
            CString id = line.Tokenize(_T(","), pos);
            CString command = line.Tokenize(_T(","), pos);
            CString result = line.Tokenize(_T(","), pos);
            CString detail;
            if (kind == _T("ERR")) detail = ErrorLabel(result);
            else if (command == _T("LOAD") && result == _T("ON"))
                detail = m_usbTestMode ? _T("LED·부저 데모 시작 완료") : _T("부하 ON 완료");
            else if (command == _T("LOAD") && result == _T("OFF"))
                detail = m_usbTestMode ? _T("LED·부저 데모 정지 완료") : _T("부하 OFF 완료");
            else if (command == _T("LOAD") && result == _T("PAUSED"))
                detail = _T("일시정지 완료 — 마지막 표시값 유지, 릴레이 현재 상태 유지, 보호 계속");
            else if (command == _T("RESET")) detail = _T("초기화 완료 — 정지 상태 유지");
            else if (command == _T("SET")) detail = _T("임계값 적용 완료");
            else detail = result;
            CString message;
            message.Format(_T("[%s] %s: %s (요청 %s)"), kind == _T("ERR") ? _T("실패") : _T("완료"),
                CommandLabel(command).GetString(), detail.GetString(), id.GetString());
            AddEvent(message);
            CString pendingId;
            pendingId.Format(_T("%u"), m_startSettingsId);
            if (m_startPending && id == pendingId && command == _T("SET")) {
                m_startPending = false;
                if (kind == _T("ACK") && result == _T("OK"))
                    SendCommand(_T("LOAD,ON"));
                else AddEvent(_T("[시작 취소] 임계값을 적용하지 못했습니다."));
            }
        } else if (kind == _T("EVENT")) {
            line.Tokenize(_T(","), pos); // Device time.
            CString event = line.Tokenize(_T(","), pos);
            CString detail = line.Tokenize(_T(","), pos);
            if (event == _T("ACQUISITION")) {
                const bool frozen = detail == _T("PAUSED");
                if (frozen && !m_displayFrozen) m_graphFrozenAt = GetTickCount64();
                m_displayFrozen = frozen;
                GetDlgItem(IDC_STATIC_GRAPH)->Invalidate(FALSE);
                SetDlgItemText(IDC_STATIC_CONNECTION, frozen ? _T("일시정지 · 보호 계속") :
                    detail == _T("PREVIEW") ? _T("시작 전 실시간 측정") :
                    detail == _T("RUNNING") ? _T("시험 중 · 실시간 측정") : _T("릴레이 OFF · 측정 중"));
                AddEvent(frozen ? _T("[일시정지] 전압·그래프 마지막 값 유지. 과전압 시 릴레이 차단은 계속 작동합니다.") :
                    detail == _T("PREVIEW") ? _T("[미리보기] 시작 버튼을 누르기 전에도 전압을 표시합니다.") :
                    detail == _T("RUNNING") ? _T("[실시간] 전압·그래프 갱신을 재개합니다.") :
                    _T("[릴레이 OFF] 실시간 측정은 계속합니다."));
            }
            else if (event == _T("RECOVER")) AddEvent(_T("[자동 복귀] 전압 하강 확인 — 릴레이 정상 상태로 복귀"));
            else if (event == _T("USB_TEST")) AddEvent(_T("[시험 안내] 센서 측정과 릴레이 제어 없이 LED·부저를 확인합니다."));
            else if (event == _T("TRIP") && detail == _T("OVERVOLTAGE"))
                AddEvent(_T("[차단] 과전압 감지 — 릴레이 OFF. 자동 복귀 여부는 센서 독립 공급 설정에 따릅니다."));
            else if (event == _T("RESET")) AddEvent(_T("[초기화] 차단 해제 — 릴레이는 꺼진 상태입니다. 재시험은 시험 시작을 누르세요."));
            else if (event == _T("TRIP_VOLTAGE")) {
                CString limit = line.Tokenize(_T(","), pos);
                CString message;
                message.Format(_T("[차단 전압] 측정 %s V ≥ 설정 %s V — 공급 차단 후 센서값은 0V가 될 수 있습니다."), detail.GetString(), limit.GetString());
                AddEvent(message);
            }
            else if (event == _T("RELAY_OUTPUT")) {
                AddEvent(_T("[출력 신호] D7 → ") + detail +
                    _T(" (명령 상태이며 실제 접점 확인값은 아닙니다)"));
            }
            else AddEvent(_T("[장치 이벤트] ") + event + _T(" / ") + detail);
        } else AddEvent(_T("[기타 장치 응답] ") + line);
        return true;
    }

    CString fields[8];
    int fieldCount = 0;
    int nextPosition = 0;

    // Split explicitly: Tokenize skips empty fields and its final cursor is
    // not guaranteed to be -1 immediately after returning the last token.
    while (fieldCount < 8)
    {
        const int commaPosition = line.Find(_T(','), nextPosition);
        const bool lastField = commaPosition < 0;
        fields[fieldCount++] = lastField
            ? line.Mid(nextPosition)
            : line.Mid(nextPosition, commaPosition - nextPosition);
        nextPosition = lastField ? -1 : commaPosition + 1;
        if (lastField)
            break;
    }

    bool emptyField = false;
    for (int i = 0; i < fieldCount; ++i)
        emptyField = emptyField || fields[i].IsEmpty();

    if (fieldCount != 8 || nextPosition != -1 || emptyField ||
        fields[0].CompareNoCase(_T("DATA")) != 0)
    {
        AddEvent(_T("[수신 오류] 전압 데이터 항목이 누락되었거나 형식이 다릅니다: ") + line);
        return false;
    }

    LPCTSTR voltageText = fields[3].GetString();
    LPTSTR endPosition = nullptr;
    const double voltage = _tcstod(voltageText, &endPosition);

    if (endPosition == voltageText || *endPosition != _T('\0') ||
        !std::isfinite(voltage) || voltage < 0.0)
    {
        AddEvent(_T("[수신 오류] 전압이 유효한 숫자가 아닙니다: ") + fields[3]);
        return false;
    }

    if (m_displayFrozen) {
        if (fields[4] == _T("DANGER") && (m_state != fields[4] || m_relayState != fields[5])) {
            CString message;
            message.Format(_T("[정지 중 보호] 실제 측정 %.2f V — 과전압 차단. 화면 전압은 정지 당시 값입니다."), voltage);
            AddEvent(message);
            m_state = fields[4]; m_relayState = fields[5]; m_tripCause = fields[6];
            UpdateDisplay();
        }
        return true;
    }
    m_testVoltage = voltage;
    const ULONGLONG receivedAt = GetTickCount64();
    // Keep live readings, but record the graph at 500 ms intervals.
    if (m_voltageHistory.empty() ||
        receivedAt - m_voltageHistory.back().timeMs >= 500)
        m_voltageHistory.push_back({ receivedAt, voltage });
    while (!m_voltageHistory.empty() &&
        receivedAt - m_voltageHistory.front().timeMs > 60000)
        m_voltageHistory.pop_front();
    GetDlgItem(IDC_STATIC_GRAPH)->Invalidate(FALSE);
    if (m_state != fields[4] || m_relayState != fields[5] || m_tripCause != fields[6]) {
        CString message;
        message.Format(_T("[%s] %s — 시험 전압 %.2f V / 릴레이 %s / %s"),
            m_usbTestMode ? _T("모의 상태") : _T("상태 변경"), StateLabel(fields[4]).GetString(), voltage,
            fields[5] == _T("ON") ? _T("켜짐") : _T("꺼짐"),
            fields[6] == _T("NONE") ? _T("차단 없음") : fields[6] == _T("OVERVOLTAGE") ? _T("과전압 차단") : fields[6].GetString());
        AddEvent(message);
    }
    m_state = fields[4];
    m_relayState = fields[5];
    m_tripCause = fields[6];
    UpdateDisplay();
    return true;
}

void CArduinoMonitorDlg::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(client);
    dc.FillSolidRect(client, RGB(255, 255, 255));
    CPen border(PS_SOLID, 1, RGB(213, 228, 250));
    CPen* oldPen = dc.SelectObject(&border);
    CBrush* oldBrush = dc.SelectObject(&m_cardBrush);
    const CRect cards[] = {
        CRect(16, 56, 584, 92), CRect(16, 98, 152, 158),
        CRect(162, 98, 298, 158), CRect(308, 98, 444, 158),
        CRect(454, 98, 584, 158), CRect(16, 166, 386, 342),
        CRect(396, 166, 584, 342), CRect(16, 348, 584, 444)
    };
    for (CRect card : cards) {
        MapDialogRect(card);
        dc.RoundRect(card, CPoint(30, 30));
    }
    dc.SelectObject(oldBrush);
    dc.SelectObject(oldPen);
}

HBRUSH CArduinoMonitorDlg::OnCtlColor(CDC* dc, CWnd* window, UINT type)
{
    HBRUSH base = CDialogEx::OnCtlColor(dc, window, type);
    dc->SetTextColor(RGB(43, 57, 77));
    if (type == CTLCOLOR_DLG) return m_backgroundBrush;
    if (type == CTLCOLOR_STATIC) {
        dc->SetBkMode(TRANSPARENT);
        const int id = window->GetDlgCtrlID();
        if (id == IDC_TITLE) dc->SetTextColor(RGB(37, 99, 235));
        CRect rect;
        window->GetWindowRect(rect); ScreenToClient(rect);
        CRect header(0, 0, 600, 54); MapDialogRect(header);
        if (id == IDC_STATIC_VOLTAGE_VALUE) dc->SetTextColor(RGB(37, 99, 235));
        if (id == IDC_STATIC_STATE_VALUE) dc->SetTextColor(
            m_state == _T("DANGER") ? RGB(220, 55, 65) :
            m_state == _T("WARNING") ? RGB(170, 105, 0) :
            m_state == _T("NORMAL") ? RGB(35, 125, 100) : RGB(90, 105, 125));
        if (id == IDC_STATIC_TRIP_VALUE && m_tripCause != _T("NONE"))
            dc->SetTextColor(RGB(220, 55, 65));
        return rect.top < header.bottom ? m_backgroundBrush : m_cardBrush;
    }
    if (type == CTLCOLOR_EDIT || type == CTLCOLOR_LISTBOX) {
        dc->SetBkColor(RGB(255, 255, 255));
        return m_cardBrush;
    }
    return base;
}

void CArduinoMonitorDlg::OnDrawItem(int controlId, LPDRAWITEMSTRUCT item)
{
    if (item->CtlType == ODT_BUTTON) {
        CDC dc;
        dc.Attach(item->hDC);
        CRect rect(item->rcItem);
        const bool enabled = !(item->itemState & ODS_DISABLED);
        const bool primary = controlId == IDC_BTN_CONNECT || controlId == IDC_BTN_LOAD_ON;
        COLORREF fill = !enabled ? RGB(241, 244, 248) : primary ? RGB(37, 99, 235) : RGB(235, 241, 249);
        if (enabled && (item->itemState & ODS_SELECTED)) fill = primary ? RGB(29, 78, 190) : RGB(216, 227, 242);
        dc.FillSolidRect(rect, RGB(255, 255, 255));
        CBrush brush(fill);
        CPen pen(PS_SOLID, 1, fill);
        auto oldBrush = dc.SelectObject(&brush);
        auto oldPen = dc.SelectObject(&pen);
        dc.RoundRect(rect, CPoint(rect.Height(), rect.Height()));
        auto oldFont = dc.SelectObject(GetFont());
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(!enabled ? RGB(150, 160, 175) : primary ? RGB(255, 255, 255) : RGB(43, 57, 77));
        CString text;
        GetDlgItem(controlId)->GetWindowText(text);
        dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) { rect.DeflateRect(4, 4); dc.DrawFocusRect(rect); }
        dc.SelectObject(oldFont); dc.SelectObject(oldPen); dc.SelectObject(oldBrush);
        dc.Detach();
        return;
    }
    if (controlId != IDC_STATIC_GRAPH)
    {
        CDialogEx::OnDrawItem(controlId, item);
        return;
    }
    CDC target;
    target.Attach(item->hDC);
    CRect bounds(item->rcItem);
    CDC dc;
    dc.CreateCompatibleDC(&target);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&target, bounds.Width(), bounds.Height());
    CBitmap* oldBitmap = dc.SelectObject(&bitmap);
    dc.FillSolidRect(bounds, RGB(255, 255, 255));
    CFont* oldFont = dc.SelectObject(GetFont());
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(65, 65, 65));
    CRect plot(bounds.left + 52, bounds.top + 22,
               bounds.right - 18, bounds.bottom - 34);
    const ULONGLONG now = m_displayFrozen ? m_graphFrozenAt : GetTickCount64();
    double maximum = 5.0;
    for (const auto& point : m_voltageHistory)
        if (now - point.timeMs <= 60000)
            maximum = (std::max)(maximum, point.voltage);
    const double topVoltage = std::ceil(maximum * 1.1 / 5.0) * 5.0;
    CPen grid(PS_SOLID, 1, RGB(225, 230, 235));
    CPen* oldPen = dc.SelectObject(&grid);
    for (int i = 0; i <= 5; ++i)
    {
        int y = plot.bottom - plot.Height() * i / 5;
        dc.MoveTo(plot.left, y); dc.LineTo(plot.right, y);
        CString label;
        label.Format(_T("%.0f"), topVoltage * i / 5);
        CRect text(bounds.left + 2, y - 8, plot.left - 6, y + 10);
        dc.DrawText(label, text, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
    for (int i = 0; i <= 6; ++i)
    {
        int x = plot.left + plot.Width() * i / 6;
        dc.MoveTo(x, plot.top); dc.LineTo(x, plot.bottom);
        CString label;
        label.Format(_T("%d"), -60 + i * 10);
        CRect text(x - 20, plot.bottom + 4, x + 20, plot.bottom + 21);
        dc.DrawText(label, text, DT_CENTER | DT_SINGLELINE);
    }
    dc.TextOut(bounds.left + 8, bounds.top + 3, _T("V"));
    CString title;
    title.Format(m_displayFrozen ? _T("일시정지 · 마지막 전압 %.2f V") : _T("시험 전압 %.2f V"), m_testVoltage);
    dc.TextOut(plot.left, bounds.top + 3, title);
    dc.TextOut(plot.right - 78, bounds.bottom - 15, _T("시간 (초)"));
    CPen curve(PS_SOLID, 2, RGB(0, 105, 205));
    dc.SelectObject(&curve);
    bool previous = false;
    ULONGLONG previousTime = 0;
    for (const auto& point : m_voltageHistory)
    {
        const ULONGLONG age = now - point.timeMs;
        if (age > 60000) continue;
        int x = plot.right - static_cast<int>(plot.Width() * age / 60000);
        int y = plot.bottom - static_cast<int>(plot.Height() * point.voltage / topVoltage);
        if (!previous || point.timeMs - previousTime > 2000)
        {
            dc.MoveTo(x, y);
            dc.SetPixel(x, y, RGB(0, 105, 205));
        }
        else dc.LineTo(x, y);
        previous = true;
        previousTime = point.timeMs;
    }
    if (!previous)
    {
        dc.SetTextColor(RGB(120, 120, 120));
        dc.DrawText(_T("시험 전압 데이터 수신 대기"), plot,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    dc.SelectObject(oldPen);
    dc.SelectObject(oldFont);
    target.BitBlt(bounds.left, bounds.top, bounds.Width(), bounds.Height(), &dc, 0, 0, SRCCOPY);
    dc.SelectObject(oldBitmap);
    target.Detach();
}

void CArduinoMonitorDlg::UpdateDisplay()
{
    CString voltageText;
    voltageText.Format(_T("%.2f V"), m_testVoltage);

    SetDlgItemText(IDC_STATIC_VOLTAGE_VALUE, voltageText);
    SetDlgItemText(IDC_STATIC_STATE_VALUE, StateLabel(m_state));
    SetDlgItemText(IDC_STATIC_RELAY_VALUE, m_relayState == _T("ON") ? _T("켜짐") : _T("꺼짐"));
    SetDlgItemText(
        IDC_STATIC_TRIP_VALUE,
        m_tripCause == _T("NONE") ? _T("차단 없음") : _T("과전압 차단"));
}

void CArduinoMonitorDlg::OnDestroy()
{
    KillTimer(SERIAL_TIMER_ID);
    m_serial.Close();
    CDialogEx::OnDestroy();
}
