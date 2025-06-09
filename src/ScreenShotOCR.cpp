#include "../include/ScreenShotOCR.h"
#include "../include/StringUtils.h" // 包含新的工具类

// 为特定的 Windows API 功能定义 _WIN32_WINNT。
// 注意：std::thread 需要 C++11（或更高版本）编译器模式，
// 这是一个与 _WIN32_WINNT 不同的设置。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

// 如果 'thread' 被先前包含的头文件（例如 <windows.h>）定义为宏，
// 则取消定义它以防止与 std::thread 冲突。
// "std::thread 未找到" 错误的主要原因通常是
// 编译器未配置为 C++11 或更高标准。
#ifdef thread
#undef thread
#endif
#include <thread>
#include <gdiplus.h>
#include <wininet.h>
#include <shlwapi.h>
#include <vector>
#include <sstream>
#include <iostream>
#include <windowsx.h>
#include <algorithm>
#include <cstdlib> // 为 std::abs 添加

// 添加 URL 编码函数声明
std::string urlEncode(const std::string& str);

// 链接必要的库
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

ScreenShotOCR* ScreenShotOCR::instance = nullptr;

ScreenShotOCR::ScreenShotOCR() 
    : overlayWindow(nullptr),
      startX(0), startY(0), endX(0), endY(0), dragging(false), windowCreated(false),
      apiUrl("https://aidemo.youdao.com/ocrapi1"), keyboardHook(nullptr) {
    
    instance = this;
    
    // 设置DPI感知，确保获取真实坐标
    SetProcessDPIAware();
    
    // 初始化GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    // 获取真实屏幕尺寸（不受DPI缩放影响）
    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // 安装全局键盘钩子监听 Ctrl+Shift+S
    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
    
    // 显示启动提示
    showToast("截图OCR已启动\n按 Ctrl+Shift+S 开始截图", 3000);
}

ScreenShotOCR::~ScreenShotOCR() {
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
    }
    closeOverlay();
    instance = nullptr;
}

LRESULT CALLBACK ScreenShotOCR::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN && instance) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        // 检测 Ctrl+Shift+S 组合键
        if (kb->vkCode == 'S' && 
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) && 
            (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            
            std::thread([](){ 
                if (instance) instance->startCapture(); 
            }).detach();
            
            return 1; // 阻止按键传递
        }
    }
    
    return CallNextHookEx(instance ? instance->keyboardHook : nullptr, nCode, wParam, lParam);
}

void ScreenShotOCR::startCapture() {
    if (windowCreated) return;
    
    createOverlayWindow();
}

void ScreenShotOCR::createOverlayWindow() {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = OverlayWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ScreenCaptureOverlay";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); // 使用黑色背景
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    
    RegisterClassEx(&wc);
    
    overlayWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        "ScreenCaptureOverlay",
        "Screen Capture",
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );
    
    if (overlayWindow) {
        // 设置透明度为0.2，对应Python的alpha=0.2
        SetLayeredWindowAttributes(overlayWindow, 0, (BYTE)(255 * 0.2), LWA_ALPHA);
        ShowWindow(overlayWindow, SW_SHOW);
        SetForegroundWindow(overlayWindow);
        windowCreated = true;
        
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) break;
        }
    }
}

