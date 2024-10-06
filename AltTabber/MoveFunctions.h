#pragma once

void MoveNext(DWORD direction);
void getFirstChildElement(IUIAutomationElement** el, bool releaseOriginalEl = true);
void MoveNextOnTaskbar(DWORD direction);
void SelectCurrent();
void SelectByMouse(DWORD lParam);
