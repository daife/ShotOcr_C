#ifndef SCREENSHOTOCR_H
#define SCREENSHOTOCR_H

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

// 添加托盘消息常量
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_ABOUT 1002
#define ID_TRAY_AUTOSTART 1003

class ToastWindow {
public:
    ToastWindow(const std::string& message, int duration = 2000);
    ~ToastWindow();
    void show();
    void close();

private:
    HWND hwnd;
    std::string message;
    int duration;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void createWindow();
    static void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
};

class ScreenShotOCR {
public:
    ScreenShotOCR();
    ~ScreenShotOCR();
    
    void startCapture();
    void run();

private:
    HWND overlayWindow;
    
    int startX, startY, endX, endY;
    bool dragging;
    bool windowCreated;
    
    int screenWidth, screenHeight;
    std::string apiUrl;
    
    // 托盘相关成员
    HWND hiddenWindow;
    NOTIFYICONDATA nid;
    
    static LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    void createOverlayWindow();
    void closeOverlay();
    void onMousePress(int x, int y);
    void onMouseDrag(int x, int y);
    void onMouseRelease(int x, int y);
    void captureAndOCR(int x1, int y1, int x2, int y2);
    std::string callYoudaoOCR(const std::string& imgBase64);
    void copyToClipboard(const std::string& text);
    void showToast(const std::string& message, int duration = 2000);
    std::string captureScreenRegion(int x, int y, int width, int height);
    std::string encodeBase64(const std::vector<unsigned char>& data);
    std::vector<unsigned char> decodeBase64(const std::string& encoded);
    void saveImageToFile(const std::string& imgBase64);
    std::string unescapeJsonString(const std::string& escapedStr);
    
    // 托盘相关方法
    void createTrayIcon();
    void removeTrayIcon();
    void showContextMenu(int x, int y);
    void exitApplication();
    
    // 开机自启动相关方法
    bool isAutoStartEnabled();
    void setAutoStart(bool enable);
    void toggleAutoStart();
    
    static ScreenShotOCR* instance;
    HHOOK keyboardHook;
};

#endif // SCREENSHOTOCR_H
