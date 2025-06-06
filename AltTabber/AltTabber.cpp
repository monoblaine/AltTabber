// AltTabber.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "AltTabber.h"
#include <UIAutomation.h>

#define MAX_LOADSTRING 100

#define MY_NOTIFICATION_ICON 2
#define MY_NOTIFY_ICON_MESSAGE_ID (WM_USER + 88)
#define MY_CLOSE_BTN_ID (WM_USER + 89)
#define MY_TRAY_CLOSE_BTN_ID (WM_USER + 90)
#define MY_TRAY_OPEN_BTN_ID (WM_USER + 91)

#define MY_MOVE_TO_BASE_ID (WM_USER + 100)
#define MY_MOVE_TO_1_ID  (MY_MOVE_TO_BASE_ID + 0)
#define MY_MOVE_TO_2_ID  (MY_MOVE_TO_BASE_ID + 1)
#define MY_MOVE_TO_3_ID  (MY_MOVE_TO_BASE_ID + 2)
#define MY_MOVE_TO_4_ID  (MY_MOVE_TO_BASE_ID + 3)
#define MY_MOVE_TO_5_ID  (MY_MOVE_TO_BASE_ID + 4)
#define MY_MOVE_TO_6_ID  (MY_MOVE_TO_BASE_ID + 5)
#define MY_MOVE_TO_7_ID  (MY_MOVE_TO_BASE_ID + 6)
#define MY_MOVE_TO_8_ID  (MY_MOVE_TO_BASE_ID + 7)
#define MY_MOVE_TO_9_ID  (MY_MOVE_TO_BASE_ID + 8)
#define MY_MOVE_TO_10_ID (MY_MOVE_TO_BASE_ID + 9)

extern void log(LPTSTR fmt, ...);
extern MonitorGeom_t GetMonitorGeometry();
extern void SynchronizeWithRegistry();
extern void ActivateSwitcher();
extern void SelectCurrent();
extern void CloseCurrent(const HWND& hWnd);
extern void MoveNext(DWORD);
extern void MoveNextOnTaskbar(DWORD);
extern void SelectByMouse(DWORD);
extern void QuitOverlay();
extern void PurgeThumbnails();
extern void CreateThumbnails(std::wstring const&);
extern void SetThumbnails();
extern void OnPaint(HDC);

ProgramState_t g_programState = {
    /*showing=*/FALSE,
    /*prevActiveWindow=*/NULL,
    /*activeSlot=*/-1,
    /*logging=*/FALSE,
    /*freopened=*/NULL,
    /*hotkey=*/
#ifdef JAT_OLD_HOTKEY
    { MOD_ALT | MOD_CONTROL, '3' },
#else
    { MOD_ALT, VK_OEM_3 },
#endif
    /*compatHacks=*/0,
    /*resetOnClose=*/false,
};

// Global Variables:
HINSTANCE hInst;                                // current instance
TCHAR szTitle[MAX_LOADSTRING];                    // The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
IUIAutomation* uiAutomation = nullptr;
IUIAutomationTreeWalker* treeWalker = nullptr;
IUIAutomationElement* toolbar = nullptr;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

static bool initUIAutomation() {
    auto hr = CoInitialize(NULL);

    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(
            __uuidof(CUIAutomation),
            NULL,
            CLSCTX_INPROC_SERVER,
            __uuidof(IUIAutomation),
            (void**) &uiAutomation
        );

        if (SUCCEEDED(hr)) {
            uiAutomation->get_ContentViewWalker(&treeWalker);

            auto hDesktop = GetDesktopWindow();
            auto hTray = FindWindowEx(hDesktop, NULL, _T("Shell_TrayWnd"), NULL);
            auto hReBar = FindWindowEx(hTray, NULL, _T("ReBarWindow32"), NULL);
            auto hTask = FindWindowEx(hReBar, NULL, _T("MSTaskSwWClass"), NULL);
            auto hToolbar = FindWindowEx(hTask, NULL, _T("MSTaskListWClass"), NULL);

            uiAutomation->ElementFromHandle(hToolbar, &toolbar);
        }
    }

    return (SUCCEEDED(hr));
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO still not clear if I still need to call this
    //      or not, given that the manifest says this is
    //      dpi aware. Hope to some day figure out if this
    //      is needed/or not/or harmless
    SetProcessDPIAware();

    MSG msg;
    HACCEL hAccelTable;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_ALTTABBER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ALTTABBER));
    initUIAutomation();

    // Main message loop:
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

