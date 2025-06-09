#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <string>
#include <windows.h> // For CP_UTF8, MultiByteToWideChar, WideCharToMultiByte

// 将 UTF-8 编码的 std::string 转换为 std::wstring
inline std::wstring Utf8ToWide(const std::string& utf8Str) {
    if (utf8Str.empty()) {
        return std::wstring();
    }
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.length(), NULL, 0);
    if (sizeNeeded == 0) {
        // 可以考虑记录错误 GetLastError()
        return std::wstring(); // 或抛出异常
    }
    std::wstring wideStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.length(), &wideStr[0], sizeNeeded);
    return wideStr;
}

// 将 std::wstring 转换为 UTF-8 编码的 std::string
inline std::string WideToUtf8(const std::wstring& wideStr) {
    if (wideStr.empty()) {
        return std::string();
    }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), (int)wideStr.length(), NULL, 0, NULL, NULL);
    if (sizeNeeded == 0) {
        // 可以考虑记录错误 GetLastError()
        return std::string(); // 或抛出异常
    }
    std::string utf8Str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), (int)wideStr.length(), &utf8Str[0], sizeNeeded, NULL, NULL);
    return utf8Str;
}

#endif // STRINGUTILS_H
