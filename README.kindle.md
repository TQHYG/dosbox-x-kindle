# DOSBox-X Kindle 移植构建指南

本目录包含 DOSBox-X 移植到 Kindle（armv7l 软浮点，如 Kindle Oasis / Paperwhite）的交叉编译脚本与所需补丁。

目标是产出一个**单个 `dosbox-x` 二进制**：SDL2、zlib、libpng、libstdc++、libgcc、libatomic 全部静态链接进二进制，仅动态依赖设备自带的 glibc 与 X11 库（Kindle 的 Xorg 环境已提供）。

---

## 1. 环境要求

| 项目 | 说明 |
|---|---|
| 交叉工具链 | crosstool-NG（NiLuJe）构建的 `arm-kindlepw2-linux-gnueabi`，GCC 14.x，glibc 2.12 sysroot |
| 主机工具 | `autoconf` `automake` `aclocal` `libtool` `make`（即 GNU Autotools） |
| 目标设备 | Kindle 带 Xorg + awesome（Lab126 定制）窗口管理器 |

工具链默认路径为 `/home/tqhyg/x-tools/arm-kindlepw2-linux-gnueabi`，可通过环境变量 `KINDLE_TOOLCHAIN` 覆盖。

---

## 2. 快速开始

```bash
git clone <本项目>
cd dosbox-x-kindle

# 如工具链不在默认路径，先设置：
#   export KINDLE_TOOLCHAIN=/path/to/arm-kindlepw2-linux-gnueabi

./build-kindle.sh
```

构建产物：

```
build-artifacts/
├── dosbox-x           # 最终可执行文件（已 strip，约 14 MB）
├── sdl2-build/        # SDL2 构建目录
├── sdl2-install/      # SDL2 静态库安装前缀（libSDL2.a、头文件、sdl2.pc）
├── zlib-build/        # zlib 构建目录
├── libpng-build/      # libpng 构建目录
└── deps-install/      # 静态 zlib + libpng 安装前缀
```

部署时只需把 `dosbox-x` 传到设备（如 `/mnt/us` 或 `/tmp/root`），再放一份 `dosbox-x.conf` 即可。

---

## 3. 构建参数说明

### 3.1 SDL2（`vs/sdl2/`，静态库）

```text
--enable-static --disable-shared   静态编译，不打 .so，直接链入 dosbox-x
--enable-video-x11                 仅保留 X11 视频驱动（运行时 dlopen libX11.so.6）
--disable-video-wayland/kmsdrm/directfb/rpi/vivante/offscreen
--disable-video-opengl/opengles1/opengles2/vulkan   Kindle 无 GL/GLES
--disable-alsa/pulseaudio/oss/pipewire/jack/esd/arts/sndio/nas
                                   仅保留 disk + dummy 音频驱动（Kindle 无声卡）
--disable-joystick --disable-haptic
```

### 3.2 zlib + libpng（`vs/zlib/`、`vs/libpng/`，静态）

Kindle sysroot 里的 libpng 头文件（1.6）与动态库（1.2）版本不一致，因此改用源码内建的 zlib 1.3.1 与 libpng 1.6.x 静态编译到私有前缀 `deps-install/`。

### 3.3 DOSBox-X（`configure`）

```text
--host=arm-kindlepw2-linux-gnueabi
--enable-sdl2           使用 SDL2（通过 PKG_CONFIG_PATH 找到静态 sdl2.pc）
--disable-opengl        Kindle X 服务器无 OpenGL
--disable-freetype      使用 output=surface，无需 TTF 输出
--disable-avcodec       避免误链宿主机 FFmpeg
--disable-sdlnet --disable-alsa-midi --disable-libfluidsynth --disable-libslirp
                        设备上无这些库
--disable-dynamic-x86 --disable-dynrec   x86 专用动态核心，ARM 上禁用
```

编译器/链接器环境：

```text
CPPFLAGS="-D__STDC_FORMAT_MACROS -I<deps-install>/include"
LDFLAGS="-L<deps-install>/lib -static-libgcc -static-libstdc++"
```

- `-D__STDC_FORMAT_MACROS`：修复 `src/gui/whereami.c` 在 C++ 下 `PRIx64` 未定义导致的编译错误。
- `-static-libgcc -static-libstdc++`：把 C++ 运行时静态链入（设备上的老 libstdc++ 缺少 `GLIBCXX_3.4.20+` 符号）。
- 构建脚本还会用 `sed` 把 `-latomic` 替换为 `-Wl,-Bstatic -latomic -Wl,-Bdynamic`：32 位 ARM 上 64 位原子操作需要 libatomic，而设备上没有 GCC 14 的 `libatomic.so`，故静态链接。

---

## 4. 源码补丁说明

以下补丁已包含在本仓库中，构建脚本会自动使用，无需手动应用。

