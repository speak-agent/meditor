# meditor 使用 openkal 的可行性分析

日期：2026-10-22。状态：已完成接口/构建资料审查和 Linux 小范围探针，尚未完成四端 GUI 集成实验。本文补充[项目方案](2026-10-22-meditor-project-design.md)，不代表 meditor 已迁移到 openkal。

## 1. 结论

**建议将 openkal 作为 meditor 平台子系统的优先候选，先验证直接使用其 ABI 并保留目标原生运行时，不在首版未经验证就把 HuxerUI GUI 进程整栈迁到 openkal-musl/libc++。**

它可以减少文件、流、子进程、取消/超时等平台实现重复，并与 mcpp 的目标解析及 mcppls 现有生态衔接。它本身不提供窗口、绘制、IME、SAF、AppKit 授权、安装签名，也不能在浏览器或手机沙箱里凭空提供进程执行。

“统一源码 + 声明目标依赖 + mcpp 交叉构建”有可行基础；“加上 openkal 就自动让整个编辑器在所有系统工作”没有证据。编译、链接、应用打包、SDK 获取、签名和实际运行是不同阶段。

## 2. 检查依据

| 组件 | 本次检查版本/提交 | 观察 |
| --- | --- | --- |
| openkal | 0.15.0 / `f5bc061a4432e0575abbb6a493c773530211b51b` | 声明和 ABI 契约，既有 C 声明也有 Modules 导出，单独不是实现 |
| openkal-linux | 0.16.1 / `e554292cc82d6d52194a6b30e3bd97de7f8ed839` | Linux 内核实现，声明文件/进程/任务/超时等接口 |
| openkal-windows | 0.11.0 / `623d27130ef7a7a8af01fe0167c600e05ea5093c` | Windows 实现；接口/属性集合需按实际目标检查，不能假定 POSIX fork |
| openkal-macos | 0.13.0 / `f58cf4b2a89ba0702bda2fb71b92fc8a66d4ee0f` | macOS 实现；源码/库边界与 GUI 仍需集成验证 |
| openkal-emscripten | 0.4.0 / `4d296885e7db42c3fbe876919bce1de6508f7493` | 不提供 process/exec/space；线程由 feature 与整图 ABI 配置决定 |
| openkal-musl | 0.20.1 / `7fe6d886643fa4634f0c55bedc415234e3150239` | 基于 openkal 的 C 运行时层，并非 openkal 接口本身 |
| openkal-llvm-runtime | 0.15.4 / `b4198d346cf0682c95dc0e51b5c32b096843ee83` | 面向 openkal-musl 的 libc++/libc++abi/libunwind 层，改变整图运行时约束 |

证据来自上述本地工作树、openkal 官方 README/接口、mcppls 的 `modules/platform` 与 HuxerUI 已固定基线的构建设计。上游 README 部分示例版本落后于实现 manifest，选型不能复制示例就宣称版本已兼容；本表不是产品依赖 lockfile。

## 3. 四层分别决定什么

| 层 | 责任 | meditor 采用方式 |
| --- | --- | --- |
| 标准 C++ 与 meditor 文档/编辑核心 | 事务、位置、算法、状态 | 无窗口/无平台依赖，优先保持纯 Modules |
| openkal 接口 + 目标实现 | 文件、流、进程、时间、任务等操作 | 在可复用的平台子系统内适配，导出 meditor 的语义接口而非散落 kal 调用 |
| C/C++ 运行时 | libc、libc++、异常、TLS、分配释放 | 初期沿用 HuxerUI 目标图选择；切换 musl/libc++ 是独立决策 |
| HuxerUI + 平台 SDK + mcpp 规则 | GUI、IME、平台授权、目标编译、资源与分发 | 保持现有边界；缺少 SDK、签名或系统能力仍需解决 |

openkal 可以与平台接口共存，但任何同进程方案都要证明实际 ABI 边界。一个 image 的 C/C++ 运行时所有权必须一致；不得把两个 libc 拥有的 `FILE*`、内存、异常对象或线程局部状态混用。单独工具进程使用自己的运行时与 GUI 通过 IPC 通信，则有不同的隔离边界，不代表能把它的库直接链接入 GUI。

