# 语言服务通知与保存隔离

保存文件与向 mcppls 发送通知具有不同结果。原先在写盘并清除 dirty 后，直接发送 `didSave`；后端退出会让已成功的保存报告失败，后端不读取消息则可能阻塞管道写入。

现在语言服务通过独立发送线程和有上限的队列发送 JSON-RPC。通知入队不等待后端读取；写线程在队列互斥区之外执行 I/O。请求保留 FIFO 顺序并等待对应响应，超时或结束后丢弃迟到/未知 ID 的响应。保存已同步打开的文档时，先排队当前修订的 `didChange`，再排队 `didSave`，避免后端观察到旧文本。

`document.save` 成功结果保留 `file-write` effect，并附带 `languageSaveNotification`：

| state | 含义 |
| --- | --- |
| `not-running` | 尚未启动语言服务 |
| `not-open` | 该文档尚未同步打开，未发送保存通知 |
| `queued` | 通知已经入队；不表示后端已收到或处理 |
| `unavailable` | 通知无法入队，附带错误 `code`；文件仍已保存 |

GUI 将最后一种结果显示为“已保存，但未能同步到语言服务。”，不冒充文件保存失败。服务后来退出不会改变已经完成的文件写入结果。`workspace.status.language` 显示传输状态、排队消息/字节数、后端状态和不含原始消息的故障说明；后续语言操作可重建已停止的服务，并重新发送打开文档。信任检查仍先于工具启动。

`capabilities.languageTransport` 公布当前边界：JSON 消息正文 16 MiB；排队帧总计 64 MiB、256 条，均计入正在发送的帧；单次管道写入超时 5000 ms。队列满或消息超限有明确错误，编辑和文件保存继续可用。`maxDocumentBytes` 仍是 32 MiB，文档字节数和 JSON 编码后的消息大小不是同一限制。

平台进程输入支持超时与 stop token。POSIX 使用非阻塞管道、短 poll 和 EINTR 重试；Windows 使用独立的真实线程句柄与限定作用域的同步 I/O 取消监视器，覆盖取消发生在写入调用之间的情况，并在返回前停止监视器，避免影响后续 I/O。失败后关闭输入，不能继续复用半个 JSON-RPC 帧。语言服务销毁先取消发送/读取线程并等待结束，再释放进程和管道。实现依据：[Windows 线程句柄](https://learn.microsoft.com/en-us/windows/win32/procthread/thread-handles-and-identifiers)、[同步 I/O 取消与竞态](https://learn.microsoft.com/en-us/windows/win32/desktop/FileIO/canceling-pending-i-o-operations)；原生 Windows 分支已纳入 CI 专项用例，结果以对应提交的完整 CI 为准，不推断任意驱动都可取消。

验证包括进程不读取 stdin 时的超时/取消、后续文件 I/O 不受取消影响、完整分块输入；实际 JSONL/LSP 子进程覆盖正常保存的版本/文本顺序、消息超限后仍可保存、退出后保存与重连、服务器请求洪泛导致背压后的保存与有界停止。真实固定版本 mcpp/mcppls 回归继续覆盖 Modules 构建运行和语言查询。

这不是完整 A05/A06/A09 验收。语言查询/初始化的调用接口仍同步等待；GUI 语言功能、后台请求及输入修订关联、跨进程共享会话、完整诊断缓存上限、优雅关闭、四端原生故障与性能证据继续待完成。当前故障用例的秒级防挂死阈值不能替代产品帧时间或输入延迟预算；GUI 的语言故障提示实际闭环也需随语言功能 GUI 完善后验证。