| 文件 | 修改内容 | 原因 |
|---|---|---|
| `vs/sdl2/src/audio/dummy/SDL_dummyaudio.c` | `DUMMYAUDIO_bootstrap` 的 `demand_only` 由 `SDL_TRUE` 改为 `SDL_FALSE` | Kindle 无声卡；让 dummy 音频成为自动回退驱动，无需设置 `SDL_AUDIODRIVER=dummy` |
| `vs/sdl2/src/video/x11/SDL_x11modes.c` | `get_visualinfo()` 优先匹配高深度 TrueColor/DirectColor 视觉（32→24→16→15），再回退默认深度 | Kindle 默认 8 位 BGR332 视觉无法被 SDL2 映射（返回 `SDL_PIXELFORMAT_UNKNOWN` 导致崩溃），改用可用的 32 位视觉 |
| `vs/sdl2/src/video/x11/SDL_x11window.c` | ① `X11_SetWindowTitle` 在 `XSupportsLocale()` 为假时回退 `XStoreName` + `_NET_WM_NAME` | Kindle Xorg 不支持 locale，原逻辑直接返回错误，窗口标题永远为空 |
| 同上 | ② 窗口属性 `backing_store` 由 `NotUseful` 改为 `WhenMapped` | Kindle X 服务器在无 backing store 时渲染 depth-32 窗口出错（画面横向拉伸约 3 倍）；启用 backing store 后 1:1 渲染正常 |
| `src/gui/sdlmain.cpp` | ① `GUI_StartUp()` 移除硬编码 `SDL_SetWindowTitle(...,"DOSBox-X")`，改调 `GFX_SetTitle()` | 硬编码标题覆盖了配置中的 `title` |
| 同上 | ② `GFX_SetTitle()` 当 `[dosbox] title` 非空时原样用作窗口标题（不再附加 ` - DOSBox-X` 版本号/cycles） | Kindle 的 awesome WM 按 `L:` 前缀解析窗口名（如 `L:A_N:application_...`），任何附加后缀都会破坏解析导致窗口不被识别 |
| `src/gui/sdlmain_linux.cpp` | `Linux_TryXRandrGetDPI()` 中限制 `ochk->nameLen`（>0 且 <4096 才使用） | Kindle X 服务器的 XRandR 输出 nameLen 不可靠（实测 0→0→87），盲信会抛 `std::length_error` |

---

## 5. 运行时配置要点（dosbox-x.conf）

```ini
[sdl]
output     = surface        # 软件渲染，Kindle 无 OpenGL
fullscreen = true           # 用 X 的桌面分辨率
fullresolution = desktop
autolock   = false
titlebar   =                # 留空，窗口名完全由 [dosbox] title 决定
onscreen_keyboard = true    # 屏幕底部 1/3 显示软键盘（默认 true）

[dosbox]
title = L:A_N:application_PC:T_ID:org.dosbox
#  ^ 必须符合 Kindle awesome WM 的 "L:" 窗口命名规范，
#    否则窗口管理器不会把它当作应用程序窗口显示。

[cpu]
core   = auto               # ARM 上无动态核心，写 dynamic 会被回退
cycles = auto
```

### 软键盘说明

- 窗口被分为上下两个区域：上 2/3 为 DOS 显示区（触摸映射为鼠标），下 1/3 为 5 行软键盘。
- 修饰键：Ctrl / Alt / Win / Fn / Caps 为**锁定式**开关（点按切换，再按取消）；Shift 为**单次**生效（按下后对下一个按键生效）。
- **Fn**：切换第一行数字键为 F1–F12。
- 按键与修饰键均有高亮视觉反馈；单指触控友好。
- 关闭软键盘：`[sdl] onscreen_keyboard = false`。

说明：
- 窗口名 `L:A_N:application_...` 是 Lab126 的窗口命名约定：`L:` 表示 layer，`A` = application 层，`N:application` 表示应用程序窗口，`ID:` 是应用标识。awesome WM 据此把窗口铺满应用层区域（状态栏下方）。
- Kindle 屏幕为纵向（1072×1448），而 DOS 多为横向，可根据需要设置 `aspect` / `scaler` / `autofit` 调整缩放与宽高比。

---

## 6. 故障排查

| 现象 | 原因 / 处理 |
|---|---|
| `Can't init SDL No available audio device` | SDL2 dummy 音频补丁未生效；确认重新编译了 `vs/sdl2` 且 `SDL_dummyaudio.c` 中 `demand_only=SDL_FALSE` |
| 窗口像素格式为 `SDL_PIXELFORMAT_UNKNOWN` 后崩溃 | 视觉选择补丁未生效；确认 `vs/sdl2/src/video/x11/SDL_x11modes.c` 有高深度视觉优先逻辑 |
| 画面横向被拉长约 3 倍、右侧超出屏幕 | `backing_store` 补丁未生效；确认 `SDL_x11window.c` 中为 `WhenMapped` |
| 窗口标题为空或显示 "DOSBox-X" | 标题补丁未生效；确认 `SDL_x11window.c` 有 locale 回退，`sdlmain.cpp` 无硬编码标题 |
| 启动即 `std::length_error` 崩溃 | XRandR nameLen 限制补丁未生效；确认 `sdlmain_linux.cpp` 有边界检查 |
| `core=dynamic` 无效告警 | ARM 无动态核心，把配置改为 `core=auto` 或 `core=normal` |

---

## 7. 产物校验

构建完成后可这样验证：

```bash
file build-artifacts/dosbox-x
# ELF 32-bit LSB executable, ARM, EABI5, dynamically linked, stripped

arm-kindlepw2-linux-gnueabi-readelf -d build-artifacts/dosbox-x | grep NEEDED
# 应只有：libc.so.6 / libdl.so.2 / libm.so.6 / libpthread.so.0 / librt.so.1
#         + libX11.so.6 / libXrandr.so.2 / libxkbfile.so.1
# 不应出现：libSDL2、libz、libpng、libstdc++、libgcc_s、libatomic
```
