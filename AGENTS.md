# meditor

轻量级、跨平台、专注 mcpp 与 C++ Modules 的产品级现代编辑器，支持桌面和手机；交互复杂度对标 Dev-C++，采用现代、简洁、优雅的视觉与交互。

## 实现约定

- 使用 **mcpp + C++23 Modules** 构建；mcpp 用法见 [skill](.agents/skills/mcpp-usage/SKILL.md)。以当前 mcpp 文档与实际行为为准。
- C++ 命名与编码遵循 [mcpp-style-ref](.agents/skills/mcpp-style-ref/SKILL.md) 和 [详细参考](.agents/skills/mcpp-style-ref/reference.md)；本文件的构建、目录与接口分离约定优先。
- 使用 mcpp workspace 划分应用与组件，共享版本、工具链和依赖。自研组件放在 `modules/<component>/`；仅职责独立、接口稳定、可独立构建测试且有复用/分发价值的子系统设独立 `mcpp.toml`，不因一两个文件拆包。普通内部模块保留在所属包。应用目录只留入口与装配，README 保持简洁。
- `.cppm` 声明模块接口，配套 `.cpp` 使用 `module <name>;` 实现；模板、constexpr 及必须可见的定义留在接口。使用 `import std;`，传统头文件限制在第三方或平台适配边界。
- 必需头文件收敛到最小适配模块和实现单元，私有 include 路径不外泄；不在多个公共模块重复包含重头文件。openkal 优先评估为平台实现候选，不未经兼容验证就替换 GUI 进程的 C/C++ 运行时。
- 优先复用 [mcpp-index](https://github.com/mcpp-community/mcpp-index) 中维护良好、质量可靠、覆盖目标平台的库；核对实际平台支持、依赖体积与许可证，避免重复自研通用设施。
- mcpp、mcppls 按平台携完整运行载荷随包分发，通过现有 CLI/JSON、LSP 接口原生集成，不修改两者源码。产品统一通过 meditor GUI/CLI 操作，不要求用户或 Agent 直接调用底层工具。
- 使用统一自绘标题栏与底部状态栏/设置入口；保留各平台窗口控制、安全区和辅助访问。必须支持 headless 的真实视图树布局、交互及虚拟时钟动画测试，原生窗口/IME/真机验证另行保留。
- GUI 与 CLI 复用命令、状态和事务；功能同时设计机器可读操作，支持无窗口测试与 Agent 控制。视觉、布局、动画、键盘与触摸交互作为产品验收内容。
- 使用 mcpp 插件体系打包及生成安装器；缺失的开发工具优先补充到 xim-pkgindex，库依赖优先从 mcpp-index 选择。

## HuxerUI

- 采用 `https://github.com/Sunrisepeak/HuxerUI.git` 的 `meditor` 分支，基于 `mcpp-live2d-template` 的 `0a207a56b598e102d7eab9dd52fd36933bd9e9c7` 创建；本地位于 `../HuxerUI`。
- 通过 Git `branch = "meditor"` 引用，提交 `mcpp.lock`；使用依赖更新命令推进分支解析版本，发布沿用已验证 lockfile，无须反复修改 manifest 中的 commit。
- 按责任归属修改：HuxerUI 的通用框架问题修复放其 `meditor` 分支；仅 meditor 相关的输入、渲染、平台适配或业务逻辑放本仓库对应模块，不因涉及平台就移到框架。

## 隐私

- 默认不收集或上传对话、源码和使用遥测；不将开发对话、提示词、会话记录或本机配置提交、打包或放入演示工程。
- 文档、提交、截图、日志导出与诊断报告不得暴露本机用户名、主机名、设备标识、绝对路径、凭据或私有仓库地址。使用工程相对路径、统一占位符与合成数据；保留上游公开署名及许可证原文。
- 运行时路径与原始输出按需留在本机；导出统一脱敏，默认不含源码正文或对话，预览后由用户选择保存或分享。不得自动上传，脱敏失败不得回退导出原文。
- 打包采用明确文件清单，排除会话记录、`.env`、密钥、机器配置、日志、缓存和 Git 历史；发布检查产物及调试信息中的机器路径。

## 验证

- 按改动运行 `mcpp build`、相关 `mcpp test` 和真实编辑交互检查；区分构建、打包、运行与真机验证。
- `release-0.0.1` 至少 Linux、Windows、Android、macOS 四端达到设计方案第 10 节的产品级标准：实际安装包、真实设备/原生系统、GUI/CLI 开发闭环及升级恢复均须有证据。未测、跳过、仅编译、Wine 或模拟器均不能替代必需原生/真机验收。
- Linux：本地构建和 GUI 验证；GTK 等依赖由 HuxerUI 的 mcpp 规则解析，不假定系统开发包已安装。
- Windows：交叉构建后用 Wine 验证启动与交互，不能替代原生 Windows 验证。
- Android：构建 APK，先用 `adb devices -l` 确认设备就绪，再安装测试；关注 IME、触摸选择、软键盘、后台恢复。不得把 USB 可见当成 ADB 可用。
- 本机 ADB 若设备列表为空，执行 `adb kill-server`，再执行 `ADB_LIBUSB=0 adb start-server` 后重新检查；不在其他设备测试进行中重启服务。
- Web：Emscripten 构建，通过 HTTP 服务在浏览器验证输入、资源与持久化；浏览器运行能力单独评估。
- iOS/macOS 验证使用对应 Apple 环境；手机本地工具进程的运行能力须独立验证。

本机环境检查（2026-10-22）：mcpp 2026.10.21.1；Wine 9.0 已运行 `cmd /c ver`；有图形会话、GTK payload、Emscripten、Chrome 和 Playwright 浏览器。Xiaomi 13 曾通过 `ADB_LIBUSB=0` 启动的 ADB 服务连接为 `device`，读到机型与 Android 13；随后掉线，测试前须重新确认连接。以上是环境检查，未验证 meditor 应用。