## 4. 适用范围与能力检查

| meditor 能力 | openkal 对应接口/机制 | 必须补充验证 |
| --- | --- | --- |
| 项目文件读取与安全保存 | fs、stream、目录相对能力 | rename/权限/符号链接/磁盘失败/刷盘语义；provider 路径另行处理 |
| 启动 mcpp/mcppls、运行用户程序 | process、spawn 的工作目录/流/目录授权/job | stdin/stdout/stderr 隔离、Unicode argv、env、错误码、后代取消和句柄关闭 |
| LSP 和任务输出 | stream、process channel、timeout | 双向背压、大消息、半关闭、超时和 EOF；读取卡死不拖住 UI |
| 子任务和截止时间 | task、time、timeout、process 属性 | 当前实现是否具备完整接口/相关 property，线程与 GUI 事件循环契约 |
| CLI 会话与授权 | 可用的流/网络/随机源作为原语 | Unix socket/named pipe、当前用户身份与平台授权仍由产品适配，不等价于已有安全 RPC |
| 系统文件授权与桌面服务 | 不直接覆盖 | Android SAF、Apple security scope、文件选择、剪贴板等继续由 HuxerUI/meditor 平台策略负责 |

先声明需要的接口并检查实现实际提供集合，再检查具体属性。process 的标准流、job、生命周期等属性不能仅凭有 `process` 名称就视为具备。缺少接口可能在解析或链接阶段被拒绝；不得用永远失败的空实现让配置看似跨平台。

## 5. 平台判断

**Linux：** 直接使用 ABI 的候选成熟度最高，已有小范围本地运行证据。现有 HuxerUI Linux 图依赖 GTK/glibc 栈；保留该图的 C runtime，先接 openkal process/fs。不得把旧 POSIX `fork`/signal/epoll 实现直接放到 openkal-musl 上而假定可用。

**Windows：** 有独立实现和 mcpp 目标支持，是可行候选。HuxerUI 基线使用 MSVC ABI/Windows SDK 路径；需要证明 openkal 的目标对象、import libraries、原生 C/C++ runtime 和工具载荷匹配。Linux 交叉生成 PE、Wine 运行和原生 Windows 安装/GUI/IME测试分别记录。

**macOS：** 有独立实现；AppKit/Objective-C++、部署版本、C++ runtime、framework 链接与签名不能由 openkal 替代。先在原生 Mac 建立直接 ABI 与 GUI 的同进程探针，最低版本取各组件交集。Linux 交叉产出 Mach-O 不等于拿到了可分发、可原生运行的 app。

**Android：** 不能因 `os = linux` 或存在 PRoot 支持就选择桌面 Linux 栈。NDK/Bionic、应用 UID/SELinux、进程可执行位置、存储授权、JNI 回调线程，以及工具的加载器/运行时必须真机验证。若选择 Linux 兼容宿主，宿主与 APK 是不同环境，连接、文件映射和用户可见行为必须作为产品功能实现。当前没有原生 Android GUI + openkal 完整开发闭环证据。

**Web：** openkal-emscripten 明确缺少 process/exec/space；线程依赖整图 threads feature 与浏览器部署条件。可以复用有实现的底层接口，不能通过它直接启动桌面工具。它是附加探索目标，不能替代 Android 或四个必达平台验收。

## 6. 本次 Linux 探针

在仓库之外的临时工程，用 mcpp 2026.10.21.1、LLVM 23.1.3、目标 `x86_64-linux-gnu` 编译一个 Modules 程序。探针依赖 openkal-linux 0.16.1 及其 openkal 声明接口，直接 `import std` 和 `openkal.fs/process/timeout/stream`；没有引入 openkal-musl 或 openkal-llvm-runtime。

