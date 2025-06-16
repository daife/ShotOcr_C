#include "../include/AppManager.h"
#include <iostream>
#include <windows.h>

// 链接库只在 MSVC 编译器下有效，MinGW 通过 CMakeLists.txt 链接
#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#endif

int main() {
    // 设置控制台输出为UTF-8，以便正确显示 e.what() 中的中文字符
    SetConsoleOutputCP(CP_UTF8);
    
    // 初始化COM
    CoInitialize(nullptr);
    
    try {
        // 创建应用程序管理器
        AppManager app;
        
        // 运行程序主循环
        app.run();
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    // 清理COM
    CoUninitialize();
    
    return 0;
}