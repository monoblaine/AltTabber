#include "stdafx.h"
#include "AltTabber.h"
#include <UIAutomation.h>
#include <ShObjIdl.h>

extern ProgramState_t g_programState;
extern void log(LPTSTR fmt, ...);
extern void QuitOverlay();

static void MoveNextGeographically(POINT p)
{
    if(g_programState.activeSlot < 0) {
        log(_T("no active slot"));
        return;
    }

    auto& slots = g_programState.slots;
    SlotThing_t& slot = slots[g_programState.activeSlot];
    RECT& r = slot.r;
    log(_T("moving away from %ld %ld %ld %ld\n"),
        r.left, r.top, r.right, r.bottom);
    POINT speculant = {
            p.x * 5
            + (p.x > 0) * r.right
            + (p.x < 0) * r.left
            + (!p.x) * ( (r.left + r.right) / 2)
        ,
            p.y * 5
            + (p.y > 0) * r.bottom
            + (p.y < 0) * r.top
            + (!p.y) * ( (r.top + r.bottom) / 2l)
        ,
    };

    auto found = std::find_if(slots.begin(), slots.end(),
        [&speculant](SlotThing_t& s) -> bool {
            return PtInRect(&s.r, speculant) != FALSE;
        });

    if(found != slots.end()) {
        g_programState.activeSlot = (long)(found - slots.begin());
        return;
    }
    /* else */
    log(_T("could not find a slot speculating at %ld %ld, trying wrap around\n"),
        speculant.x, speculant.y);
    RECT wr;
    (void) GetWindowRect(g_programState.hWnd, &wr);
    speculant.x =
            p.x * 5
            + (p.x > 0) * 0
            + (p.x < 0) * (wr.right - wr.left)
            + (!p.x) * ( (r.left + r.right) / 2)
        ;
    speculant.y =
            p.y * 5
            + (p.y > 0) * 0
            + (p.y < 0) * (wr.bottom - wr.top)
            + (!p.y) * ( (r.top + r.bottom) / 2l)
        ;

    auto found2 = std::find_if(slots.begin(), slots.end(),
        [&speculant](SlotThing_t& s) -> bool {
            return PtInRect(&s.r, speculant) != FALSE;
        });

    if(found2 != slots.end()) {
        g_programState.activeSlot = (long)(found2 - slots.begin());
        return;
    }

    log(_T("could not find a slot speculating at %ld %ld, silently failing\n"),
        speculant.x, speculant.y);
}

void MoveNext(DWORD direction)
{
    if(g_programState.activeSlot < 0) {
        if(g_programState.slots.size() > 0) {
            g_programState.activeSlot = (long)(g_programState.slots.size() - 1);
        } else {
            return;
        }
    }
    log(_T("Moving from %ld "), g_programState.activeSlot);
    POINT p = { 0, 0 };
    switch(direction)
    {
        case VK_TAB:
        case VK_RIGHT:
            g_programState.activeSlot++;
            log(_T("by %d\n"), 1);
            break;
        case VK_BACK:
        case VK_LEFT:
            g_programState.activeSlot--;
            log(_T("by %d\n"), -1);
            break;
        case VK_UP:
            p.y = -1;
            MoveNextGeographically(p);
            break;
        case VK_DOWN:
            p.y = 1;
            MoveNextGeographically(p);
            break;
    }
    if(g_programState.activeSlot < 0) {
        g_programState.activeSlot = (long)
            (g_programState.slots.size() + g_programState.activeSlot);
    }
    g_programState.activeSlot %= g_programState.slots.size();

    if(g_programState.activeSlot >= 0) {
        log(_T("Current active slot: %ld hwnd: %p\n"),
                g_programState.activeSlot,
                (void*)g_programState.slots[g_programState.activeSlot].hwnd);
    }

    RedrawWindow(g_programState.hWnd, NULL, NULL, RDW_INVALIDATE);
}

BOOL InitializeUIAutomation(IUIAutomation** automation)
{
    CoInitialize(NULL);
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**) automation);

    return (SUCCEEDED(hr));
}

BOOL IsButton(IUIAutomationElement* control, int* state) {
    CONTROLTYPEID controlTypeId;

    control->get_CurrentControlType(&controlTypeId);

    if (controlTypeId != UIA_ButtonControlTypeId) {
        return FALSE;
    }

    VARIANT var_propertyValue;
    control->GetCurrentPropertyValue(UIA_LegacyIAccessibleStatePropertyId, &var_propertyValue);
    auto hasPopup = ((*state = var_propertyValue.intVal) & STATE_SYSTEM_HASPOPUP) != 0;
    VariantClear(&var_propertyValue);

    return hasPopup;
}

