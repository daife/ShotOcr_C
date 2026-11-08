#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;
std::string base64(const Bytes& bytes);
std::string urlEncode(const std::string& text);
Bytes png(HBITMAP bitmap, RECT selection);
Bytes wav(const Bytes& pcm);
std::wstring parseRecognition(const std::string& json, bool voice);
std::wstring recognize(const Bytes& data, bool voice, bool english, const std::atomic_bool& cancelled);
