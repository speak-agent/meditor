# C++ 基础高亮：实现与边界

状态：H01 的部分实现，不代表第 17 节完整交付或 A01–A13 产品验收通过。

## 实现

`modules/document/src/syntax.cppm` / `.cpp` 提供不依赖 GUI、LSP 或平台头文件的 `meditor.syntax` 模块，作为文档包内的文本处理能力；没有为少量代码新增独立 package。已检查本地 mcpp-index 的现有条目，未找到可直接采用的 tokenizer/Tree-sitter 包。本轮采用设计允许的保守词法实现；没有据此排除后续语法库，也没有宣称完成候选库的四端评估。

按扩展名识别 C/C++ 和 Modules 接口文件，其他文件暂用纯文本。基础角色包括 keyword、comment、string、number、directive。覆盖 Modules 关键词、跨行块注释、带自定义 delimiter 的原始字符串、普通字符串与字符、常见编码前缀、数值分隔符及预处理指令名称。`import` / `module` 按词法关键词着色，不做上下文解析。不提供类型分析或完整 C++ 预处理翻译阶段；复杂转义换行、宏展开、头文件名等仍须补充专门用例与语法支持。

按行缓存文本、输入/输出词法状态和 token；编辑后状态不同的后续行重算，相同行与相同输入状态复用。当前文档拆行和缓存遍历仍为线性操作，尚未证明大文件性能预算。超过约 2 MiB 文本预算、任一行 16 KiB 或 65,536 个 token 时整份高亮退回纯文本；UI 显示 en/zh 受限提示，编辑与保存继续使用原文。文本预算包含逐行边界开销。预算不是已经实测达标的性能承诺。

GUI 把 token 字节范围转换为 UTF-16 属性范围，通过一份 `AttributedText` 段落绘制，不拆成独立文本片段。深浅主题各有配色；选择背景、光标和编辑坐标仍由原有输入模型提供。此处没有宣称完成 IME composition 装饰、诊断、查找、语义 token 或原生字体整形验收。

## CLI / Agent

通过现有 meditor JSONL 会话发送：

```json
{"command":"document.highlight","params":{"document":"<document-id>","startLine":0,"maxLines":100}}
```

返回当前 `revision`、`language`、`layer: lexical`、`offsetEncoding: utf-8-bytes-per-line`、`limited`、`truncated`、`totalLines`、`startLine`、`completeLines` 和 token。行号与字节范围从零开始，end 不包含在内。`maxLines` 范围 1–1000；一次最多返回 4096 个 token，`truncated` 表示结果在所请求范围内被 token 上限截断，`completeLines` 只计完整处理的行，消费者不能把部分行当完整结果。分段请求以相同行号重读时仍须核对 revision。超长单行和全文降级通过 `limited` 区分。

查询不执行工具、不写文件，也不改变文档 revision。GUI/CLI 使用同一个词法模块；尚未实现的 GUI/CLI 跨进程共享会话问题仍然存在。

## 验证与剩余要求

文档包测试覆盖跨行注释、raw string、Modules、数字、UTF-8 边界、状态传播、有限预算降级，以及 300 次随机行编辑后增量结果与冷扫描的一致性。CLI 回归覆盖 schema、revision、范围参数、结果截断、分页与长行降级。GUI headless 测试检查实际绘制快照中的属性范围、深浅配色差异以及两个窗口尺寸下中文/emoji 选择和原文不变；既有工作台测试继续验证触摸/鼠标、输入、保存及撤销重做。

Markdown、JSON/JSONC、TOML/YAML/INI 和其他常见语言、用户语言覆盖、LSP semantic tokens 分层、按 revision 接纳异步结果、可靠的大文件增量布局、完整主题/叠加对比度与四端实际呈现和性能证据仍待实现。不得用本轮 C++ token 测试或 CI 编译替代这些要求。