static inline void Cleanup()
{
    PurgeThumbnails();

    if(g_programState.freopened != NULL) fclose(g_programState.freopened);

    if (uiAutomation) {
        if (treeWalker) {
            treeWalker->Release();

            if (toolbar) {
                toolbar->Release();
            }
        }

        uiAutomation->Release();
    }

    CoUninitialize();
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style            = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra        = 0;
    wcex.cbWndExtra        = 0;
    wcex.hInstance        = hInstance;
    wcex.hIcon            = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ALTTABBER));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground    = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName    = NULL; //MAKEINTRESOURCE(IDC_ALTTABBER);
    wcex.lpszClassName    = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int)
{
    HWND hWnd;

    hInst = hInstance; // Store instance handle in our global variable

    auto geom = GetMonitorGeometry();
    // SetWindowPos(hWnd, HWND_TOPMOST, geom.x, geom.y, geom.cx, geom.cy);

    hWnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_COMPOSITED | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            szWindowClass,
            szTitle,
            WS_POPUP,
            geom.r.left, geom.r.top,
            geom.r.right - geom.r.left, geom.r.bottom - geom.r.top,
            NULL,
            NULL,
            hInstance,
            NULL);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    SynchronizeWithRegistry();

    if(RegisterHotKey(
                hWnd,
                1,
                g_programState.hotkey.modifiers,
                g_programState.hotkey.key))
    {
        log(_T("hotkey registered successfully\n"));
    }
    else
    {
        log(_T("failed to register hotkey\n"));
        MessageBox(hWnd,
                _T("Failed to register hotkey.\n"                           )
                _T("\n"                                                     )
                _T("This usually means that there is already a program\n"   )
                _T("running that has registered the same hotkey.\n"         )
                _T("\n"                                                     )
                _T("Possible resolutions:\n"                                )
                _T("- Close the already running instance of AltTabber\n"    )
                _T("- If there is another application that has registered\n")
                _T("  the current hotkey, change it in that application\n"  )
                _T("- Change AltTabber's hotkey by editing the registry\n"  )
                _T("  Refer to the README.md document (which you can find\n")
                _T("  at http://github.com/alzwded/AltTabber ) on how to\n" )
                _T("  accomplish this.\n"                                   )
                _T("\n"                                                     )
                _T("The application will now exit"                          )
            ,
            _T("AltTabber - Failed to register hotkey"),
            MB_OK | MB_ICONERROR);
        Cleanup();
        PostQuitMessage(0);
    }

    RegisterHotKey(hWnd, 2, MOD_WIN, VK_NEXT);
    RegisterHotKey(hWnd, 3, MOD_WIN, VK_PRIOR);

    g_programState.hWnd = hWnd;

    auto hrFW = FindWindow(_T("ThunderRT6Main"), _T("Dexpot"));
    if(hrFW) {
        g_programState.compatHacks |= JAT_HACK_DEXPOT;
    }

    return TRUE;
}

