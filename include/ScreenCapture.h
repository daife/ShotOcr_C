#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <windows.h>
#include <string>
#include <vector>

class AppManager;

class ScreenCapture {
public:
    ScreenCapture(AppManager* app);
    ~ScreenCapture();
    
    void startCapture();

private:
    AppManager* appManager;
    HWND overlayWindow;
    int startX, startY, endX, endY;
    bool dragging;
    bool windowCreated;
    int screenWidth, screenHeight;
    
    void createOverlayWindow();
    void closeOverlay();
    void onMousePress(int x, int y);
    void onMouseDrag(int x, int y);
    void onMouseRelease(int x, int y);
    void captureAndOCR(int x1, int y1, int x2, int y2);
    
    std::string captureScreenRegion(int x, int y, int width, int height);
    std::string callYoudaoOCR(const std::string& imgBase64);
    std::string encodeBase64(const std::vector<unsigned char>& data);
    std::string unescapeJsonString(const std::string& escapedStr);
    void copyToClipboard(const std::string& text);
    
    static LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

#endif // SCREENCAPTURE_H
