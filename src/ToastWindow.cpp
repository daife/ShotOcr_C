#include "../include/ScreenShotOCR.h"
#include "../include/StringUtils.h"
#include <windowsx.h>

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
    
    // 创建更大的无边框置顶窗口 - 进一步增大窗口尺寸
    hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        "ToastWindow",
        "Toast",
        WS_POPUP,
        0, 0, 500, 120,  // 窗口尺寸从400x80改为500x120
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );
    
    if (hwnd) {
        // 设置透明度
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)(255 * 0.9), LWA_ALPHA);
        
        // 计算窗口位置（屏幕中下方）
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - 500) / 2;  // 调整居中计算
        int y = screenHeight - 200;  // 调整垂直位置
        
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, 500, 120, SWP_SHOWWINDOW);  // 更新窗口尺寸
        
        // 设置定时器自动关闭
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
        
        // 设置背景
        HBRUSH bgBrush = CreateSolidBrush(RGB(51, 51, 51));
        FillRect(hdc, &rect, bgBrush);
        DeleteObject(bgBrush);
        
        // 绘制文本 - 进一步增大字体
        if (toast) {
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            
            HFONT font = CreateFont(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,  // 字体大小从36改为48
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

void CALLBACK ToastWindow::TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    ToastWindow* toast = reinterpret_cast<ToastWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (toast) {
        toast->close();
    }
}