IUIAutomationElement* GetSiblingButton(IUIAutomationTreeWalker* treeWalker, BOOL isLtr, IUIAutomationElement* el, int* buttonState) {
    IUIAutomationElement* sibling = NULL;

    if (isLtr) {
        treeWalker->GetNextSiblingElement(el, &sibling);
    }
    else {
        treeWalker->GetPreviousSiblingElement(el, &sibling);
    }

    while (sibling != NULL && !IsButton(sibling, buttonState)) {
        auto tmp = GetSiblingButton(treeWalker, isLtr, sibling, buttonState);
        sibling->Release();
        sibling = tmp;
    }

    return sibling;
}

IUIAutomationElement* GetChildButton(IUIAutomationTreeWalker* treeWalker, BOOL isLtr, IUIAutomationElement* parentEl, int* buttonState) {
    IUIAutomationElement* child = NULL;

    if (isLtr) {
        treeWalker->GetFirstChildElement(parentEl, &child);
    }
    else {
        treeWalker->GetLastChildElement(parentEl, &child);
    }

    if (child == NULL || IsButton(child, buttonState)) {
        return child;
    }

    auto tmp = GetSiblingButton(treeWalker, isLtr, child, buttonState);
    child->Release();
    child = tmp;

    return child;
}

void MoveNextOnTaskbar(DWORD direction)
{
    auto isLtr = direction == VK_RIGHT;
    IUIAutomation* pUIAutomation = NULL;
    IUIAutomationTreeWalker* treeWalker = NULL;
    IUIAutomationElement* windowElement = NULL;
    IUIAutomationElement* firstVisitedTaskbarButton = NULL;
    IUIAutomationElement* activeTaskbarButton = NULL;
    IUIAutomationInvokePattern* invokePattern = NULL;
    int buttonState;
    int isPressed;

    InitializeUIAutomation(&pUIAutomation);
    pUIAutomation->get_ControlViewWalker(&treeWalker);

    auto hDesktop = GetDesktopWindow();
    auto hTray = FindWindowEx(hDesktop, NULL, _T("Shell_TrayWnd"), NULL);
    auto hReBar = FindWindowEx(hTray, NULL, _T("ReBarWindow32"), NULL);
    auto hTask = FindWindowEx(hReBar, NULL, _T("MSTaskSwWClass"), NULL);
    auto hToolbar = FindWindowEx(hTask, NULL, _T("MSTaskListWClass"), NULL);

    pUIAutomation->ElementFromHandle(hToolbar, &windowElement);
    firstVisitedTaskbarButton = activeTaskbarButton = GetChildButton(treeWalker, isLtr, windowElement, &buttonState);
    windowElement->Release();

    if (activeTaskbarButton) {
        isPressed = buttonState & STATE_SYSTEM_PRESSED;
    }

    while (activeTaskbarButton && !isPressed) {
        auto tmpEl = GetSiblingButton(treeWalker, isLtr, activeTaskbarButton, &buttonState);

        if (activeTaskbarButton != firstVisitedTaskbarButton) {
            activeTaskbarButton->Release();
        }

        activeTaskbarButton = tmpEl;

        if (activeTaskbarButton != NULL) {
            isPressed = buttonState & STATE_SYSTEM_PRESSED;
        }
    }

    if (isPressed) {
        auto targetTaskbarButton = GetSiblingButton(treeWalker, isLtr, activeTaskbarButton, &buttonState);

        if (targetTaskbarButton == NULL) {
            targetTaskbarButton = firstVisitedTaskbarButton;
        }

        auto res = targetTaskbarButton->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), (void**) &invokePattern);

        if (SUCCEEDED(res)) {
            invokePattern->Invoke();
        }

        if (invokePattern) {
            invokePattern->Release();
        }

        targetTaskbarButton->Release();

        if (activeTaskbarButton != targetTaskbarButton) {
            activeTaskbarButton->Release();
        }

        if (firstVisitedTaskbarButton != activeTaskbarButton) {
            firstVisitedTaskbarButton->Release();
        }
    }
    else {
        if (activeTaskbarButton != NULL) {
            activeTaskbarButton->Release();
        }

        if (firstVisitedTaskbarButton != NULL && firstVisitedTaskbarButton != activeTaskbarButton) {
            firstVisitedTaskbarButton->Release();
        }
    }

    treeWalker->Release();
    pUIAutomation->Release();
    CoUninitialize();
}

void SelectByMouse(DWORD lParam)
{
    int xPos = GET_X_LPARAM(lParam);
    int yPos = GET_Y_LPARAM(lParam);
    POINT pt = { xPos, yPos };
    auto found = std::find_if(
            g_programState.slots.begin(), g_programState.slots.end(),
            [&](SlotThing_t const& thing) -> BOOL {
                return PtInRect(&thing.r, pt);
            });
    if(found != g_programState.slots.end()) {
        g_programState.activeSlot = (long)(found - g_programState.slots.begin());
        RedrawWindow(g_programState.hWnd, NULL, NULL, RDW_INVALIDATE);
    }
}

void SelectCurrent()
{
    if(g_programState.activeSlot >= 0) {
        g_programState.prevActiveWindow =
            g_programState.slots[g_programState.activeSlot]
            .hwnd;
        QuitOverlay();
    }
}