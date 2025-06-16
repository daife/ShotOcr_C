#ifndef HOTKEYMANAGER_H
#define HOTKEYMANAGER_H

#include <windows.h>

class AppManager;

class HotkeyManager {
public:
    HotkeyManager(AppManager* app);
    ~HotkeyManager();
    
    void startListening();
    void stopListening();

private:
    AppManager* appManager;
    HHOOK keyboardHook;
    
    static HotkeyManager* instance;
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    bool isCtrlShiftPressed();
};

#endif // HOTKEYMANAGER_H
