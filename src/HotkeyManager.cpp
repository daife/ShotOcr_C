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
    if (nCode >= 0 && instance && instance->appManager) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        // 检查是否在截图状态
        bool isCapturing = instance->appManager->screenCapture && 
                          instance->appManager->screenCapture->windowCreated;
        
        // 检查是否在录音状态
        bool isRecording = instance->appManager->voiceRecognizer && 
                          instance->appManager->voiceRecognizer->keyListeningActive;
        
        // 处理鼠标右键 - 在录音和截图状态下阻止传递给第三方应用
        if (wParam == WM_RBUTTONDOWN) {
            if (isRecording || isCapturing) {
                return 1; // 阻止右键传递给第三方应用
            }
        }
        
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // 如果语音识别正在录音，只阻止非控制按键传递给第三方应用
            if (isRecording) {
                // ESC、空格键需要正常传递，由VoiceRecognizer处理
                if (kb->vkCode == VK_ESCAPE || kb->vkCode == VK_SPACE) {
                    return CallNextHookEx(instance->keyboardHook, nCode, wParam, lParam);
                }
                // 阻止其他按键传递给第三方应用，避免误操作
                return 1;
            }
            
            // 如果正在截图，ESC键需要正常传递，由ScreenCapture处理
            if (isCapturing) {
                if (kb->vkCode == VK_ESCAPE) {
                    return CallNextHookEx(instance->keyboardHook, nCode, wParam, lParam);
                }
                // 阻止其他按键传递给第三方应用
                return 1;
            }
            
            // 正常的快捷键处理
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
    }
    
    return CallNextHookEx(instance ? instance->keyboardHook : nullptr, nCode, wParam, lParam);
}

bool HotkeyManager::isCtrlShiftPressed() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
}