执行了：枚举预打开目录 → 创建明确工作目录的 spawn 配置 → 启动系统的成功退出程序 → 有界等待 → 检查退出码/终止状态 → 释放进程句柄。`mcpp run` 返回 0，实际输出 `native C++ runtime + openkal modules: spawn and bounded wait passed`。ELF 动态依赖检查可见 `libc.so.6`、`libm.so.6` 及 Linux loader，支持“该小程序未替换为 openkal-musl”的判断。

这仅证明 Linux 小范围直接 ABI 与原生运行时组合可编译并运行，不证明 HuxerUI 同进程、文件安全保存、LSP 背压、任务树回收、其他平台、跨编译或 GUI 性能通过。临时工程不包含 meditor 工作区修改，也没有发布产物；不提交机器路径或原始会话输出。正式采用前，应将等价的、无机器依赖的受控探针转为 PR #1 中的自动化测试。

## 7. 验证顺序与采用门槛

1. 在 workspace 平台候选包中定义产品需要的文件/进程/流语义，保持上层无 OS 头或 kal 句柄耦合；独立包仍需满足可复用子系统标准，不为每个接口建包。
2. Linux 扩展探针：完整文件保存、子进程三流、超时、取消树、LSP 大消息；与原生实现作同语义测试，不以新增 abstraction 层为成功。
3. Windows/macOS 原生重复相同核心契约测试，验证 HuxerUI GUI 同进程使用；记录 ABI、链接产物、线程/TLS 和异常路径。Android 在真机验证原生或受控本地宿主的实际方案。
4. 由 manifest 条件选择实现，mcpp 明确 target 编译；对每个声明的 host→target 组合记录编译/链接/包结构，随后用实际 OS/设备运行测试。缺 SDK 时补工具供应或使用相应原生 CI，不创建伪成功矩阵。
5. 只有实现集合、运行时兼容、资源回收和四端产品用例都通过，才将其作为默认平台实现。整栈 musl/libc++ 替换若不能证明收益与 GUI 兼容，继续后置，不阻塞原生运行时路径。

避免过度承诺：mcppls 已用 openkal 是有价值的生态证据，但其服务端进程模型和 GUI 不同；不修改其源码，也不直接复制其带运行时闭包的平台包作为 meditor 默认依赖。

## 8. Review 建议

- 确认“平台能力优先评估 openkal、编辑核心保持标准 C++、GUI 保留原生 runtime”作为首版研究路线。
- 确认自动交叉构建的验收粒度是指定 host/target 的完整产物和真实运行，不是泛指所有 OS。
- 不将该调研标为四端实现完成；缺少的 Windows/macOS/Android 同进程/宿主证据进入产品验收矩阵。

## 9. 资料

- [openkal README 与层次边界](https://github.com/mcpplibs/openkal/blob/f5bc061a4432e0575abbb6a493c773530211b51b/README.md)
- [openkal process 接口](https://github.com/mcpplibs/openkal/blob/f5bc061a4432e0575abbb6a493c773530211b51b/include/openkal/process.h)
- [Linux 实现](https://github.com/mcpplibs/openkal-linux/tree/e554292cc82d6d52194a6b30e3bd97de7f8ed839)
- [Windows 实现](https://github.com/mcpplibs/openkal-windows/tree/623d27130ef7a7a8af01fe0167c600e05ea5093c)
- [macOS 实现](https://github.com/mcpplibs/openkal-macos/tree/f58cf4b2a89ba0702bda2fb71b92fc8a66d4ee0f)
- [Emscripten 能力边界](https://github.com/mcpplibs/openkal-emscripten/blob/4d296885e7db42c3fbe876919bce1de6508f7493/README.md)
- [HuxerUI 基线构建与 ABI](https://github.com/Sunrisepeak/HuxerUI/blob/0a207a56b598e102d7eab9dd52fd36933bd9e9c7/docs/design/mcpp-build-system.md)
- [mcpp workspace](https://github.com/mcpp-community/mcpp/blob/main/docs/07-workspace.md)

资料是接口与实现证据，不是 meditor 的产品验证报告。
