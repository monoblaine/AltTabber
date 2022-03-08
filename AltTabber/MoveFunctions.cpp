#include "stdafx.h"
#include "AltTabber.h"
#include <UIAutomation.h>
#include <ShObjIdl.h>

extern ProgramState_t g_programState;
extern IUIAutomation* uiAutomation;
extern IUIAutomationTreeWalker* treeWalker;
extern IUIAutomationElement* toolbar;
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

static inline void updateEl(HRESULT hr, IUIAutomationElement** el, IUIAutomationElement** tmp, bool releaseOriginalEl) {
    if (SUCCEEDED(hr)) {
        if (releaseOriginalEl) {
            (*el)->Release();
        }

        *el = *tmp;
    }
    else {
        *el = nullptr;
    }
}

static void getNextSiblingElement(IUIAutomationElement** el, bool releaseOriginalEl = true) {
    IUIAutomationElement* tmp;
    auto hr = treeWalker->GetNextSiblingElement(*el, &tmp);
    updateEl(hr, el, &tmp, releaseOriginalEl);
}

static void getPrevSiblingElement(IUIAutomationElement** el, bool releaseOriginalEl = true) {
    IUIAutomationElement* tmp;
    auto hr = treeWalker->GetPreviousSiblingElement(*el, &tmp);
    updateEl(hr, el, &tmp, releaseOriginalEl);
}

static void getFirstChildElement(IUIAutomationElement** el, bool releaseOriginalEl = true) {
    IUIAutomationElement* tmp;
    auto hr = treeWalker->GetFirstChildElement(*el, &tmp);
    updateEl(hr, el, &tmp, releaseOriginalEl);
}

static void getLastChildElement(IUIAutomationElement** el, bool releaseOriginalEl = true) {
    IUIAutomationElement* tmp;
    auto hr = treeWalker->GetLastChildElement(*el, &tmp);
    updateEl(hr, el, &tmp, releaseOriginalEl);
}

static bool isButtonWithPopup(IUIAutomationElement* el, int* buttonState) {
    CONTROLTYPEID typeId;
    el->get_CurrentControlType(&typeId);

    if (typeId != UIA_ButtonControlTypeId) {
        return false;
    }

    VARIANT variant;
    el->GetCurrentPropertyValue(UIA_LegacyIAccessibleStatePropertyId, &variant);
    auto hasPopup = ((*buttonState = variant.intVal) & STATE_SYSTEM_HASPOPUP) != 0;
    VariantClear(&variant);

    return hasPopup;
}

static void sendLeftClick(int x, int y) {
    const double XSCALEFACTOR = 65535.0 / (GetSystemMetrics(SM_CXSCREEN) - 1);
    const double YSCALEFACTOR = 65535.0 / (GetSystemMetrics(SM_CYSCREEN) - 1);

    POINT cursorPos;
    GetCursorPos(&cursorPos);

    double cx = cursorPos.x * XSCALEFACTOR;
    double cy = cursorPos.y * YSCALEFACTOR;

    double nx = x * XSCALEFACTOR;
    double ny = y * YSCALEFACTOR;

    INPUT input = { 0 };
    input.type = INPUT_MOUSE;

    input.mi.dx = (LONG) nx;
    input.mi.dy = (LONG) ny;

    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;

    SendInput(1, &input, sizeof(INPUT));

    input.mi.dx = (LONG) cx;
    input.mi.dy = (LONG) cy;

    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    SendInput(1, &input, sizeof(INPUT));
}

void MoveNextOnTaskbar(DWORD direction)
{
    auto isLtr = direction == VK_RIGHT;
    IUIAutomationElement* el = toolbar;
    int buttonState;

    getFirstChildElement(&el, false);

    while (el) {
        if (isButtonWithPopup(el, &buttonState) && (buttonState & STATE_SYSTEM_PRESSED) != 0) {
            break;
        }

        getNextSiblingElement(&el);
    }

    if (el) {
        void (*getSiblingElement)(IUIAutomationElement**, bool) = nullptr;
        void (*getElementFromEdge)(IUIAutomationElement**, bool) = nullptr;

        if (isLtr) {
            getSiblingElement = &getNextSiblingElement;
            getElementFromEdge = &getFirstChildElement;
        }
        else {
            getSiblingElement = &getPrevSiblingElement;
            getElementFromEdge = &getLastChildElement;
        }

        do {
            getSiblingElement(&el, true);

            if (!el) {
                el = toolbar;
                getElementFromEdge(&el, false);
            }

            if (el && isButtonWithPopup(el, &buttonState)) {
                break;
            }
        }
        while (el);

        if (el) {
            VARIANT variant;

            el->GetCurrentPropertyValue(UIA_BoundingRectanglePropertyId, &variant);
            el->Release();
            double* rectData = nullptr;
            SafeArrayAccessData(variant.parray, (void**) &rectData);
            auto left = (int) rectData[0];
            auto top = (int) rectData[1];
            auto right = left + (int) rectData[2];
            auto bottom = top + (int) rectData[3];
            SafeArrayUnaccessData(variant.parray);
            VariantClear(&variant);

            auto pointX = left + (int) ((right - left) / 2);
            auto pointY = top + (int) ((bottom - top) / 2);

            sendLeftClick(pointX, pointY);
        }
    }
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