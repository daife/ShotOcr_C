#ifndef APPMANAGER_H
#define APPMANAGER_H

#include <windows.h>
#include <shellapi.h>
#include <string>

// 托盘消息常量
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_ABOUT 1002
#define ID_TRAY_AUTOSTART 1003

class HotkeyManager;
class ScreenCapture;
class VoiceRecognizer;
class ToastWindow;

class AppManager {
public:
    AppManager();
    ~AppManager();
    
    void run();
    void showToast(const std::string& message, int duration = 2000);
    void exitApplication();

    // 公共访问组件（供HotkeyManager使用）
    ScreenCapture* screenCapture;
    VoiceRecognizer* voiceRecognizer;

private:
    // 托盘相关成员
    HWND hiddenWindow;
    NOTIFYICONDATA nid;
    
    // 功能组件
    HotkeyManager* hotkeyManager;
    
    static AppManager* instance;
    
    // 托盘相关方法
    void createTrayIcon();
    void removeTrayIcon();
    void showContextMenu(int x, int y);
    
    // 开机自启动相关方法
    bool isAutoStartEnabled();
    void setAutoStart(bool enable);
    void toggleAutoStart();
    
    static LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    friend class HotkeyManager;
};

#endif // APPMANAGER_H