static inline void NotificationAreaMenu(POINT location)
{
    // as per documentation, if hWnd is not the foreground window,
    // then the popup menu will not go away. So do that.
    SetForegroundWindow(g_programState.hWnd);
    // actually show the menu
    HMENU ctxMenu = CreatePopupMenu();
    AppendMenu(ctxMenu, MF_STRING, MY_TRAY_OPEN_BTN_ID, _T("&Open"));
    AppendMenu(ctxMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(ctxMenu, MF_STRING, MY_TRAY_CLOSE_BTN_ID, _T("E&xit"));
    TrackPopupMenu(ctxMenu,
        TPM_RIGHTBUTTON,
        location.x, location.y,
        0, g_programState.hWnd, NULL);
    DestroyMenu(ctxMenu);
}

void MoveToMonitor(unsigned int monitor)
{
    if(g_programState.activeSlot < 0) return;

    HWND hwnd = g_programState.slots[g_programState.activeSlot].hwnd;
    auto mis = GetMonitorGeometry();
    auto mi = mis.monitors[monitor];

    WINDOWPLACEMENT wpl;
    wpl.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &wpl);
    WINDOWPLACEMENT newWpl;
    ZeroMemory(&newWpl, sizeof(newWpl));
    newWpl.flags = WPF_ASYNCWINDOWPLACEMENT;
    newWpl.length = sizeof(WINDOWPLACEMENT);
    newWpl.showCmd = wpl.showCmd;

    auto hmonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    auto found = std::find_if(mis.monitors.begin(), mis.monitors.end(), [&hmonitor](MonitorInfo_t& mif)->bool {
        return mif.hMonitor == hmonitor;
    });
    auto oldMonitor = *found;

    newWpl.rcNormalPosition = wpl.rcNormalPosition;
    int diffX = newWpl.rcNormalPosition.right - newWpl.rcNormalPosition.left;
    int diffY = newWpl.rcNormalPosition.bottom - newWpl.rcNormalPosition.top;

    MONITORINFO minfoOld;
    minfoOld.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(oldMonitor.hMonitor, &minfoOld);
    MONITORINFO minfoNew;
    minfoNew.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(mi.hMonitor, &minfoNew);

    newWpl.rcNormalPosition.left = minfoNew.rcWork.left + (newWpl.rcNormalPosition.left - minfoOld.rcWork.left);
    newWpl.rcNormalPosition.right = newWpl.rcNormalPosition.left + diffX;
    newWpl.rcNormalPosition.top = minfoNew.rcWork.top + (newWpl.rcNormalPosition.top - minfoOld.rcWork.top);
    newWpl.rcNormalPosition.bottom = newWpl.rcNormalPosition.top + diffY;
    newWpl.ptMaxPosition.x = minfoNew.rcWork.left;
    newWpl.ptMaxPosition.y = minfoNew.rcWork.top;

    // apparent SetWindowPlacement don't work if it's maximized
    if(newWpl.showCmd == SW_MAXIMIZE) {
        PostMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        SetWindowPlacement(hwnd, &newWpl);
    } else if(newWpl.showCmd == SW_MINIMIZE) {
        // FIXME
        // right now moving minimized windows is disabled
        // mostly because I don't know how to handle the
        // minimized maximized window case properly
        SetWindowPlacement(hwnd, &newWpl);
    } else {
        SetWindowPlacement(hwnd, &newWpl);
    }
    Sleep(50);

    CreateThumbnails(g_programState.filter);
    SetThumbnails();
    RedrawWindow(g_programState.hWnd, NULL, 0, RDW_INVALIDATE);

    auto foundSlot = std::find_if(g_programState.slots.begin(),
        g_programState.slots.end(),
        [&hwnd](SlotThing_t& sthing)->bool {
            return sthing.hwnd == hwnd;
    });
    if(foundSlot != g_programState.slots.end()) {
        g_programState.activeSlot = (long)(foundSlot - g_programState.slots.begin());
    }
}

