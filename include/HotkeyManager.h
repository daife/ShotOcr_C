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
    HHOOK mouseHook;  // 新增：鼠标钩子
    
    static HotkeyManager* instance;
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);  // 新增：鼠标钩子处理函数
    
    bool isCtrlShiftPressed();
};

#endif // HOTKEYMANAGER_H
