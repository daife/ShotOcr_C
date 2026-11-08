#include "service.h"
#include <windowsx.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#ifndef _WIN64
#error ShotOcr supports Windows x64 only
#endif
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT TrayMessage = WM_APP + 1, DoneMessage = WM_APP + 2, CancelMessage = WM_APP + 3, StopMessage = WM_APP + 4;
enum class State { Idle, Selecting, Recording, Recognizing, Pasting };
struct Job { unsigned id; Bytes data; bool voice, english; std::atomic_bool cancelled{false}; };
struct Result { std::shared_ptr<Job> job; std::wstring text, error; };
struct Target {
    HWND root = nullptr, focus = nullptr;
    bool editable = false;
    ComPtr<IUIAutomationElement> element;
};
struct App {
    HINSTANCE instance{}; HWND host{}, overlay{}, tip{};
    HHOOK keyboard{}, mouse{}; NOTIFYICONDATAW tray{};
    State state = State::Idle; bool english = false, dragging = false;
    bool swallowSpace = false, swallowEscape = false, swallowRight = false;
    unsigned serial = 0;
    Target target;
    ComPtr<IUIAutomation> automation;
    HBITMAP desktop{}; int x{}, y{}, width{}, height{}; POINT anchor{}, cursor{};
    HWAVEIN input{}; WAVEHDR audioHeader{}; Bytes audio;
    ULONGLONG recordingStart{}, tipUntil{};
    std::wstring tipText, pending;
    DWORD clipboardSequence{};
    bool clipboardWritten = false;
    std::shared_ptr<Job> active;
    std::mutex mutex; std::condition_variable cv; std::deque<std::shared_ptr<Job>> queue;
    bool quitting = false; std::thread worker;
} a;

bool busy() { return a.state != State::Idle; }
bool modifiersDown() {
    for (int k : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN}) if (GetAsyncKeyState(k) & 0x8000) return true;
    return false;
}
bool pasteKeysDown() { return modifiersDown() || (GetAsyncKeyState('V') & 0x8000); }

