#include "stdafx.h"
#include "AltTabber.h"

extern ProgramState_t g_programState;
extern void log(LPTSTR fmt, ...);
extern MonitorGeom_t GetMonitorGeometry();
extern void CreateThumbnails(std::wstring const& filter);
extern void SetThumbnails();

void Quit();

void ActivateSwitcher()
{
    g_programState.prevActiveWindow = GetForegroundWindow();
    g_programState.showing = TRUE;

    auto windowThreadProcessId = GetWindowThreadProcessId(g_programState.prevActiveWindow, LPDWORD(0));
    auto currentThreadId = GetCurrentThreadId();

    AttachThreadInput(windowThreadProcessId, currentThreadId, true);
    SetForegroundWindow(g_programState.hWnd);
    SetFocus(g_programState.hWnd);
    AttachThreadInput(windowThreadProcessId, currentThreadId, false);

    auto monitorGeom = GetMonitorGeometry();
    auto hrSWP = SetWindowPos(g_programState.hWnd, NULL, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
         hrSWP = SetWindowPos(g_programState.hWnd, HWND_TOPMOST, monitorGeom.r.left, monitorGeom.r.top, monitorGeom.r.right - monitorGeom.r.left, monitorGeom.r.bottom - monitorGeom.r.top, SWP_NOSENDCHANGING);

    g_programState.filter = _T("");
    CreateThumbnails(g_programState.filter);
    SetThumbnails();
}

void QuitOverlay()
{
    log(_T("escape pressed; reverting\n"));
    g_programState.showing = FALSE;
    auto monitorGeom = GetMonitorGeometry();
    SetWindowPos(g_programState.hWnd,
            0,
            0, 0,
            0, 0,
            SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOZORDER | SWP_NOSENDCHANGING);
    if(g_programState.prevActiveWindow) {
        HWND hwnd = g_programState.prevActiveWindow;
        auto hr = SetForegroundWindow(hwnd);
        log(_T("set foreground window to previous result: %d\n"), hr);
        if(IsIconic(hwnd)) {
            // note to self: apparently only the owner of the window
            // can re-maximize it/restore it properly; calling anything
            // else like SetWindowPlacement or ShowWindow results in
            // its internal min/max state being broken; by sending
            // that window an actual message, things seem to work fine

            auto hr = PostMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
            //auto hr = SendMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
            //auto hr = OpenIcon(hwnd);
            log(_T("restoring %p hr = %ld\n"), (void*)hwnd, hr);
        }
    }
}

void forceForegroundWindow(HWND hwnd) {
    DWORD windowThreadProcessId = GetWindowThreadProcessId(GetForegroundWindow(),LPDWORD(0));
    DWORD currentThreadId = GetCurrentThreadId();
    AttachThreadInput(windowThreadProcessId, currentThreadId, TRUE);
    BringWindowToTop(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    AttachThreadInput(windowThreadProcessId,currentThreadId, FALSE);
}

void Quit()
{
    if (g_programState.prevActiveWindow) {
        forceForegroundWindow(g_programState.prevActiveWindow);
    }
}