void CloseCurrent(const HWND& hWnd)
{
    if (g_programState.activeSlot >= 0) {
        auto& slot = g_programState.slots[g_programState.activeSlot];
        PostMessage(slot.hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        if (g_programState.resetOnClose) {
            // clear the filter because of use case
            if (g_programState.slots.size() <= 2) {
                g_programState.filter = L"";
            }

            Sleep(50);

            // rebuild thumbnails because filter was changed
            // and there are maybe dangling slots
            CreateThumbnails(g_programState.filter);
            SetThumbnails();
            // force redraw window (the labels)
            RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        }
    }
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND    - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY    - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent, hotKeyId;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
    case MY_NOTIFY_ICON_MESSAGE_ID: {
        auto what = LOWORD(lParam);
        switch(what) {
        case NIN_SELECT:
            ActivateSwitcher();
            break;
        case NIN_KEYSELECT:
        case WM_CONTEXTMENU: {
            POINT location;
            location.x = GET_X_LPARAM(wParam);
            location.y = GET_Y_LPARAM(wParam);
            NotificationAreaMenu(location);
            break; }
        }
        break; }
    case WM_SYSCOMMAND:
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_COMMAND:
        wmId    = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case MY_TRAY_OPEN_BTN_ID:
            ActivateSwitcher();
            break;
        case MY_TRAY_CLOSE_BTN_ID:
            PostMessage(hWnd, WM_CLOSE, 0, 0);
            break;
        case MY_CLOSE_BTN_ID:
            CloseCurrent(hWnd);
            break;
        case MY_MOVE_TO_1_ID:
        case MY_MOVE_TO_2_ID:
        case MY_MOVE_TO_3_ID:
        case MY_MOVE_TO_4_ID:
        case MY_MOVE_TO_5_ID:
        case MY_MOVE_TO_6_ID:
        case MY_MOVE_TO_7_ID:
        case MY_MOVE_TO_8_ID:
        case MY_MOVE_TO_9_ID:
        case MY_MOVE_TO_10_ID:
            log(_T("message was %ld\n"), message);
            MoveToMonitor(wmId - MY_MOVE_TO_BASE_ID);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_PAINT: {
        hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code here...
        if(g_programState.showing)
        {
            HGDIOBJ original = NULL;
            original = SelectObject(hdc, GetStockObject(DC_PEN));
            HPEN pen = CreatePen(PS_SOLID, 5, RGB(0, 255, 0));
            SelectObject(hdc, pen);
            MoveToEx(hdc, 0, 0, NULL);
            auto mi = GetMonitorGeometry();
            LineTo(hdc, mi.r.right - mi.r.left, mi.r.bottom - mi.r.top);
            SelectObject(hdc, original);
            DeleteObject(pen);

            OnPaint(hdc);
        }
        EndPaint(hWnd, &ps);
        break; }
    case WM_DESTROY:
        Cleanup();
        PostQuitMessage(0);
        break;
    case WM_HOTKEY:
        hotKeyId = (int) wParam;

        switch (hotKeyId) {
            case 2:
            case 3:
                if (g_programState.showing) {
                    return 0;
                }

                g_programState.showing = true;
                MoveNextOnTaskbar(hotKeyId == 2 ? VK_RIGHT : VK_LEFT);
                g_programState.showing = false;
                return 0;
        }

        // only waiting for one, so skip over trying to decode it
        // even if multiple keys are registered as hotkeys, they'd
        // all do the same thing anyway
        log(_T("hotkey pressed\n"));
        if(g_programState.showing) {
            SelectCurrent();
        } else {
            ActivateSwitcher();
        }
        break;
    case WM_MOUSEWHEEL: {
        int amount = GET_WHEEL_DELTA_WPARAM(wParam);
        if(amount > 0) MoveNext(VK_BACK);
        else MoveNext(VK_TAB);
        break; }
    case WM_LBUTTONDOWN:
        if(!g_programState.showing) break;
        SelectByMouse((DWORD)lParam);
        SelectCurrent();
        break;
    case WM_CHAR:
        switch(wParam) {
        case VK_ESCAPE:
        case VK_BACK:
        case VK_TAB:
        case VK_RETURN:
        case VK_SPACE:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        g_programState.filter += (wchar_t)wParam;
        log(_T("got filter %ls\n"), g_programState.filter.c_str());
        CreateThumbnails(g_programState.filter);
        SetThumbnails();
        RedrawWindow(g_programState.hWnd, NULL, NULL, RDW_INVALIDATE);
        break;
    case WM_KEYDOWN:
        switch(wParam) {
        case VK_ESCAPE:
            QuitOverlay();
            break;
        case VK_TAB:
            if(GetAsyncKeyState(VK_SHIFT)) {
                MoveNext(VK_BACK);
            } else {
                MoveNext(VK_TAB);
            }
            break;
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
            MoveNext((DWORD)wParam);
            break;
        case VK_RETURN:
        case VK_SPACE:
            SelectCurrent();
            break;
        case VK_DELETE:
            CloseCurrent(hWnd);
            break;
        case VK_F1:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case VK_F2:
            g_programState.logging = !g_programState.logging;
            break;
        case VK_BACK:
            if(!g_programState.filter.empty()) {
                g_programState.filter =
                    g_programState.filter.substr(
                            0, g_programState.filter.size() - 1);

                CreateThumbnails(g_programState.filter);
                SetThumbnails();
                RedrawWindow(g_programState.hWnd, NULL, NULL, RDW_INVALIDATE);
            }
            break;
#if 0
        default: {
            INT hr = (INT)MapVirtualKey(wParam, MAPVK_VK_TO_CHAR);
            if(hr > 0) {
                g_programState.filter += (wchar_t)hr;
                log(_T("got filter %ls\n"), g_programState.filter.c_str());
                CreateThumbnails(g_programState.filter);
                SetThumbnails();
                RedrawWindow(g_programState.hWnd, NULL, NULL, RDW_INVALIDATE);
            }
            break; }
#else
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
#endif
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
