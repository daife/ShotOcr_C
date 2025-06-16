#include "../include/AppManager.h"
#include "../include/HotkeyManager.h"
#include "../include/ScreenCapture.h"
#include "../include/VoiceRecognizer.h"
#include "../include/StringUtils.h"
#include <thread>
#include <gdiplus.h>
#include <windowsx.h>

// 链接库只在 MSVC 编译器下有效
#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

AppManager* AppManager::instance = nullptr;

// ToastWindow 类实现
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

ToastWindow::ToastWindow(const std::string& message, int duration) 
    : hwnd(nullptr), message(message), duration(duration) {
}

ToastWindow::~ToastWindow() {
    close();
}

void ToastWindow::createWindow() {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ToastWindow";
    wc.hbrBackground = CreateSolidBrush(RGB(51, 51, 51));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    
    RegisterClassEx(&wc);
    
    hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        "ToastWindow",
        "Toast",
        WS_POPUP,
        0, 0, 500, 120,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );
    
    if (hwnd) {
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)(255 * 0.9), LWA_ALPHA);
        
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - 500) / 2;
        int y = screenHeight - 200;
        
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, 500, 120, SWP_SHOWWINDOW);
        SetTimer(hwnd, 1, duration, TimerProc);
    }
}

void ToastWindow::show() {
    createWindow();
    
    if (hwnd) {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT) break;
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
}

void ToastWindow::close() {
    if (hwnd) {
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
}

LRESULT CALLBACK ToastWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ToastWindow* toast = nullptr;
    
    if (uMsg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        toast = static_cast<ToastWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(toast));
    } else {
        toast = reinterpret_cast<ToastWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        RECT rect;
        GetClientRect(hwnd, &rect);
        
        HBRUSH bgBrush = CreateSolidBrush(RGB(51, 51, 51));
        FillRect(hdc, &rect, bgBrush);
        DeleteObject(bgBrush);
        
        if (toast) {
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            
            HFONT font = CreateFont(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
            HFONT oldFont = (HFONT)SelectObject(hdc, font);
            
            std::wstring wMessage = Utf8ToWide(toast->message); 
            DrawTextW(hdc, wMessage.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            
            SelectObject(hdc, oldFont);
            DeleteObject(font);
        }
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (toast) toast->close();
        return 0;
    case WM_TIMER:
        if (toast) toast->close();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CALLBACK ToastWindow::TimerProc(HWND hwnd, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) {
    ToastWindow* toast = reinterpret_cast<ToastWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (toast) {
        toast->close();
    }
}

// AppManager 实现
AppManager::AppManager() 
    : hiddenWindow(nullptr), hotkeyManager(nullptr), 
      screenCapture(nullptr), voiceRecognizer(nullptr) {
    
    instance = this;
    
    SetProcessDPIAware();
    
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    // 创建功能组件
    screenCapture = new ScreenCapture(this);
    voiceRecognizer = new VoiceRecognizer(this);
    hotkeyManager = new HotkeyManager(this);
    
    createTrayIcon();
    hotkeyManager->startListening();
    
    showToast("ShotOcr已启动", 3000);
}

AppManager::~AppManager() {
    if (hotkeyManager) {
        delete hotkeyManager;
        hotkeyManager = nullptr;
    }
    if (screenCapture) {
        delete screenCapture;
        screenCapture = nullptr;
    }
    if (voiceRecognizer) {
        delete voiceRecognizer;
        voiceRecognizer = nullptr;
    }
    
    removeTrayIcon();
    instance = nullptr;
}

void AppManager::showToast(const std::string& message, int duration) {
    std::thread([message, duration]() {
        ToastWindow toast(message, duration);
        toast.show();
    }).detach();
}

void AppManager::run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void AppManager::exitApplication() {
    PostQuitMessage(0);
}

void AppManager::createTrayIcon() {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = HiddenWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "AppManagerHidden";
    
    RegisterClassEx(&wc);
    
    hiddenWindow = CreateWindowEx(
        0,
        "AppManagerHidden",
        "AppManager Hidden Window",
        0,
        0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );
    
    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hiddenWindow;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    
    #ifdef UNICODE
        wcscpy_s(nid.szTip, sizeof(nid.szTip) / sizeof(wchar_t), L"ShotOcr");
    #else
        strcpy_s(nid.szTip, sizeof(nid.szTip), "ShotOcr");
    #endif
    
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void AppManager::removeTrayIcon() {
    if (hiddenWindow) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        DestroyWindow(hiddenWindow);
        hiddenWindow = nullptr;
    }
}

LRESULT CALLBACK AppManager::HiddenWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    AppManager* app = nullptr;
    
    if (uMsg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = static_cast<AppManager*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<AppManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    switch (uMsg) {
    case WM_TRAYICON:
        if (app) {
            switch (lParam) {
            case WM_RBUTTONUP:
                {
                    POINT pt;
                    GetCursorPos(&pt);
                    app->showContextMenu(pt.x, pt.y);
                }
                break;
            case WM_LBUTTONDBLCLK:
                app->screenCapture->startCapture();
                break;
            }
        }
        return 0;
    case WM_COMMAND:
        if (app) {
            switch (LOWORD(wParam)) {
            case ID_TRAY_EXIT:
                app->exitApplication();
                break;
            case ID_TRAY_ABOUT:
                MessageBoxW(nullptr, L"ShotOcr\n截图OCR: Ctrl+Shift+S 或 双击托盘图标\n语音识别: Ctrl+Shift+H(空格结束)\n取消操作：Esc 或 鼠标右键", L"关于", MB_OK | MB_ICONINFORMATION);
                break;
            case ID_TRAY_AUTOSTART:
                app->toggleAutoStart();
                break;
            }
        }
        return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void AppManager::showContextMenu(int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"关于");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    
    UINT flags = MF_STRING;
    if (isAutoStartEnabled()) {
        flags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, flags, ID_TRAY_AUTOSTART, L"开机自启动");
    
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
    
    SetForegroundWindow(hiddenWindow);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, hiddenWindow, nullptr);
    DestroyMenu(hMenu);
}

bool AppManager::isAutoStartEnabled() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, 
                               L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 
                               0, KEY_READ, &hKey);
    
    if (result != ERROR_SUCCESS) {
        return false;
    }
    
    wchar_t buffer[MAX_PATH];
    DWORD bufferSize = sizeof(buffer);
    result = RegQueryValueExW(hKey, L"ShotOcr", nullptr, nullptr, 
                             (LPBYTE)buffer, &bufferSize);
    
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

void AppManager::setAutoStart(bool enable) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, 
                               L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 
                               0, KEY_SET_VALUE, &hKey);
    
    if (result != ERROR_SUCCESS) {
        return;
    }
    
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        
        result = RegSetValueExW(hKey, L"ShotOcr", 0, REG_SZ, 
                               (const BYTE*)exePath, 
                               (wcslen(exePath) + 1) * sizeof(wchar_t));
        
        if (result == ERROR_SUCCESS) {
            showToast("已启用开机自启动");
        } else {
            showToast("设置开机自启动失败");
        }
    } else {
        result = RegDeleteValueW(hKey, L"ShotOcr");
        
        if (result == ERROR_SUCCESS) {
            showToast("已禁用开机自启动");
        } else {
            showToast("取消开机自启动失败");
        }
    }
    
    RegCloseKey(hKey);
}

void AppManager::toggleAutoStart() {
    bool currentState = isAutoStartEnabled();
    setAutoStart(!currentState);
}
