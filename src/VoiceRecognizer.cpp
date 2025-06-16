#include "../include/VoiceRecognizer.h"
#include "../include/AppManager.h"
#include "../include/StringUtils.h"
#include <wininet.h>
#include <sstream>
#include <algorithm>

// 链接库只在 MSVC 编译器下有效，MinGW 忽略这些指令
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "wininet.lib")
#endif

VoiceRecognizer::VoiceRecognizer(AppManager* app) 
    : appManager(app), hWaveIn(nullptr), keyListeningActive(false), isRecording(false), shouldStop(false) {
    initializeWaveFormat();
}

VoiceRecognizer::~VoiceRecognizer() {
    if (isRecording) {
        stopRecording();
    }
}

void VoiceRecognizer::initializeWaveFormat() {
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = CHANNELS;
    waveFormat.nSamplesPerSec = SAMPLE_RATE;
    waveFormat.nAvgBytesPerSec = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8);
    waveFormat.nBlockAlign = CHANNELS * (BITS_PER_SAMPLE / 8);
    waveFormat.wBitsPerSample = BITS_PER_SAMPLE;
    waveFormat.cbSize = 0;
}

void VoiceRecognizer::startRecording() {
    if (isRecording) return;
    
    appManager->showToast("开始录音...\nESC或右键取消，空格键结束", 2000);
    
    try {
        setupRecording();
        isRecording = true;
        shouldStop = false;
        keyListeningActive = true;
        
        // 清空之前的录音数据
        recordedData.clear();
        
        // 只启动计时线程，不再需要按键检测线程
        timerThread = std::thread(&VoiceRecognizer::timerLoop, this);
        
    } catch (...) {
        appManager->showToast("录音启动失败");
        cleanupRecording();
    }
}

void VoiceRecognizer::stopRecording() {
    if (!isRecording) return;
    
    // 先设置标志，确保HotkeyManager能立即感知状态变化
    keyListeningActive = false;
    shouldStop = true;
    isRecording = false;
    
    // 停止录音设备
    if (hWaveIn) {
        waveInStop(hWaveIn);
        waveInReset(hWaveIn);
    }
    
    // 等待线程结束
    if (timerThread.joinable()) {
        timerThread.join();
    }
    
    cleanupRecording();
    
    if (!recordedData.empty()) {
        appManager->showToast("正在识别...");
        
        // 创建副本避免在异步操作中访问成员变量
        std::vector<char> dataCopy = recordedData;
        recordedData.clear();
        
        std::thread([this, dataCopy]() {
            std::vector<char> wavData = createWavFile(dataCopy);
            std::string result = sendToYoudaoAPI(wavData);
            processResult(result);
        }).detach();
    } else {
        appManager->showToast("录音数据为空");
    }
}

void VoiceRecognizer::cancelRecording() {
    if (!isRecording) return;
    
    // 先设置标志，确保HotkeyManager能立即感知状态变化
    keyListeningActive = false;
    shouldStop = true;
    isRecording = false;
    
    // 停止录音设备
    if (hWaveIn) {
        waveInStop(hWaveIn);
        waveInReset(hWaveIn);
    }
    
    // 等待线程结束
    if (timerThread.joinable()) {
        timerThread.join();
    }
    
    cleanupRecording();
    recordedData.clear();
    
    appManager->showToast("录音已取消");
}