LRESULT CALLBACK ScreenShotOCR::OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ScreenShotOCR* ocr = nullptr;
    
    if (uMsg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        ocr = static_cast<ScreenShotOCR*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ocr));
    } else {
        ocr = reinterpret_cast<ScreenShotOCR*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    switch (uMsg) {
    case WM_LBUTTONDOWN:
        if (ocr) ocr->onMousePress(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (ocr && ocr->dragging) {
            ocr->onMouseDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (ocr) ocr->onMouseRelease(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONDOWN:
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && ocr) {
            ocr->closeOverlay();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        // 绘制全屏遮罩
        HBRUSH grayBrush = CreateSolidBrush(RGB(128, 128, 128));
        RECT fullRect = {0, 0, ocr ? ocr->screenWidth : 0, ocr ? ocr->screenHeight : 0};
        FillRect(hdc, &fullRect, grayBrush);
        DeleteObject(grayBrush);
        
        // 如果正在拖拽，绘制红色选择框
        if (ocr && ocr->dragging) {
            HPEN redPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
            HPEN oldPen = (HPEN)SelectObject(hdc, redPen);
            SetBkMode(hdc, TRANSPARENT);
            
            // 将屏幕坐标转换回窗口坐标进行绘制
            POINT startPt = {ocr->startX, ocr->startY};
            POINT endPt = {ocr->endX, ocr->endY};
            ScreenToClient(hwnd, &startPt);
            ScreenToClient(hwnd, &endPt);
            
            int x1 = (std::min)(startPt.x, endPt.x);
            int y1 = (std::min)(startPt.y, endPt.y);
            int x2 = (std::max)(startPt.x, endPt.x);
            int y2 = (std::max)(startPt.y, endPt.y);
            
            // 绘制矩形边框
            MoveToEx(hdc, x1, y1, nullptr);
            LineTo(hdc, x2, y1);
            LineTo(hdc, x2, y2);
            LineTo(hdc, x1, y2);
            LineTo(hdc, x1, y1);
            
            SelectObject(hdc, oldPen);
            DeleteObject(redPen);
        }
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (ocr) {
            ocr->windowCreated = false;
            ocr->overlayWindow = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ScreenShotOCR::onMousePress(int x, int y) {
    // 将窗口坐标转换为屏幕坐标
    POINT pt = {x, y};
    ClientToScreen(overlayWindow, &pt);
    
    startX = pt.x;
    startY = pt.y;
    dragging = true;
}

void ScreenShotOCR::onMouseDrag(int x, int y) {
    if (!dragging) return;
    
    // 将窗口坐标转换为屏幕坐标
    POINT pt = {x, y};
    ClientToScreen(overlayWindow, &pt);
    
    endX = pt.x;
    endY = pt.y;
}

void ScreenShotOCR::onMouseRelease(int x, int y) {
    if (!dragging) return;
    
    dragging = false;
    
    // 将窗口坐标转换为屏幕坐标
    POINT pt = {x, y};
    ClientToScreen(overlayWindow, &pt);
    
    endX = pt.x;
    endY = pt.y;
    
    // 计算选择区域坐标（现在使用屏幕坐标）
    int x1 = (std::min)(startX, endX);
    int y1 = (std::min)(startY, endY);
    int x2 = (std::max)(startX, endX);
    int y2 = (std::max)(startY, endY);
    
    // 检查区域大小（对应Python的10x10像素检查）
    if (std::abs(x2 - x1) > 10 && std::abs(y2 - y1) > 10) {
        std::thread([this, x1, y1, x2, y2]() {
            captureAndOCR(x1, y1, x2, y2);
        }).detach();
    } else {
        closeOverlay();
    }
}

void ScreenShotOCR::captureAndOCR(int x1, int y1, int x2, int y2) {
    // 隐藏窗口（对应Python的withdraw）
    ShowWindow(overlayWindow, SW_HIDE);
    Sleep(200);  // 等待窗口完全隐藏
    
    try {
        // 截取屏幕区域
        std::string imgBase64 = captureScreenRegion(x1, y1, x2 - x1, y2 - y1);
        
        std::string ocrText = callYoudaoOCR(imgBase64);
        
        if (!ocrText.empty()) {
            // 去除首尾空格
            size_t start = ocrText.find_first_not_of(" \t\r\n");
            size_t end = ocrText.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                std::string cleanedText = ocrText.substr(start, end - start + 1);
                copyToClipboard(cleanedText);
                showToast("识别成功！已复制到剪贴板");
            } else {
                showToast("识别失败，未检测到文字");
            }
        } else {
            showToast("识别失败，未检测到文字");
        }
    } catch (...) {
        showToast("处理失败，请检查网络连接");
    }
    
    closeOverlay();
}

std::string ScreenShotOCR::captureScreenRegion(int x, int y, int width, int height) {
    // 确保参数有效
    if (width <= 0 || height <= 0) {
        return "";
    }
    
    // 直接使用屏幕DC进行截图
    HDC screenDC = CreateDC("DISPLAY", nullptr, nullptr, nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);
    
    // 直接从屏幕DC复制指定区域
    BitBlt(memDC, 0, 0, width, height, screenDC, x, y, SRCCOPY);
    
    // 转换为GDI+ Bitmap
    Gdiplus::Bitmap gdiBitmap(bitmap, nullptr);
    
    CLSID pngClsid;
    CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
    
    IStream* stream = nullptr;
    CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    
    Gdiplus::Status status = gdiBitmap.Save(stream, &pngClsid, nullptr);
    
    std::vector<unsigned char> pngData;
    
    if (status == Gdiplus::Ok) {
        HGLOBAL hGlobal;
        GetHGlobalFromStream(stream, &hGlobal);
        SIZE_T size = GlobalSize(hGlobal);
        void* data = GlobalLock(hGlobal);
        
        if (data && size > 0) {
            pngData.assign((unsigned char*)data, (unsigned char*)data + size);
        }
        
        GlobalUnlock(hGlobal);
    }
    
    stream->Release();
    
    // 清理资源
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    DeleteDC(screenDC);
    
    return encodeBase64(pngData);
}

void ScreenShotOCR::saveImageToFile(const std::string& imgBase64) {
    try {
        // 确保d:/tmp目录存在
        std::string tmpDir = "d:/tmp";
        CreateDirectoryA(tmpDir.c_str(), nullptr);
        
        // 生成带时间戳的文件名
        SYSTEMTIME st;
        GetLocalTime(&st);
        char filename[256];
        sprintf_s(filename, sizeof(filename), 
                  "d:/tmp/screenshot_%04d%02d%02d_%02d%02d%02d_%03d.png",
                  st.wYear, st.wMonth, st.wDay, 
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        
        // 解码base64数据
        std::vector<unsigned char> imageData = decodeBase64(imgBase64);
        
        // 写入文件
        FILE* file = nullptr;
        if (fopen_s(&file, filename, "wb") == 0 && file) {
            fwrite(imageData.data(), 1, imageData.size(), file);
            fclose(file);
        }
    } catch (...) {
        OutputDebugStringA("保存图片失败");
    }
}

std::vector<unsigned char> ScreenShotOCR::decodeBase64(const std::string& encoded) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> result;
    
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        
        size_t pos = chars.find(c);
        if (pos == std::string::npos) continue;
        
        val = (val << 6) + pos;
        valb += 6;
        if (valb >= 0) {
            result.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    
    return result;
}

std::string ScreenShotOCR::encodeBase64(const std::vector<unsigned char>& data) {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    for (size_t i = 0; i < data.size(); i += 3) {
        int b = (data[i] & 0xFC) >> 2;
        result += chars[b];
        
        b = (data[i] & 0x03) << 4;
        if (i + 1 < data.size()) {
            b |= (data[i + 1] & 0xF0) >> 4;
            result += chars[b];
            b = (data[i + 1] & 0x0F) << 2;
            if (i + 2 < data.size()) {
                b |= (data[i + 2] & 0xC0) >> 6;
                result += chars[b];
                b = data[i + 2] & 0x3F;
                result += chars[b];
            } else {
                result += chars[b];
                result += '=';
            }
        } else {
            result += chars[b];
            result += "==";
        }
    }
    
    return result;
}

std::string ScreenShotOCR::callYoudaoOCR(const std::string& imgBase64) {
    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    std::string response_data;

    // 在函数开始时声明所有可能使用的变量，避免 goto 跨越初始化
    std::string urlEncodedBase64;
    std::string postData;
    const char* headers;
    BOOL result;

    // 严格按照Python版本：直接对base64进行URL编码
    urlEncodedBase64 = urlEncode(imgBase64);
    
    // 构建请求参数（完全对应Python版本）
    postData = "lang=auto&imgBase=base64," + urlEncodedBase64;

    // 设置请求头（对应Python版本）
    headers = "Content-Type: application/x-www-form-urlencoded\r\n";

    // 使用WinINet进行HTTP请求
    hInternet = InternetOpenA("ScreenShotOCR", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) {
        goto cleanup;
    }
    
    hConnect = InternetConnectA(hInternet, "aidemo.youdao.com", INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        goto cleanup;
    }
    
    hRequest = HttpOpenRequestA(hConnect, "POST", "/ocrapi1", nullptr, nullptr, nullptr, INTERNET_FLAG_SECURE, 0);
    if (!hRequest) {
        goto cleanup;
    }
    
    result = HttpSendRequestA(hRequest, headers, (DWORD)strlen(headers), (LPVOID)postData.c_str(), (DWORD)postData.length());
    
    if (result) {
        char buffer[4096];
        DWORD bytesRead;
        while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            response_data.append(buffer, bytesRead);
        }
    }

cleanup:
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hInternet) InternetCloseHandle(hInternet);
    
    // 严格按照Python版本解析JSON: if 'lines' in result:
    std::string resultText;
    size_t linesPos = response_data.find("\"lines\":");
    if (linesPos != std::string::npos) {
        size_t arrayStart = response_data.find('[', linesPos);
        if (arrayStart != std::string::npos) {
            std::vector<std::string> wordsList;  // 对应Python的words_list = []
            
            size_t pos = arrayStart;
            // 查找每个line中的words字段
            while ((pos = response_data.find("\"words\":", pos)) != std::string::npos) {
                pos += 8; // length of "\"words\":
                // 跳过空格
                while (pos < response_data.length() && (response_data[pos] == ' ' || response_data[pos] == '\t')) {
                    pos++;
                }
                if (pos < response_data.length() && response_data[pos] == '"') {
                    pos++;
                    size_t end = response_data.find('"', pos);
                    if (end != std::string::npos) {
                        std::string word = response_data.substr(pos, end - pos);
                        if (!word.empty()) {
                            // 处理JSON转义字符，将\\转换为\（对应Python的自动JSON解析）
                            std::string unescapedWord = unescapeJsonString(word);
                            wordsList.push_back(unescapedWord);
                        }
                        pos = end + 1;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            
            // 对应Python的 return ' '.join(words_list).strip()
            for (size_t i = 0; i < wordsList.size(); ++i) {
                if (i > 0) resultText += " ";
                resultText += wordsList[i];
            }
        }
    }

    return resultText;
}

// 添加JSON字符串反转义函数
std::string ScreenShotOCR::unescapeJsonString(const std::string& escapedStr) {
    std::string result;
    result.reserve(escapedStr.length());
    
    for (size_t i = 0; i < escapedStr.length(); ++i) {
        if (escapedStr[i] == '\\' && i + 1 < escapedStr.length()) {
            char nextChar = escapedStr[i + 1];
            switch (nextChar) {
                case '\\':
                    result += '\\';
                    i++; // 跳过下一个字符
                    break;
                case '"':
                    result += '"';
                    i++; // 跳过下一个字符
                    break;
                case '/':
                    result += '/';
                    i++; // 跳过下一个字符
                    break;
                case 'b':
                    result += '\b';
                    i++; // 跳过下一个字符
                    break;
                case 'f':
                    result += '\f';
                    i++; // 跳过下一个字符
                    break;
                case 'n':
                    result += '\n';
                    i++; // 跳过下一个字符
                    break;
                case 'r':
                    result += '\r';
                    i++; // 跳过下一个字符
                    break;
                case 't':
                    result += '\t';
                    i++; // 跳过下一个字符
                    break;
                case 'u':
                    // Unicode转义序列处理（简化版本）
                    if (i + 5 < escapedStr.length()) {
                        // 这里简化处理，实际应该解析Unicode
                        result += escapedStr.substr(i, 6);
                        i += 5;
                    } else {
                        result += escapedStr[i];
                    }
                    break;
                default:
                    // 不是标准转义序列，保持原样
                    result += escapedStr[i];
                    break;
            }
        } else {
            result += escapedStr[i];
        }
    }
    
    return result;
}

std::string urlEncode(const std::string& str) {
    std::string encoded;
    char hex[4];
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = (unsigned char)str[i];
        if ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            sprintf_s(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

void ScreenShotOCR::copyToClipboard(const std::string& text) {
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        
        // 使用 Utf8ToWide 进行转换
        std::wstring wtext = Utf8ToWide(text);
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (wtext.length() + 1) * sizeof(wchar_t));
        if (hGlobal) {
            wchar_t* pGlobal = (wchar_t*)GlobalLock(hGlobal);
            if (pGlobal) { // 检查 GlobalLock 是否成功
                wcscpy_s(pGlobal, wtext.length() + 1, wtext.c_str());
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            } else {
                GlobalFree(hGlobal); // 如果 GlobalLock 失败，释放内存
            }
        }
        
        CloseClipboard();
    }
}

void ScreenShotOCR::showToast(const std::string& message, int duration) {
    std::thread([message, duration]() {
        ToastWindow toast(message, duration);
        toast.show();
    }).detach();
}

void ScreenShotOCR::closeOverlay() {
    if (overlayWindow) {
        PostMessage(overlayWindow, WM_CLOSE, 0, 0);
    }
}

void ScreenShotOCR::run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
