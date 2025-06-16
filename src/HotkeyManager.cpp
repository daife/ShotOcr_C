#include "../include/HotkeyManager.h"
#include "../include/AppManager.h"
#include "../include/ScreenCapture.h"
#include "../include/VoiceRecognizer.h"
#include <thread>

HotkeyManager* HotkeyManager::instance = nullptr;

HotkeyManager::HotkeyManager(AppManager* app) 
    : appManager(app), keyboardHook(nullptr), mouseHook(nullptr) {
    instance = this;
}

HotkeyManager::~HotkeyManager() {
    stopListening();
    instance = nullptr;
}

void HotkeyManager::startListening() {
    // 安装键盘钩子
    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
    
    // 安装鼠标钩子
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(nullptr), 0);
}

void HotkeyManager::stopListening() {
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = nullptr;
    }
    if (mouseHook) {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = nullptr;
    }
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && instance && instance->appManager) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        // 只处理按键按下事件
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // 检查是否在截图状态
            bool isCapturing = instance->appManager->screenCapture && 
                              instance->appManager->screenCapture->windowCreated;
            
            // 检查是否在录音状态
            bool isRecording = instance->appManager->voiceRecognizer && 
                              instance->appManager->voiceRecognizer->keyListeningActive;
            
            // 截图状态下的按键处理
            if (isCapturing) {
                // ESC键交给ScreenCapture处理，其他按键拦截避免误操作
                if (kb->vkCode == VK_ESCAPE) {
                    // 通知ScreenCapture处理按键
                    std::thread([vkCode = kb->vkCode]() {
                        if (instance && instance->appManager && instance->appManager->screenCapture) {
                            instance->appManager->screenCapture->onKeyPressed(vkCode);
                        }
                    }).detach();
                    
                    return 1; // 拦截ESC键
                }
                
                // 拦截其他按键，避免误操作（除了Ctrl、Shift等修饰键）
                if (kb->vkCode != VK_LCONTROL && kb->vkCode != VK_RCONTROL &&
                    kb->vkCode != VK_LSHIFT && kb->vkCode != VK_RSHIFT &&
                    kb->vkCode != VK_LMENU && kb->vkCode != VK_RMENU) {
                    return 1;
                }
            }
            
            // 录音状态下的按键处理
            if (isRecording) {
                // ESC、空格键交给VoiceRecognizer处理，其他按键拦截
                if (kb->vkCode == VK_ESCAPE || kb->vkCode == VK_SPACE) {
                    
                    // 通知VoiceRecognizer处理按键
                    std::thread([vkCode = kb->vkCode]() {
                        if (instance && instance->appManager && instance->appManager->voiceRecognizer) {
                            instance->appManager->voiceRecognizer->onKeyPressed(vkCode);
                        }
                    }).detach();
                    
                    return 1; // 拦截按键
                }
                
                // 拦截其他按键，避免误操作
                if (kb->vkCode != VK_LCONTROL && kb->vkCode != VK_RCONTROL &&
                    kb->vkCode != VK_LSHIFT && kb->vkCode != VK_RSHIFT &&
                    kb->vkCode != VK_LMENU && kb->vkCode != VK_RMENU) {
                    return 1;
                }
            }
            
            // 正常状态下的快捷键处理
            if (!isCapturing && !isRecording && instance->isCtrlShiftPressed()) {
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

LRESULT CALLBACK HotkeyManager::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && instance && instance->appManager) {
        // 只处理右键按下事件
        if (wParam == WM_RBUTTONDOWN) {
            bool isCapturing = instance->appManager->screenCapture && 
                              instance->appManager->screenCapture->windowCreated;
            bool isRecording = instance->appManager->voiceRecognizer && 
                              instance->appManager->voiceRecognizer->keyListeningActive;
            
            // 临时调试输出
            // OutputDebugStringA(("Mouse right button - isRecording: " + std::to_string(isRecording) + ", isCapturing: " + std::to_string(isCapturing) + "\n").c_str());
            
            if (isRecording) {
                // 通知VoiceRecognizer处理右键
                std::thread([]() {
                    if (instance && instance->appManager && instance->appManager->voiceRecognizer) {
                        // OutputDebugStringA("Sending VK_RBUTTON to VoiceRecognizer\n");
                        instance->appManager->voiceRecognizer->onKeyPressed(VK_RBUTTON);
                    }
                }).detach();
                return 1; // 拦截右键
            }
            
            if (isCapturing) {
                // 通知ScreenCapture处理右键
                std::thread([]() {
                    if (instance && instance->appManager && instance->appManager->screenCapture) {
                        // OutputDebugStringA("Sending VK_RBUTTON to ScreenCapture\n");
                        instance->appManager->screenCapture->onKeyPressed(VK_RBUTTON);
                    }
                }).detach();
                return 1; // 拦截右键
            }
        }
    }
    
    return CallNextHookEx(instance ? instance->mouseHook : nullptr, nCode, wParam, lParam);
}

bool HotkeyManager::isCtrlShiftPressed() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
}
