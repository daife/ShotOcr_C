#include "../include/HotkeyManager.h"
#include "../include/AppManager.h"
#include "../include/ScreenCapture.h"
#include "../include/VoiceRecognizer.h"
#include <thread>

HotkeyManager* HotkeyManager::instance = nullptr;

HotkeyManager::HotkeyManager(AppManager* app) 
    : appManager(app), keyboardHook(nullptr) {
    instance = this;
}

HotkeyManager::~HotkeyManager() {
    stopListening();
    instance = nullptr;
}

void HotkeyManager::startListening() {
    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
}

void HotkeyManager::stopListening() {
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = nullptr;
    }
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN && instance && instance->appManager) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        // 如果语音识别正在录音，只处理特定按键
        if (instance->appManager->voiceRecognizer && 
            instance->appManager->voiceRecognizer->keyListeningActive) {
            // 允许ESC、右键和空格键通过，由VoiceRecognizer处理
            if (kb->vkCode == VK_ESCAPE || kb->vkCode == VK_RBUTTON || kb->vkCode == VK_SPACE) {
                return CallNextHookEx(instance->keyboardHook, nCode, wParam, lParam);
            }
            // 其他键不传递给系统，阻止其他操作
            return 1;
        }
        
        if (instance->isCtrlShiftPressed()) {
            // Ctrl+Shift+S - 截图OCR
            if (kb->vkCode == 'S') {
                std::thread([](){ 
                    if (instance && instance->appManager && instance->appManager->screenCapture) {
                        instance->appManager->screenCapture->startCapture(); 
                    }
                }).detach();
                return 1;
            }
            // Ctrl+Shift+H - 语音识别
            else if (kb->vkCode == 'H') {
                std::thread([](){ 
                    if (instance && instance->appManager && instance->appManager->voiceRecognizer) {
                        instance->appManager->voiceRecognizer->startRecording(); 
                    }
                }).detach();
                return 1;
            }
        }
    }
    
    return CallNextHookEx(instance ? instance->keyboardHook : nullptr, nCode, wParam, lParam);
}

bool HotkeyManager::isCtrlShiftPressed() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
}