void tip(const std::wstring& text, unsigned duration = 0) {
    a.tipText = text; a.tipUntil = duration ? GetTickCount64() + duration : 0;
    MONITORINFO mi{sizeof(mi)}; POINT p{}; GetCursorPos(&p);
    GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    UINT dpi = GetDpiForWindow(a.tip); int w = MulDiv(420, dpi, 96), h = MulDiv(48, dpi, 96);
    SetWindowPos(a.tip, HWND_TOPMOST, (mi.rcWork.left + mi.rcWork.right - w) / 2, mi.rcWork.bottom - h - 32, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(a.tip, nullptr, FALSE);
}
void dropDesktop() { if (a.desktop) { DeleteObject(a.desktop); a.desktop = nullptr; } }
Target rememberTarget() {
    Target t; t.root = GetForegroundWindow();
    GUITHREADINFO gi{sizeof(gi)};
    if (GetGUIThreadInfo(GetWindowThreadProcessId(t.root, nullptr), &gi)) {
        t.focus = gi.hwndFocus; t.editable = gi.hwndCaret != nullptr;
    }
    if (a.automation && SUCCEEDED(a.automation->GetFocusedElement(&t.element)) && t.element) {
        CONTROLTYPEID type{}; t.element->get_CurrentControlType(&type);
        ComPtr<IUIAutomationValuePattern> value;
        if (SUCCEEDED(t.element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value) {
            BOOL readonly = TRUE; value->get_CurrentIsReadOnly(&readonly);
            t.editable = t.editable || !readonly;
        }
        if (!value && type == UIA_EditControlTypeId) t.editable = true;
        ComPtr<IUIAutomationTextPattern2> text;
        if (SUCCEEDED(t.element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text))) && text) {
            BOOL active = FALSE; ComPtr<IUIAutomationTextRange> caret;
            if (SUCCEEDED(text->GetCaretRange(&active, &caret)) && active) t.editable = true;
        }
    }
    // Desktop/shell surfaces have no input target even when their view owns focus.
    if (!t.root || t.root == GetDesktopWindow() || t.root == GetShellWindow()) t.editable = false;
    return t;
}
void restoreTarget() {
    if (!IsWindow(a.target.root)) return;
    SetForegroundWindow(a.target.root);
    DWORD thread = GetWindowThreadProcessId(a.target.root, nullptr), current = GetCurrentThreadId();
    if (GetForegroundWindow() == a.target.root && IsWindow(a.target.focus)) {
        bool attached = thread != current && AttachThreadInput(current, thread, TRUE);
        if (thread == current || attached) SetFocus(a.target.focus);
        if (attached) AttachThreadInput(current, thread, FALSE);
    }
}
bool targetStillFocused() {
    if (!a.target.editable || !IsWindow(a.target.root) || GetForegroundWindow() != a.target.root) return false;
    GUITHREADINFO gi{sizeof(gi)};
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(a.target.root, nullptr), &gi) || gi.hwndFocus != a.target.focus) return false;
    if (a.target.element && a.automation) {
        ComPtr<IUIAutomationElement> now; BOOL same = FALSE;
        if (FAILED(a.automation->GetFocusedElement(&now)) || !now || FAILED(a.automation->CompareElements(a.target.element.Get(), now.Get(), &same)) || !same) return false;
    }
    return true;
}
void closeSelection() {
    a.dragging = false;
    ShowWindow(a.overlay, SW_HIDE); if (GetCapture() == a.overlay) ReleaseCapture();
    restoreTarget();
}
Bytes stopAudio(bool keep) {
    Bytes result;
    if (!a.input) return result;
    waveInReset(a.input);
    DWORD n = std::min<DWORD>(a.audioHeader.dwBytesRecorded, 16000 * 2 * 60);
    if (keep && n >= 2) result.assign(a.audio.begin(), a.audio.begin() + (n & ~1u));
    waveInUnprepareHeader(a.input, &a.audioHeader, sizeof(a.audioHeader)); waveInClose(a.input);
    a.input = nullptr; a.audio.clear(); a.audioHeader = {};
    return result;
}
void idle() {
    a.state = State::Idle; a.active.reset(); a.pending.clear(); a.clipboardWritten = false;
    a.target = {};
}
void cancel() {
    if (!busy()) return;
    if (a.active) a.active->cancelled = true;
    if (a.state == State::Selecting) closeSelection();
    stopAudio(false); dropDesktop(); ++a.serial; idle(); tip(L"已取消", 900);
}
void failed(const std::wstring& message) {
    if (a.active) a.active->cancelled = true;
    idle(); tip(message, 4000);
}
std::wstring errorText(const char* error) {
    int n = MultiByteToWideChar(CP_UTF8, 0, error, -1, nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, error, -1, w.data(), n);
    if (!w.empty()) w.pop_back(); return L"识别失败：" + w;
}
void submit(Bytes bytes, bool voice) {
    a.active = std::make_shared<Job>(); a.active->id = ++a.serial;
    a.active->data = std::move(bytes); a.active->voice = voice; a.active->english = a.english;
    a.state = State::Recognizing; tip(L"正在识别…  Esc / 右键取消");
    { std::lock_guard<std::mutex> lock(a.mutex); a.queue.push_back(a.active); }
    a.cv.notify_one();
}
void worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        std::shared_ptr<Job> job;
        { std::unique_lock<std::mutex> lock(a.mutex); a.cv.wait(lock, [] { return a.quitting || !a.queue.empty(); });
          if (a.quitting) break; job = a.queue.front(); a.queue.pop_front(); }
        if (job->cancelled) continue;
        auto result = std::make_unique<Result>(); result->job = job;
        try { result->text = recognize(job->data, job->voice, job->english, job->cancelled); }
        catch (const std::exception& e) { result->error = errorText(e.what()); }
        job->data.clear();
        if (!job->cancelled && PostMessageW(a.host, DoneMessage, 0, (LPARAM)result.get())) result.release();
    }
    CoUninitialize();
}
void startCapture() {
    if (busy()) return;
    a.target = rememberTarget(); ShowWindow(a.tip, SW_HIDE);
    a.x = GetSystemMetrics(SM_XVIRTUALSCREEN); a.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    a.width = GetSystemMetrics(SM_CXVIRTUALSCREEN); a.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    a.desktop = CreateCompatibleBitmap(screen, a.width, a.height);
    HGDIOBJ old = SelectObject(memory, a.desktop);
    BOOL ok = a.desktop && BitBlt(memory, 0, 0, a.width, a.height, screen, a.x, a.y, SRCCOPY | CAPTUREBLT);
    SelectObject(memory, old); DeleteDC(memory); ReleaseDC(nullptr, screen);
    if (!ok) { dropDesktop(); failed(L"截图失败"); return; }
    a.state = State::Selecting; a.dragging = false;
    SetWindowPos(a.overlay, HWND_TOPMOST, a.x, a.y, a.width, a.height, SWP_SHOWWINDOW);
    SetForegroundWindow(a.overlay); SetFocus(a.overlay); InvalidateRect(a.overlay, nullptr, FALSE);
}
void startRecording() {
    if (busy()) return;
    a.target = rememberTarget();
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
    if (waveInOpen(&a.input, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        a.input = nullptr; failed(L"无法打开麦克风（16kHz 单声道 PCM）"); return;
    }
    a.audio.resize(16000 * 2 * 60); a.audioHeader = {};
    a.audioHeader.lpData = (LPSTR)a.audio.data(); a.audioHeader.dwBufferLength = (DWORD)a.audio.size();
    if (waveInPrepareHeader(a.input, &a.audioHeader, sizeof(a.audioHeader)) != MMSYSERR_NOERROR ||
        waveInAddBuffer(a.input, &a.audioHeader, sizeof(a.audioHeader)) != MMSYSERR_NOERROR ||
        waveInStart(a.input) != MMSYSERR_NOERROR) {
        stopAudio(false); failed(L"麦克风录音失败"); return;
    }
    a.recordingStart = GetTickCount64(); a.state = State::Recording;
    tip(a.english ? L"录音中 · 英文  |  空格结束  Esc / 右键取消" : L"录音中 · 中文  |  空格结束  Esc / 右键取消");
}
void finishRecording() {
    if (a.state != State::Recording) return;
    Bytes pcm = stopAudio(true);
    if (pcm.empty()) { failed(L"没有录到声音"); return; }
    submit(wav(pcm), true);
}
bool writeClipboard(const std::wstring& text) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!memory) return false;
    void* ptr = GlobalLock(memory);
    if (!ptr) { GlobalFree(memory); return false; }
    memcpy(ptr, text.c_str(), (text.size() + 1) * sizeof(wchar_t)); GlobalUnlock(memory);
    if (!OpenClipboard(a.host)) { GlobalFree(memory); return false; }
    bool ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard(); if (!ok) GlobalFree(memory);
    return ok;
}
void deliver() {
    if (a.state != State::Pasting || !a.active || a.active->cancelled) return;
    if (a.target.editable && targetStillFocused() && pasteKeysDown()) return;
    // Commit clipboard once; a failed write must never paste stale contents.
    if (!a.clipboardWritten) {
        if (!writeClipboard(a.pending)) { failed(L"剪贴板写入失败"); return; }
        a.clipboardWritten = true; a.clipboardSequence = GetClipboardSequenceNumber();
    }
    if (!a.target.editable || !targetStillFocused() || GetClipboardSequenceNumber() != a.clipboardSequence) {
        idle(); tip(L"已复制", 1600); return;
    }
    if (pasteKeysDown()) return;
    INPUT keys[4]{};
    keys[0].type = keys[1].type = keys[2].type = keys[3].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_CONTROL; keys[1].ki.wVk = 'V';
    keys[2].ki.wVk = 'V'; keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3].ki.wVk = VK_CONTROL; keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    UINT sent = SendInput(4, keys, sizeof(INPUT));
    idle(); tip(sent == 4 ? L"已复制并粘贴" : L"已复制，自动粘贴未完成", 1800);
}
LRESULT CALLBACK keyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto& key = *(KBDLLHOOKSTRUCT*)lp;
        if (!(key.flags & LLKHF_INJECTED)) {
            bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
            bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
            if (key.vkCode == VK_ESCAPE) {
                if (down && busy()) { a.swallowEscape = true; if (a.active) a.active->cancelled = true; PostMessageW(a.host, CancelMessage, 0, 0); return 1; }
                if (up && a.swallowEscape) { a.swallowEscape = false; return 1; }
            }
            if (key.vkCode == VK_SPACE) {
                if (down && (a.state == State::Recording || a.swallowSpace)) {
                    if (!a.swallowSpace) PostMessageW(a.host, StopMessage, 0, 0);
                    a.swallowSpace = true; return 1;
                }
                if (up && a.swallowSpace) { a.swallowSpace = false; return 1; }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
LRESULT CALLBACK mouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && !(((MSLLHOOKSTRUCT*)lp)->flags & LLMHF_INJECTED)) {
        if (wp == WM_RBUTTONDOWN && busy()) {
            a.swallowRight = true; if (a.active) a.active->cancelled = true;
            PostMessageW(a.host, CancelMessage, 0, 0); return 1;
        }
        if (wp == WM_RBUTTONUP && a.swallowRight) { a.swallowRight = false; return 1; }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
RECT selection() { return {std::min(a.anchor.x, a.cursor.x), std::min(a.anchor.y, a.cursor.y), std::max(a.anchor.x, a.cursor.x), std::max(a.anchor.y, a.cursor.y)}; }
LRESULT CALLBACK overlayProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_CROSS)); return TRUE;
    case WM_LBUTTONDOWN:
        a.anchor = a.cursor = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; a.dragging = true; SetCapture(hwnd); return 0;
    case WM_MOUSEMOVE:
        if (a.dragging) { a.cursor = {std::clamp(GET_X_LPARAM(lp), 0, a.width), std::clamp(GET_Y_LPARAM(lp), 0, a.height)}; InvalidateRect(hwnd, nullptr, FALSE); } return 0;
    case WM_LBUTTONUP:
        if (a.dragging) {
            a.cursor = {std::clamp(GET_X_LPARAM(lp), 0, a.width), std::clamp(GET_Y_LPARAM(lp), 0, a.height)};
            RECT r = selection(); closeSelection();
            if (r.right <= r.left || r.bottom <= r.top) { dropDesktop(); idle(); return 0; }
            try { Bytes bytes = png(a.desktop, r); dropDesktop(); submit(std::move(bytes), false); }
            catch (const std::exception& e) { dropDesktop(); failed(errorText(e.what())); }
        } return 0;
    case WM_CAPTURECHANGED: if (a.dragging && a.state == State::Selecting) cancel(); return 0;
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE && a.state == State::Selecting && a.dragging) cancel(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        if (a.desktop) {
            HDC source = CreateCompatibleDC(dc); auto old = SelectObject(source, a.desktop);
            HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, a.width, a.height); auto oldBuffer = SelectObject(buffer, bitmap);
            BitBlt(buffer, 0, 0, a.width, a.height, source, 0, 0, SRCCOPY);
            HDC black = CreateCompatibleDC(dc); HBITMAP pixel = CreateCompatibleBitmap(dc, 1, 1); auto oldBlack = SelectObject(black, pixel); SetPixel(black, 0, 0, RGB(0, 0, 0));
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 90, 0}; AlphaBlend(buffer, 0, 0, a.width, a.height, black, 0, 0, 1, 1, blend);
            SelectObject(black, oldBlack); DeleteObject(pixel); DeleteDC(black);
            if (a.dragging) {
                RECT r = selection(); BitBlt(buffer, r.left, r.top, r.right - r.left, r.bottom - r.top, source, r.left, r.top, SRCCOPY);
                HBRUSH brush = CreateSolidBrush(RGB(72, 180, 255)); FrameRect(buffer, &r, brush); DeleteObject(brush);
            }
            BitBlt(dc, 0, 0, a.width, a.height, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, oldBuffer); DeleteObject(bitmap); DeleteDC(buffer); SelectObject(source, old); DeleteDC(source);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
