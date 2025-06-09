#include "../include/ScreenShotOCR.h"
#include <iostream>
#include <windows.h>

// 链接必要的库
// #pragma comment(lib, ...) 主要用于 MSVC 编译器。
// 对于 g++ (MinGW)，您需要在链接命令中显式链接这些库，例如:
// g++ ... -lole32 -luser32 -lgdi32 ...
// 并且需要将所有 .cpp 文件（main.cpp, ScreenShotOCR.cpp, ToastWindow.cpp）
// 都包含在编译命令中。
// 例如: g++ main.cpp ScreenShotOCR.cpp ToastWindow.cpp -o main.exe -lole32 -luser32 -lgdi32 -lgdiplus -lwininet -lshlwapi -lshell32
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

int main() {
    // 设置控制台输出为UTF-8，以便正确显示 e.what() 中的中文字符
    SetConsoleOutputCP(CP_UTF8);
    // 可选：同时设置控制台输入编码（如果需要）
    // SetConsoleCP(CP_UTF8);

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