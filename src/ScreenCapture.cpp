#include "../include/ScreenCapture.h"
#include "../include/AppManager.h"
#include "../include/StringUtils.h"
#include <thread>
#include <gdiplus.h>
#include <wininet.h>
#include <shlwapi.h>
#include <vector>
#include <sstream>
#include <iostream>
#include <windowsx.h>
#include <algorithm>
#include <cstdlib>

// 链接库只在 MSVC 编译器下有效
#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

ScreenCapture::ScreenCapture(AppManager* app) 
    : appManager(app), overlayWindow(nullptr),
      startX(0), startY(0), endX(0), endY(0), dragging(false), windowCreated(false) {
    
    // 获取真实屏幕尺寸（不受DPI缩放影响）
    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);
}

ScreenCapture::~ScreenCapture() {
    closeOverlay();
}

void ScreenCapture::startCapture() {
    if (windowCreated) return;
    
    createOverlayWindow();
}

void ScreenCapture::createOverlayWindow() {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = OverlayWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ScreenCaptureOverlay";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
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

void ScreenCapture::closeOverlay() {
    if (overlayWindow) {
        PostMessage(overlayWindow, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK ScreenCapture::OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ScreenCapture* capture = nullptr;
    
    if (uMsg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        capture = static_cast<ScreenCapture*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(capture));
    } else {
        capture = reinterpret_cast<ScreenCapture*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    switch (uMsg) {
    case WM_LBUTTONDOWN:
        if (capture) capture->onMousePress(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (capture && capture->dragging) {
            capture->onMouseDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (capture) capture->onMouseRelease(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONDOWN:
        if (capture) capture->closeOverlay();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && capture) capture->closeOverlay();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        // 绘制全屏遮罩
        HBRUSH grayBrush = CreateSolidBrush(RGB(128, 128, 128));
        RECT fullRect = {0, 0, capture ? capture->screenWidth : 0, capture ? capture->screenHeight : 0};
        FillRect(hdc, &fullRect, grayBrush);
        DeleteObject(grayBrush);
        
        // 如果正在拖拽，绘制红色选择框
        if (capture && capture->dragging) {
            HPEN redPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
            HPEN oldPen = (HPEN)SelectObject(hdc, redPen);
            SetBkMode(hdc, TRANSPARENT);
            
            POINT startPt = {capture->startX, capture->startY};
            POINT endPt = {capture->endX, capture->endY};
            ScreenToClient(hwnd, &startPt);
            ScreenToClient(hwnd, &endPt);
            
            int x1 = (std::min)(startPt.x, endPt.x);
            int y1 = (std::min)(startPt.y, endPt.y);
            int x2 = (std::max)(startPt.x, endPt.x);
            int y2 = (std::max)(startPt.y, endPt.y);
            
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
        if (capture) {
            capture->windowCreated = false;
            capture->overlayWindow = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ScreenCapture::onMousePress(int x, int y) {
    POINT pt = {x, y};
    ClientToScreen(overlayWindow, &pt);
    
    startX = pt.x;
    startY = pt.y;
    dragging = true;
}

void ScreenCapture::onMouseDrag(int x, int y) {
    if (!dragging) return;
    
    POINT pt = {x, y};
    ClientToScreen(overlayWindow, &pt);
    
    endX = pt.x;
    endY = pt.y;
}

void ScreenCapture::onMouseRelease(int x, int y) {
    if (!dragging) return;
    
    dragging = false;
    
    POINT pt = {x, y};
    ClientToScreen(overlayWindow, &pt);
    
    endX = pt.x;
    endY = pt.y;
    
    int x1 = (std::min)(startX, endX);
    int y1 = (std::min)(startY, endY);
    int x2 = (std::max)(startX, endX);
    int y2 = (std::max)(startY, endY);
    
    if (std::abs(x2 - x1) > 10 && std::abs(y2 - y1) > 10) {
        std::thread([this, x1, y1, x2, y2]() {
            captureAndOCR(x1, y1, x2, y2);
        }).detach();
    } else {
        closeOverlay();
    }
}

void ScreenCapture::captureAndOCR(int x1, int y1, int x2, int y2) {
    ShowWindow(overlayWindow, SW_HIDE);
    Sleep(200);
    
    try {
        std::string imgBase64 = captureScreenRegion(x1, y1, x2 - x1, y2 - y1);
        std::string ocrText = callYoudaoOCR(imgBase64);
        
        if (!ocrText.empty()) {
            size_t start = ocrText.find_first_not_of(" \t\r\n");
            size_t end = ocrText.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                std::string cleanedText = ocrText.substr(start, end - start + 1);
                copyToClipboard(cleanedText);
                appManager->showToast("识别成功！已复制到剪贴板");
            } else {
                appManager->showToast("识别失败，未检测到文字");
            }
        } else {
            appManager->showToast("识别失败，未检测到文字");
        }
    } catch (...) {
        appManager->showToast("处理失败，请检查网络连接");
    }
    
    closeOverlay();
}

std::string ScreenCapture::captureScreenRegion(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return "";
    
    HDC screenDC = CreateDC("DISPLAY", nullptr, nullptr, nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);
    
    BitBlt(memDC, 0, 0, width, height, screenDC, x, y, SRCCOPY);
    
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
    
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    DeleteDC(screenDC);
    
    return encodeBase64(pngData);
}

std::string ScreenCapture::callYoudaoOCR(const std::string& imgBase64) {
    HINTERNET hInternet = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;
    std::string response_data;
    
    std::string urlEncodedBase64;
    std::string postData;
    const char* headers;
    BOOL result;
    
    // URL编码
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
    for (char c : imgBase64) {
        if (strchr(chars, c)) {
            urlEncodedBase64 += c;
        } else {
            char hex[4];
            sprintf_s(hex, sizeof(hex), "%%%02X", (unsigned char)c);
            urlEncodedBase64 += hex;
        }
    }
    
    postData = "lang=auto&imgBase=base64," + urlEncodedBase64;
    headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    
    hInternet = InternetOpenA("ScreenCapture", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) goto cleanup;
    
    hConnect = InternetConnectA(hInternet, "aidemo.youdao.com", INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) goto cleanup;
    
    hRequest = HttpOpenRequestA(hConnect, "POST", "/ocrapi1", nullptr, nullptr, nullptr, INTERNET_FLAG_SECURE, 0);
    if (!hRequest) goto cleanup;
    
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
    
    // 解析JSON结果
    std::string resultText;
    size_t linesPos = response_data.find("\"lines\":");
    if (linesPos != std::string::npos) {
        size_t arrayStart = response_data.find('[', linesPos);
        if (arrayStart != std::string::npos) {
            std::vector<std::string> wordsList;
            
            size_t pos = arrayStart;
            while ((pos = response_data.find("\"words\":", pos)) != std::string::npos) {
                pos += 8;
                while (pos < response_data.length() && (response_data[pos] == ' ' || response_data[pos] == '\t')) {
                    pos++;
                }
                if (pos < response_data.length() && response_data[pos] == '"') {
                    pos++;
                    size_t end = response_data.find('"', pos);
                    if (end != std::string::npos) {
                        std::string word = response_data.substr(pos, end - pos);
                        if (!word.empty()) {
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
            
            for (size_t i = 0; i < wordsList.size(); ++i) {
                if (i > 0) resultText += " ";
                resultText += wordsList[i];
            }
        }
    }

    return resultText;
}

std::string ScreenCapture::encodeBase64(const std::vector<unsigned char>& data) {
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

std::string ScreenCapture::unescapeJsonString(const std::string& escapedStr) {
    std::string result;
    result.reserve(escapedStr.length());
    
    for (size_t i = 0; i < escapedStr.length(); ++i) {
        if (escapedStr[i] == '\\' && i + 1 < escapedStr.length()) {
            char nextChar = escapedStr[i + 1];
            switch (nextChar) {
                case '\\': result += '\\'; i++; break;
                case '"': result += '"'; i++; break;
                case '/': result += '/'; i++; break;
                case 'b': result += '\b'; i++; break;
                case 'f': result += '\f'; i++; break;
                case 'n': result += '\n'; i++; break;
                case 'r': result += '\r'; i++; break;
                case 't': result += '\t'; i++; break;
                case 'u':
                    if (i + 5 < escapedStr.length()) {
                        result += escapedStr.substr(i, 6);
                        i += 5;
                    } else {
                        result += escapedStr[i];
                    }
                    break;
                default:
                    result += escapedStr[i];
                    break;
            }
        } else {
            result += escapedStr[i];
        }
    }
    
    return result;
}

void ScreenCapture::copyToClipboard(const std::string& text) {
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        
        std::wstring wtext = Utf8ToWide(text);
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (wtext.length() + 1) * sizeof(wchar_t));
        if (hGlobal) {
            wchar_t* pGlobal = (wchar_t*)GlobalLock(hGlobal);
            if (pGlobal) {
                wcscpy_s(pGlobal, wtext.length() + 1, wtext.c_str());
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            } else {
                GlobalFree(hGlobal);
            }
        }
        
        CloseClipboard();
    }
}
