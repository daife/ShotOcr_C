# ShotOcr

CachyOS / niri 重建版见 [Linux 使用、构建与实现说明](linux/README.md)。Windows 版本说明如下。

Windows 10（1703+）/ 11 x64 原生托盘工具。C++17 + Win32，无第三方运行库，单个 EXE。

## 使用

运行 `build\ShotOcr.exe`，在目标输入框放好光标：

- **Ctrl+Shift+S**：冻结桌面，拖动框选，松开即识别。Esc / 右键取消。
- **Ctrl+Shift+H**：开始录音，**空格**结束并识别。Esc / 右键取消，60 秒自动提交。
- 托盘右键菜单选择语音中文 / English，或退出。默认中文，语言选择保存在 `HKCU\Software\ShotOcr`。

识别成功自动复制，有输入目标且焦点仍在原处时粘贴一次，不发送回车。没有输入目标或已切换窗口时仅复制。截图框选结束立即恢复原焦点；录音条和识别提示不抢焦点。等待 Ctrl / Shift / Alt / Win 松开后才粘贴，不释放用户按着的键。剪贴板写入失败时不粘贴。

录音、框选、网络识别和等待粘贴期间均可取消。取消后迟到的结果被丢弃。截图和录音仅在内存中处理，提交至有道进行识别，不生成本地媒体文件。OCR 按返回的行顺序保留换行。

## 编译

安装标准 **Visual Studio 2022 Build Tools → 使用 C++ 的桌面开发**（MSVC x64、Windows SDK），然后运行：

```bat
build.cmd
```

脚本通过 `vswhere` 定位安装并调用 `vcvars64.bat`，只影响当前构建进程的环境。输出为 `build\ShotOcr.exe`，使用静态 C++ 运行库，可直接复制运行。

## 实现

- `src/main.cpp`：托盘、全局快捷键、GDI 截图蒙层、WinMM 录音、焦点与剪贴板、单个网络工作线程。
- `src/service.cpp`：WIC PNG 编码、Base64、PCM WAV 封装、WinHTTP 和 JSON 响应解析。
- 截图：PNG → Base64 → `POST https://aidemo.youdao.com/ocrapi1`，表单 `imgBase=data:image/png;base64,...&lang=&company=`，取 `lines[].words`。
- 语音：16kHz / 单声道 / 16 位 PCM WAV，录完后 `POST https://aidemo.youdao.com/asr?lang=zh-CHS`（英文 `en`），multipart 文件字段 `audioData`，取 `result`。

接口字段参照有道官网的[文字识别演示](https://ai.youdao.com/new/product-ocr-print.s)和[短语音演示](https://ai.youdao.com/new/product-asr.s)。快捷键占用、设备或接口错误通过提示条显示。普通权限进程无法向管理员权限窗口注入粘贴，此时仍可手动粘贴剪贴板文字。
