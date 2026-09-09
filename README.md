# GameTrigger — 进程触发与监控

> 一个 Windows 桌面小工具：**监控某个进程（例如游戏）的启动与退出，并自动执行一组动作**。
> 基于 [HeliosView](https://github.com/CoplenSasbian/HeliosView)（C++ / WebView2 + 原生 ⇄ JS 桥）与
> **React + Vite** 前端实现。

当你想给游戏加“进入时切换高性能电源方案 + 开启 HDR + 关闭粘滞键”，退出时再把一切还原，
GameTrigger 就是干这个的：配好规则后它一直在后台监控进程，命中就自动切换。

```
┌────────────────────────── C++ ──────────────────────────┐
│  main()                                                  │
│    AppContext    UI loop (helios::App)                   │
│    MainWindow    : helios::WebViewWindow (WebView2)      │
│      ProcessMonitor   exe → config 自动切换              │
│      PluginManager    动态加载插件 DLL 并执行            │
│        │  window.helios.call('config_get') → Promise     │
│        ▼                                                 │
└──── frontend (React + Vite) ───────────────────────────┘
     dev : vite dev server on :5173   (HMR)
     prod: built assets served from appRoot/assets/ via https://app.local/
```

## 核心概念

**配置（Config）** 是触发动作的载体。每个配置保存一份“每个插件各自参数值”的设置，
切换配置即触发该配置内所有已启用插件的 `execute()`，立即生效。自带一个内置配置
`close`（不启用任何动作），用于“还原”状态——比如进程退出后自动切回。

**进程自动切换（Process auto-switch）**：维护一组 `exe → 配置` 规则。某个被监控的进程
启动时，自动切换到对应配置；最后一个被监控的进程退出时，自动回到 `close`。也可在托盘
或主页手动切换。

## 功能特性

- **配置系统**：可创建/删除/复制多套配置，插件参数按配置分别保存；内置 `close` 关闭态。
- **插件（动态 DLL）**：每个插件声明自己的参数（int / double / string / bool / 文件 / 目录 /
  下拉选择等），前端按元数据自动生成表单：
  - **HDR 插件** — 切换 HDR（原生 DisplayConfig API / Win11 24H2+ `SET_HDR_STATE`，兜底 `Win+Alt+B`）
  - **NVIDIA 数字振动插件** — 自动调节 NVIDIA 数字振动（Digital Vibrance）
  - **电源方案插件** — 切换 Windows 电源方案（枚举本机所有方案，下拉选择）
  - **运行脚本插件** — 激活配置时执行脚本/程序（`bat` / `ps1` / `vbs` / `exe`）
  - **粘滞键插件** — 屏蔽/恢复粘滞键（临时屏蔽、退出自动还原用户原设置）
- **毛玻璃外观与个性化**：iOS 风格磨砂玻璃 + 多色极光背景；支持 Bing 每日壁纸 / 本地背景
  图库 / 纯色背景；根据背景亮度自动切换深浅主题；设计面板可调**主题色、模糊、圆角、字号、密度**。
- **系统托盘**：打开主界面、切换配置、开机启动（任务计划程序）、进程监控开关、退出。
- **进程监控**：后台 watch 指定 exe，启动/退出自动切换配置。
- **应用内日志控制台**：日志实时刷新到前端，可过滤（时间/来源/级别）。
- 单实例守护、开机自启（Task Scheduler，非注册表）、窗口位置/尺寸记忆。

## 界面

- **主页** — 当前状态、一键切换配置、日志控制台。
- **插件** — 插件列表 + 配置管理（创建/删除），按配置编辑各插件参数。
- **进程监控** — 维护 `exe → 配置` 的自动切换规则。
- **应用设置** — 常规行为（开机启动、托盘弹窗位置）与背景/主题/设计面板。

## 构建

### 前置依赖

| 工具 | 用途 |
| --- | --- |
| CMake ≥ 4.3 | C++ 构建 |
| C++23 编译器 | MSVC（Windows）或 Clang/GCC |
| ninja | C++ 构建（Windows 回退到 Visual Studio 生成器） |
| Node.js ≥ 20 | 前端（Vite） |

HeliosView 库是 **git 子模块**（`HeliosView/`，跟踪 master 分支），含其自身依赖
（stdexec + Boost 超级项目子模块 —— 桥接用 Boost.JSON；WebView2 SDK 与 OpenSSL 在配置时从
NuGet 拉取），无需 vcpkg/conan。

> **自动配置**：全新 clone 可直接构建。`CMakeLists.txt` 在 configure 时运行
> `ensure-submodule.cmake`，自动初始化缺失的子模块（HeliosView → 其内层 stdexec + boost →
> HeliosView 需要的少数 Boost 库，一次 `git submodule update` 流式拉取），无需手动
> `git submodule update` 来回递归。

### Windows

```bat
REM 1a. 开发（C++ + Vite 开发服务器，带 HMR）
scripts\dev.cmd

REM 1b. 打发布包（C++ + 编译后的前端）
scripts\build.cmd
dist\bin\GameTrigger.exe
```

- **Dev 模式（默认）**：前端走 `vite dev --port 5173 --strictPort`（HMR），C++ 导航到
  `http://localhost:5173`。
- **Prod 模式**（`build.cmd` 自动切到 `-DHELIOSVIEW_TEMPLATE_DEV=OFF`）：`vite build` 输出
  拷到应用根目录的 `assets\`（即 exe 所在 `bin\` 的上一级，如 `dist\bin\GameTrigger.exe` →
  `dist\assets\`），通过 WebView2 虚拟主机 `https://app.local/` 加载（`file://`
  无法服务 Vite 的 ES-module 产物）。`dist\` 由 `cmake --install` 组装，只含运行所需文件，
  整目录可直接分发。

CLion / IDE 开发流程：终端跑 `scripts\vite.cmd` 起前端，然后在 IDE 里运行 `GameTrigger`
目标即可（默认 Dev 模式）。

### 应用身份、应用名与窗口标题

集中在一个文件 **`app-config.cmake`**（仓库根目录，被 `CMakeLists.txt` 引入）：

| 变量 | 当前值 |
| --- | --- |
| `HELIOSVIEW_TEMPLATE_APP_NAME` | `GameTrigger`（可执行文件 / target 名） |
| `HELIOSVIEW_TEMPLATE_APP_ID` | `Game Trigger`（进程身份：Windows AppUserModelID / macOS bundle id / Linux app id，同时是通知的默认 id） |
| `HELIOSVIEW_TEMPLATE_APP_TITLE` | `Game Trigger`（窗口标题） |

`src/main.cpp` 在创建任何窗口之前把它交给库：

```cpp
helios::App::setAppId(HELIOSVIEW_TEMPLATE_APP_ID);   // 进程身份；notificationInit() 默认用它
helios::notificationInit();                          // 不再传参：id 来自上面
```

配套的还有 `helios::App::setActivationPolicy(...)`：托盘/菜单栏常驻应用在 macOS 上应使用
`helios::ActivationPolicy::Accessory` 以隐藏 Dock 图标。两者都是进程级设置，必须在第一个窗口
之前调用（详见 HeliosView README 的 “Process identity / activation policy”）。

改完重新构建即可，`scripts\dev.cmd` / `build.cmd` 会按构建输出自动找到 exe，无需其它同步。

> 目前仅支持 **Windows**（Win32 + WebView2）。前端工具链（dev server / `vite build`）可在任意
> 系统使用，C++ 后端待 HeliosView 提供非 Windows 后端后即可移植。

## 架构

`AppMain()`（`src/main.cpp`，由 `src/entry.cpp` 的平台入口调用）装配三件事：

1. **`AppContext`**（`src/AppContext.h`）— 应用级服务，先于所有窗口创建：
   - `app()` — UI 消息循环（`helios::App`，同时是 `std::execution` 调度器）；
     `postTask()` 把任务投递到 UI 线程；每个窗口/WebView API 都必须在消息循环线程上跑。
   - `logger()` — 全局日志，监听器实时转发到前端 `log` 频道。
   - `guard()` — 单实例守护。
2. **`MainWindow`**（`src/MainWindow.h/.cpp`）— 继承 `helios::WebViewWindow`，注册原生 ⇄ JS
   桥接、`loadFrontend()` 加载前端；持有 `PluginManager`（插件加载/激活）、`ProcessMonitor`
   （进程自动切换）、`Wallpaper`（Bing 壁纸）、`BgImages`（本地背景图库）。
3. **UI 循环** — `return ctx.app().exec()`，最后一个窗口关闭时退出。

### 插件系统

插件是独立的动态库（放在 `plugins/`），实现 `IPlugin` 接口（见 `PluginInterface/include/IPlugin.h`），
用 `REGISTER_PLUGIN` 导出。`PluginManager` 在启动时从应用根目录 `plugins\` 目录加载 DLL，查询每个
插件的参数元数据；切换配置时逐个调用已启用插件的 `execute(PluginParameterValue*)`。
开发新插件只需在 `plugins/` 下仿照现有例子里加一个 target 即可，无需改主程序。

### 原生 ⇄ JS 桥

HeliosView 向每个页面注入 `window.helios`。前端调用 `window.helios.call(name, ...args)`，
原生侧在 `MainWindow::setupBridge()` 用 `bindJson<...>` 绑定（handler 是 detached 的
`std::execution::task` 协程，Promise 在线程安全地 resolve）。

当前桥接函数（`src/MainWindow.cpp`）：

| 函数 | 说明 |
| --- | --- |
| `config_get` | 获取整体配置状态（活动配置、配置列表、插件信息、参数元数据/值） |
| `plugins_getParamValues` / `plugins_setParams` | 读取 / 写入某套配置的插件参数 |
| `plugins_activate` / `plugins_createConfig` / `plugins_deleteConfig` | 切换 / 创建 / 删除配置 |
| `plugins_pickPath` | 文件 / 文件夹选择对话框 |
| `settings_get` / `settings_set` | 整对象读写应用设置（含进程规则、开机自启、设计令牌） |
| `wallpaper_fetch` / `bg_list` / `bg_load` / `bg_loadThumb` | Bing 壁纸与本地背景图库 |
| `shell_reveal` / `shell_openDir` | 在资源管理器中定位文件 / 打开目录 |

广播频道（前端 `BroadcastChannel`）：`configActivated`、`paramsSaved`、`configsChanged`、
`settingsChanged`、`log`。

## 目录结构

```
CMakeLists.txt       ensure-submodule + add_subdirectory(HeliosView) + 应用 target + dev/prod 模式
app-config.cmake     应用身份：程序名、进程 id、窗口标题（改这里）
ensure-submodule.cmake  configure 时自动拉取 HeliosView（及其嵌套依赖）子模块
HeliosView/          HeliosView 库（git 子模块，concrete commit 固定在索引里）
PluginInterface/     插件 SDK：IPlugin / PluginParameter 接口头文件
plugins/             各插件源码（HDR / NvDvc / Power / RunScript / StickyKeys / test）
src/AppContext.h     UI 循环上下文 + 日志 + 单实例守护
src/MainWindow.h/.cpp  主窗口：WebViewWindow 子类、桥接绑定、前端加载
src/ProcessMonitor.h / ProcessMonitor_win32.cpp  进程 → 配置自动切换
src/AppSettings.h    应用设置（整对象 JSON 持久化 app.json）
src/Wallpaper.h · BgImages.h · Logger.h · AutoStart.h …
frontend/            React + Vite 前端（src/style.css · theme.css · pages/ · components/）
scripts/dev.cmd      开发循环：Vite dev server + C++ 应用
scripts/build.cmd    发布：vite build + C++ prod 构建并组装 dist\
scripts/vite.cmd     仅起 Vite dev server（供 CLion / IDE 使用）
```

## License / 致谢

- 界面框架：HeliosView template（`/src`、`frontend/` 及构建脚本来自
  `template/react-js` 分支）。
- 依赖：Windows 回调 / WebView2（微软）、Boost.JSON、stdexec、miniz、Vite、React。
