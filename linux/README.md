# CachyOS / niri

此分支保留 Windows 源码，并新增本机 Wayland 版本。C++17 + 系统 Qt6 Widgets/Network、AT-SPI；不使用 WebView、Python 环境或常驻辅助守护进程。

## 本机使用

程序已安装到 `~/.local/bin/shotocr`，niri 的 `~/.config/niri/shotocr.kdl` 配置快捷键与启动，主配置只新增一条 include。不修改 PATH。保持原有 Ctrl+Shift+1/2/3 截图绑定。

- Ctrl+Shift+S：冻结桌面，拖动框选，松开提交；Esc / 右键取消。
- Ctrl+Shift+H：开始录音；再次按此组合键提交。60 秒自动提交。
- Ctrl+Shift+Esc：取消框选、录音、请求或待提交结果。
- 托盘菜单：截图、录音/提交、取消、中文 / English、退出。语言写入 Qt 标准用户配置位置 `~/.config/ShotOcr/ShotOcr.conf`。

识别后复制文本，保留行顺序与换行，不发送回车。自动粘贴要求原 niri 窗口和原 AT-SPI 可编辑控件仍聚焦，通过控件的 PasteText 接口执行一次，不模拟按键。未开放无障碍接口的应用（包括部分终端）、启动后尚未产生焦点事件的控件，或焦点改变时只复制。AT-SPI 的控件粘贴行为由目标应用实现，尚需逐个验证日常使用的应用；不能宣称与 Win32 对所有应用的兼容性相同。

录音提交与全局取消采用上述 niri 组合键，录音期间空格、Esc、右键继续交给当前应用。这是相对 Windows 的已接受交互调整。提示走桌面通知，不抢焦点。当前实际验证设备为单屏、1.75 倍缩放；多屏跨屏框选尚未验证。

## 构建与更新

系统依赖：C++ 编译器、CMake、pkg-config、qt6-base、at-spi2-core、grim、pipewire-audio（pw-record）、wl-clipboard。优先复用已安装包。本次只新增了 grim；探索过程中安装的 wtype 已移除。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
shotocr quit
cmake --install build --prefix "$HOME/.local"
shotocr
```

离线测试验证反向拖动、像素缩放、空选区、Esc 和右键取消。可选在线集成检查会向有道发送一张生成的测试文字图片，并覆盖当前剪贴板：

```sh
cmake --build build --target integration_test
./build/integration_test
```

## 实现与性能

- 空闲无轮询定时器；Qt 事件循环处理命令、本机进程 I/O 和异步 HTTPS。快捷键客户端只启动 QCoreApplication，不初始化图形界面。
- grim 输出 PPM 管道，仅在框选时保留冻结桌面，选区编码 PNG 后释放。全图不经过 PNG 压缩再解压。
- pw-record 输出 16kHz 单声道 s16 PCM 管道；最多 60 秒（1.92MB），在内存中封装 WAV。
- 继续使用 Windows 版有道演示接口、请求字段和语言参数，不自动重试。响应最多 2MiB，网络超时提示错误。
- 每次操作使用代次标识；取消终止当前子进程或网络请求，迟到回调不复制、不粘贴。媒体不写入文件。
- 剪贴板通过已有 wl-copy 接管所有权，不要求后台应用获得键盘焦点；其标准剪贴板所有者进程按 Wayland 规则存活。

本机部署文件：`~/.local/bin/shotocr`、`~/.config/niri/shotocr.kdl`，以及主 niri 配置中的 include。移除部署时先 `shotocr quit`，移除这两个文件和对应 include；语言配置仅在更改语言后产生。项目构建产物统一在已忽略的 `build/` 下。
