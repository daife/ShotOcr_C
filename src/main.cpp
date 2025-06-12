#include "../include/ScreenShotOCR.h"
#include <iostream>
#include <windows.h>

// 链接必要的库
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

int main() {
    // 设置控制台输出为UTF-8，以便正确显示 e.what() 中的中文字符
    SetConsoleOutputCP(CP_UTF8);
    
    // 初始化COM
    CoInitialize(nullptr);
    
    try {
        // 创建OCR实例
        ScreenShotOCR ocrApp;
        
        // 运行程序主循环
        ocrApp.run();
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    // 清理COM
    CoUninitialize();
    
    return 0;
}