LRESULT CALLBACK tipProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        HBRUSH brush = CreateSolidBrush(RGB(30, 34, 42)); FillRect(dc, &r, brush); DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(245, 247, 250));
        HFONT font = CreateFontW(-MulDiv(14, GetDpiForWindow(hwnd), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        auto old = SelectObject(dc, font); r.left += 10; r.right -= 10;
        DrawTextW(dc, a.tipText.c_str(), -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, old); DeleteObject(font); EndPaint(hwnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
void menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 10, L"截图识别  Ctrl+Shift+S");
    AppendMenuW(menu, MF_STRING, 11, L"语音识别  Ctrl+Shift+H");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!a.english ? MF_CHECKED : 0), 12, L"语音：中文");
    AppendMenuW(menu, MF_STRING | (a.english ? MF_CHECKED : 0), 13, L"语音：English");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, 14, L"退出");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(a.host);
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, a.host, nullptr);
    DestroyMenu(menu); PostMessageW(a.host, WM_NULL, 0, 0);
    // Menu invocation has no original input target. Global hotkeys preserve it.
    if (id == 10) startCapture(); if (id == 11) startRecording();
    if (id == 12 || id == 13) {
        a.english = id == 13;
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ShotOcr", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            DWORD value = a.english; RegSetValueExW(key, L"English", 0, REG_DWORD, (BYTE*)&value, sizeof(value)); RegCloseKey(key);
        }
    }
    if (id == 14) DestroyWindow(a.host);
}
LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (message == taskbarCreated) { Shell_NotifyIconW(NIM_ADD, &a.tray); return 0; }
    switch (message) {
    case WM_HOTKEY: if (wp == 1) startCapture(); if (wp == 2) startRecording(); return 0;
    case CancelMessage: cancel(); return 0;
    case StopMessage: finishRecording(); return 0;
    case TrayMessage: if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) menu(); return 0;
    case DoneMessage: {
        std::unique_ptr<Result> result((Result*)lp);
        if (!a.active || result->job != a.active || result->job->cancelled || result->job->id != a.serial) return 0;
        if (!result->error.empty()) { failed(result->error); return 0; }
        a.pending = std::move(result->text); a.state = State::Pasting; a.clipboardWritten = false;
        tip(L"识别完成，等待粘贴…  Esc / 右键取消"); return 0;
    }
    case WM_TIMER:
        if (a.state == State::Recording && (GetTickCount64() - a.recordingStart >= 60000 || (a.audioHeader.dwFlags & WHDR_DONE))) finishRecording();
        if (a.state == State::Pasting) deliver();
        if (a.tipUntil && GetTickCount64() >= a.tipUntil) { ShowWindow(a.tip, SW_HIDE); a.tipUntil = 0; }
        return 0;
    case WM_DESTROY: cancel(); Shell_NotifyIconW(NIM_DELETE, &a.tray); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\ShotOcr.Win32.x64");
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) { if (single) CloseHandle(single); return 0; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); a.instance = instance;
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&a.automation));
    DWORD english{}, length = sizeof(english);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ShotOcr", L"English", RRF_RT_REG_DWORD, nullptr, &english, &length) == ERROR_SUCCESS) a.english = english != 0;
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = hostProc; wc.lpszClassName = L"ShotOcr.Host";
    RegisterClassW(&wc); wc.lpfnWndProc = overlayProc; wc.lpszClassName = L"ShotOcr.Selection"; wc.hCursor = LoadCursorW(nullptr, IDC_CROSS); RegisterClassW(&wc);
    wc.lpfnWndProc = tipProc; wc.lpszClassName = L"ShotOcr.Tip"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    a.host = CreateWindowW(L"ShotOcr.Host", L"ShotOcr", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    a.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"ShotOcr.Selection", L"ShotOcr", WS_POPUP, 0, 0, 0, 0, a.host, nullptr, instance, nullptr);
    a.tip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"ShotOcr.Tip", L"ShotOcr", WS_POPUP, 0, 0, 0, 0, a.host, nullptr, instance, nullptr);
    a.tray.cbSize = sizeof(a.tray); a.tray.hWnd = a.host; a.tray.uID = 1;
    a.tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; a.tray.uCallbackMessage = TrayMessage;
    a.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wcscpy_s(a.tray.szTip, L"ShotOcr · Ctrl+Shift+S 截图 · Ctrl+Shift+H 语音");
    a.keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, instance, 0);
    a.mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, instance, 0);
    bool ready = a.host && a.overlay && a.tip && a.keyboard && a.mouse && Shell_NotifyIconW(NIM_ADD, &a.tray);
    bool shot = RegisterHotKey(a.host, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S');
    bool voice = RegisterHotKey(a.host, 2, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'H');
    if (!ready || !shot || !voice) {
        UnregisterHotKey(a.host, 1); UnregisterHotKey(a.host, 2);
        // Startup failures remain visible without a confirmation dialog.
        a.tray.uFlags |= NIF_INFO; wcscpy_s(a.tray.szInfoTitle, L"ShotOcr 无法启动");
        wcscpy_s(a.tray.szInfo, !shot || !voice ? L"Ctrl+Shift+S 或 Ctrl+Shift+H 已被占用。退出占用程序后重新启动。" : L"托盘、窗口或输入钩子初始化失败。");
        a.tray.dwInfoFlags = NIIF_ERROR; Shell_NotifyIconW(NIM_MODIFY, &a.tray);
        tip(a.tray.szInfo, 5000); SetTimer(a.host, 2, 5000, [](HWND h, UINT, UINT_PTR, DWORD) { DestroyWindow(h); });
    } else { a.worker = std::thread(worker); }
    SetTimer(a.host, 1, 15, nullptr);
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (a.keyboard) UnhookWindowsHookEx(a.keyboard); if (a.mouse) UnhookWindowsHookEx(a.mouse);
    UnregisterHotKey(a.host, 1); UnregisterHotKey(a.host, 2);
    { std::lock_guard<std::mutex> lock(a.mutex); a.quitting = true; for (auto& job : a.queue) job->cancelled = true; }
    a.cv.notify_all(); if (a.worker.joinable()) a.worker.join();
    while (PeekMessageW(&msg, nullptr, DoneMessage, DoneMessage, PM_REMOVE)) delete (Result*)msg.lParam;
    DestroyWindow(a.tip); DestroyWindow(a.overlay); dropDesktop(); a.automation.Reset();
    if (SUCCEEDED(com)) CoUninitialize(); CloseHandle(single); return 0;
}
