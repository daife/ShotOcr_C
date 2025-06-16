#ifndef VOICERECOGNIZER_H
#define VOICERECOGNIZER_H

#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

class AppManager;

class VoiceRecognizer {
public:
    VoiceRecognizer(AppManager* app);
    ~VoiceRecognizer();
    
    void startRecording();
    void stopRecording();
    void cancelRecording();
    
    // 新增：按键事件处理接口
    void onKeyPressed(int vkCode);
    
    // 公共访问（供HotkeyManager使用）
    std::atomic<bool> keyListeningActive;

private:
    AppManager* appManager;
    
    HWAVEOUT hWaveOut;
    HWAVEIN hWaveIn;
    WAVEFORMATEX waveFormat;
    
    std::vector<WAVEHDR> waveHeaders;
    std::vector<std::vector<char>> audioBuffers;
    std::vector<char> recordedData;
    
    std::atomic<bool> isRecording;
    std::atomic<bool> shouldStop;
    std::thread recordingThread;
    std::thread timerThread;
    
    static const int SAMPLE_RATE = 16000;
    static const int CHANNELS = 1;
    static const int BITS_PER_SAMPLE = 16;
    static const int BUFFER_SIZE = 4096;
    static const int NUM_BUFFERS = 4;
    static const int MAX_RECORD_TIME = 59; // 最大录音时间（秒）
    
    void initializeWaveFormat();
    void setupRecording();
    void cleanupRecording();
    void timerLoop();
    
    // 移除 recordingLoop，改为事件驱动
    // void recordingLoop();  // 删除这一行
    
    std::vector<char> createWavFile(const std::vector<char>& audioData);
    std::string sendToYoudaoAPI(const std::vector<char>& audioData);
    void processResult(const std::string& result);
    void insertTextAtCursor(const std::string& text);
    void copyToClipboard(const std::string& text);
    
    static void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
};

#endif // VOICERECOGNIZER_H