void VoiceRecognizer::setupRecording() {
    MMRESULT result = waveInOpen(&hWaveIn, WAVE_MAPPER, &waveFormat, 
                                 (DWORD_PTR)waveInProc, (DWORD_PTR)this, CALLBACK_FUNCTION);
    
    if (result != MMSYSERR_NOERROR) {
        throw std::runtime_error("Failed to open wave input device");
    }
    
    // 准备录音缓冲区
    audioBuffers.resize(NUM_BUFFERS);
    waveHeaders.resize(NUM_BUFFERS);
    
    for (int i = 0; i < NUM_BUFFERS; i++) {
        audioBuffers[i].resize(BUFFER_SIZE);
        
        waveHeaders[i].lpData = audioBuffers[i].data();
        waveHeaders[i].dwBufferLength = BUFFER_SIZE;
        waveHeaders[i].dwFlags = 0;
        waveHeaders[i].dwLoops = 0;
        
        waveInPrepareHeader(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
        waveInAddBuffer(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
    }
    
    waveInStart(hWaveIn);
}

void VoiceRecognizer::cleanupRecording() {
    if (hWaveIn) {
        waveInStop(hWaveIn);
        waveInReset(hWaveIn);
        
        for (auto& header : waveHeaders) {
            waveInUnprepareHeader(hWaveIn, &header, sizeof(WAVEHDR));
        }
        
        waveInClose(hWaveIn);
        hWaveIn = nullptr;
    }
    
    waveHeaders.clear();
    audioBuffers.clear();
}

// 新增：按键事件处理方法
void VoiceRecognizer::onKeyPressed(int vkCode) {
    if (!isRecording || !keyListeningActive) return;
    
    // 临时调试输出
    // OutputDebugStringA(("VoiceRecognizer received key: " + std::to_string(vkCode) + "\n").c_str());
    
    switch (vkCode) {
        case VK_ESCAPE:
        case VK_RBUTTON:
            // 取消录音
            std::thread([this]() {
                cancelRecording();
            }).detach();
            break;
            
        case VK_SPACE:
            // 结束录音
            std::thread([this]() {
                stopRecording();
            }).detach();
            break;
    }
}

void VoiceRecognizer::timerLoop() {
    auto startTime = GetTickCount64();
    
    while (isRecording && !shouldStop) {
        auto elapsed = GetTickCount64() - startTime;
        if (elapsed >= (MAX_RECORD_TIME-3) * 1000) {
            // 使用异步方式避免死锁
            std::thread([this]() {
                appManager->showToast("录音时间上限60s，自动结束");
                stopRecording();
            }).detach();
            return;
        }
        Sleep(1000);
    }
}

void CALLBACK VoiceRecognizer::waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR /*dwParam2*/) {
    VoiceRecognizer* recorder = (VoiceRecognizer*)dwInstance;
    
    if (uMsg == WIM_DATA && recorder->isRecording) {
        WAVEHDR* header = (WAVEHDR*)dwParam1;
        
        // 保存录音数据
        recorder->recordedData.insert(recorder->recordedData.end(),
                                     header->lpData, 
                                     header->lpData + header->dwBytesRecorded);
        
        // 重新添加缓冲区
        if (!recorder->shouldStop) {
            header->dwFlags = 0;
            waveInPrepareHeader(hwi, header, sizeof(WAVEHDR));
            waveInAddBuffer(hwi, header, sizeof(WAVEHDR));
        }
    }
}

std::vector<char> VoiceRecognizer::createWavFile(const std::vector<char>& audioData) {
    std::vector<char> wavFile;
    
    // WAV文件头
    const char* riff = "RIFF";
    const char* wave = "WAVE";
    const char* fmt = "fmt ";
    const char* data = "data";
    
    uint32_t fileSize = 36 + audioData.size();
    uint32_t fmtSize = 16;
    uint32_t dataSize = audioData.size();
    
    // 写入文件头
    wavFile.insert(wavFile.end(), riff, riff + 4);
    wavFile.insert(wavFile.end(), (char*)&fileSize, (char*)&fileSize + 4);
    wavFile.insert(wavFile.end(), wave, wave + 4);
    
    // 写入格式块
    wavFile.insert(wavFile.end(), fmt, fmt + 4);
    wavFile.insert(wavFile.end(), (char*)&fmtSize, (char*)&fmtSize + 4);
    wavFile.insert(wavFile.end(), (char*)&waveFormat, (char*)&waveFormat + 16);
    
    // 写入数据块
    wavFile.insert(wavFile.end(), data, data + 4);
    wavFile.insert(wavFile.end(), (char*)&dataSize, (char*)&dataSize + 4);
    wavFile.insert(wavFile.end(), audioData.begin(), audioData.end());
    
    return wavFile;
}

std::string VoiceRecognizer::sendToYoudaoAPI(const std::vector<char>& audioData) {
    HINTERNET hInternet = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;
    std::string response_data;
    
    // 预先声明所有变量以避免跨越 goto 的初始化
    std::string boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    std::string postData;
    std::string headers;
    BOOL result = FALSE;
    
    // 构建multipart/form-data内容
    postData += "--" + boundary + "\r\n";
    postData += "Content-Disposition: form-data; name=\"audioData\"; filename=\"blob\"\r\n";
    postData += "Content-Type: audio/wav\r\n\r\n";
    postData.append(audioData.begin(), audioData.end());
    postData += "\r\n--" + boundary + "--\r\n";
    
    headers = "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    headers += "Accept: */*\r\n";
    headers += "Origin: https://ai.youdao.com\r\n";
    headers += "Referer: https://ai.youdao.com/\r\n";
    headers += "Accept-Language: zh-CN,zh;q=0.9,en-US;q=0.8,en;q=0.7\r\n";
    
    hInternet = InternetOpenA("VoiceRecognizer", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) goto cleanup;
    
    hConnect = InternetConnectA(hInternet, "aidemo.youdao.com", INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) goto cleanup;
    
    hRequest = HttpOpenRequestA(hConnect, "POST", "/asr?lang=zh-CHS&mutiSentences=true", nullptr, nullptr, nullptr, INTERNET_FLAG_SECURE, 0);
    if (!hRequest) goto cleanup;
    
    result = HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
    
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
    
    return response_data;
}

void VoiceRecognizer::processResult(const std::string& result) {
    std::string recognizedText;
    
    // 解析JSON响应
    size_t errorCodePos = result.find("\"errorCode\":");
    if (errorCodePos != std::string::npos) {
        size_t codeStart = result.find('\"', errorCodePos + 12);
        if (codeStart != std::string::npos) {
            codeStart++;
            size_t codeEnd = result.find('\"', codeStart);
            if (codeEnd != std::string::npos) {
                std::string errorCode = result.substr(codeStart, codeEnd - codeStart);
                
                if (errorCode == "0") {
                    // 识别成功，提取result数组
                    size_t resultPos = result.find("\"result\":");
                    if (resultPos != std::string::npos) {
                        size_t arrayStart = result.find('[', resultPos);
                        size_t arrayEnd = result.find(']', arrayStart);
                        if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
                            std::string arrayContent = result.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
                            
                            // 提取数组中的字符串
                            size_t pos = 0;
                            while ((pos = arrayContent.find('\"', pos)) != std::string::npos) {
                                pos++;
                                size_t end = arrayContent.find('\"', pos);
                                if (end != std::string::npos) {
                                    if (!recognizedText.empty()) recognizedText += " ";
                                    recognizedText += arrayContent.substr(pos, end - pos);
                                    pos = end + 1;
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                } else if (errorCode == "4304") {
                    appManager->showToast("未识别到有效语音内容");
                    return;
                } else {
                    appManager->showToast("识别失败，错误代码: " + errorCode);
                    return;
                }
            }
        }
    }
    
    if (!recognizedText.empty()) {
        copyToClipboard(recognizedText);
        insertTextAtCursor(recognizedText);
        appManager->showToast("识别成功！已输入文本并复制到剪贴板");
    } else {
        appManager->showToast("识别失败，未检测到语音内容");
    }
}

void VoiceRecognizer::insertTextAtCursor(const std::string& text) {
    std::wstring wtext = Utf8ToWide(text);
    
    // 模拟键盘输入
    for (wchar_t c : wtext) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0;
        input.ki.wScan = c;
        input.ki.dwFlags = KEYEVENTF_UNICODE;
        SendInput(1, &input, sizeof(INPUT));
        
        input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void VoiceRecognizer::copyToClipboard(const std::string& text) {
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