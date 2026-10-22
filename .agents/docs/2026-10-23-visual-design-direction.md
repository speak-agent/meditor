# meditor 视觉与交互设计方向稿（R08 · 待 review 选定）

状态：2026-10-23 · 三选一（或混排）评审稿。本文只定义设计语言与验收标准，不改动实现；方向选定后按 §6 映射进 `src/ui/theme.cpp` 与各视图，并以真实视图树截图对照本稿验收（D8：不以“能用”代替“达标”）。

## 1. 现状诊断（为什么不及格）

当前 `theme.cpp` 是 FlatTheme 模板 + 通用蓝（`116,171,255`）+ 系统字体 + 6px 圆角 + 8–12ms 微动效。问题不是“难看”，而是**没有身份**：

- 色彩、圆角、字重全部来自通用组件库默认值，任何 AI 生成的“现代深色主题”都长这样（D8 禁止的大众 AI 味）。
- 没有从 mcpp/Modules 开发场景推导的任何视觉决策：模块依赖、构建阶段、诊断、修订号——这些 meditor 独有的信息没有视觉语汇。
- 状态表达只靠颜色深浅；动效只有 hover/press 两层，无状态语义。

## 2. 共享骨架（三方向都不变）

- 自绘标题栏 + 底部状态栏；安全区、窗口控制、IME 优先级照旧。
- 布局：左工作区导航 / 中编辑区（Tabs）/ 底部面板（问题·输出·大纲）/ 状态栏。
- 全部文字与控件满足对比度 ≥ 4.5:1；颜色不是唯一状态载体（配字形/图标/文本）。
- 减少动画模式保留全部状态信息（D8 + §16.5 不变项）。

## 3. 方向 A —— 制图台 Drafting Table

**隐喻**：游标卡尺与工程图纸。meditor 是测量代码的仪器：行列号、修订号、构建阶段都是“读数”。

| 变量 | 值 |
| --- | --- |
| 深色底 | 石墨 `#12151A` / 面板 `#1A1F26` / 浮层 `#232A33` |
| 浅色底 | 图纸白 `#F7F6F3` / 面板 `#EFEDE8` / 浮层 `#E7E4DD` |
| 强调 | 工程橙 `#E8590C`（深浅同值，浅色下加深为 `#C2410C`） |
| 墨/纸文字 | `#E8EAED` / `#21262E`；次级 `#9AA3AF` / `#5B6470` |
| 边框 | 1px `#2E3742`（深）/ `#D6D2C8`（浅）；**焦点 = 2px 橙实线**，不用光晕 |
| 圆角 | 2px（控件）、0px（面板/输入），拒绝胶囊 |
| 字体 | UI：无衬线 13px；**读数（行列号/大小/耗时/修订号）一律等宽**，像仪器刻度 |
| 图标 | 1.5px 线性，24 网格，端点方头（butt cap） |
| 空态 | 8px 淡网格纹理（仅空态与画布，工作面永远干净） |
| 动效 | 机械精密：90–140ms，`cubic-bezier(0.2, 0, 0, 1)`（快进慢停，无回弹）；进度 = 刻度块推进，不用渐变条 |

```svg
<svg viewBox="0 0 960 560" xmlns="http://www.w3.org/2000/svg" font-family="ui-monospace,'Cascadia Mono',monospace">
<rect width="960" height="560" fill="#12151A"/>
<!-- 标题栏 -->
<rect width="960" height="36" fill="#1A1F26"/>
<rect x="0" y="36" width="960" height="1" fill="#2E3742"/>
<g fill="#E8590C"><rect x="12" y="12" width="4" height="12"/><rect x="18" y="18" width="4" height="6"/><rect x="24" y="12" width="4" height="12"/><rect x="32" y="12" width="4" height="4"/><rect x="32" y="20" width="4" height="4"/><rect x="40" y="14" width="3" height="3"/><rect x="46" y="14" width="3" height="3"/><rect x="43" y="21" width="3" height="3"/></g>
<text x="56" y="23" fill="#E8EAED" font-size="12" font-family="system-ui,sans-serif">meditor — hello 工程示例</text>
<text x="880" y="23" fill="#9AA3AF" font-size="12">—  ▢  ✕</text>
<!-- 左侧文件树 -->
<rect x="0" y="37" width="192" height="487" fill="#161A20"/>
<text x="14" y="60" fill="#9AA3AF" font-size="11">工作区</text>
<text x="14" y="80" fill="#E8EAED" font-size="12">▾ modules/</text>
<text x="26" y="97" fill="#9AA3AF" font-size="12">  document/</text>
<text x="26" y="114" fill="#9AA3AF" font-size="12">  application/</text>
<text x="14" y="131" fill="#E8EAED" font-size="12">▾ src/</text>
<text x="14" y="148" fill="#E8590C" font-size="12">▸ hello.cppm ●</text>
<text x="14" y="165" fill="#9AA3AF" font-size="12">  main.cpp</text>
<text x="14" y="182" fill="#9AA3AF" font-size="12">  mcpp.toml</text>
<text x="14" y="199" fill="#9AA3AF" font-size="12">  mcpp.lock</text>
<!-- Tabs -->
<rect x="192" y="37" width="768" height="34" fill="#1A1F26"/>
<rect x="192" y="37" width="140" height="34" fill="#12151A"/>
<rect x="192" y="68" width="140" height="2" fill="#E8590C"/>
<text x="204" y="58" fill="#E8EAED" font-size="12">hello.cppm</text>
<text x="342" y="58" fill="#9AA3AF" font-size="12">main.cpp   mcpp.toml</text>
<!-- 编辑区 -->
<rect x="192" y="71" width="768" height="388" fill="#12151A"/>
<g font-size="12">
<text x="204" y="92"  fill="#4A5361">1</text><text x="232" y="92"  fill="#5B6470">// hello.cppm — 模块接口</text>
<text x="204" y="110" fill="#4A5361">2</text><text x="232" y="110" fill="#E8590C">export</text><text x="316" y="110" fill="#E8EAED"> </text><text x="326" y="110" fill="#E8590C">module</text><text x="402" y="110" fill="#E8EAED"> hello;</text>
<text x="204" y="128" fill="#4A5361">3</text>
<text x="204" y="146" fill="#4A5361">4</text><text x="232" y="146" fill="#E8590C">import</text><text x="308" y="146" fill="#E8EAED"> std;</text>
<text x="204" y="164" fill="#4A5361">5</text>
<text x="204" y="182" fill="#4A5361">6</text><text x="232" y="182" fill="#E8590C">export</text><text x="316" y="182" fill="#E8EAED"> </text><text x="326" y="182" fill="#E8590C">void</text><text x="386" y="182" fill="#E8EAED"> greet(</text><text x="478" y="182" fill="#7FB0E8">std::string_view</text><text x="658" y="182" fill="#E8EAED"> who) {</text>
<text x="204" y="200" fill="#4A5361">7</text><text x="254" y="200" fill="#E8EAED">    std::println(</text><text x="416" y="200" fill="#7FBF7F">"你好, {}!"</text><text x="536" y="200" fill="#E8EAED">, who);</text>
<text x="204" y="218" fill="#4A5361">8</text><text x="254" y="218" fill="#E8EAED">}</text>
<text x="204" y="236" fill="#4A5361">9</text><text x="232" y="236" fill="#E8EAED">▏</text>
</g>
<!-- 底部：问题面板 -->
<rect x="192" y="459" width="768" height="65" fill="#161A20"/>
<rect x="192" y="459" width="768" height="1" fill="#2E3742"/>
<text x="204" y="476" fill="#9AA3AF" font-size="11">问题 1 · 输出 · 大纲</text>
<text x="204" y="496" fill="#E8B339" font-size="12">⚠ P0201 未使用的 import『std』 hello.cppm:4:1</text>
<text x="204" y="514" fill="#5B6470" font-size="11">mcppls · 就绪 · 诊断对齐修订 r7</text>
<!-- 状态栏：仪器读数 -->
<rect x="0" y="524" width="960" height="36" fill="#1A1F26"/>
<rect x="0" y="524" width="960" height="1" fill="#2E3742"/>
<text x="14"  y="546" fill="#E8590C" font-size="11">⏻ mcpp 2026.10.21.1</text>
<text x="150" y="546" fill="#9AA3AF" font-size="11">hello.cppm · C++ Modules</text>
<text x="874" y="546" fill="#9AA3AF" font-size="11">UTF-8·LF·07:24</text>
</svg>
```

## 4. 方向 B —— 纸墨 Ink

**隐喻**：代码即文稿。深色是砚台，浅色是稿纸；编辑是书写的延续，构建是“落款”。

| 变量 | 值 |
| --- | --- |
| 深色底 | 炭墨 `#17181C` / 面板 `#1F2126` / 浮层 `#282A31` |
| 浅色底 | **米白纸 `#FAF7F0`** / 面板 `#F2EEE5` / 浮层 `#EAE5D9` |
| 强调 | 朱砂 `#B4402F`（浅）/ 柿 `#D96C4F`（深）；链接与跳转用靛青 `#31579C` |
| 墨/纸文字 | `#ECEAE4` / `#26241F`；次级 `#98948A` / `#6B675C` |
| 边框 | 1px 墨线 `#33353B` / `#DDD7C9`；分隔线像笔锋（两端渐隐可用 α 渐变实现，但仅分隔线） |
| 圆角 | 4px 温和；卡片不描边，用**纸面层次**（明度差）区分 |
| 字体 | UI：13px 无衬线；**界面标题可用衬线**（仅欢迎页/面板标题，编辑器永不用）；代码等宽 13px/1.6 行高 |
| 图标 | 面性–线面混合，笔触收角圆润；强调图标可用朱砂单色填充 |
| 空态 | 欢迎页右侧一行竖排小字（如“轻舟已过万重山”级克制文案位），不使用插画 |
| 动效 | 墨迹：140–200ms `cubic-bezier(0.33,1,0.68,1)`（ease-out-cubic，稍慢而稳）；面板展开像铺纸；**无任何弹跳/闪烁**；未保存圆点呼吸 2s 周期 α 0.5→1 |

```svg
<svg viewBox="0 0 960 560" xmlns="http://www.w3.org/2000/svg" font-family="ui-monospace,'Cascadia Mono',monospace">
<rect width="960" height="560" fill="#17181C"/>
<rect width="960" height="38" fill="#1F2126"/>
<g fill="#D96C4F"><rect x="14" y="13" width="4" height="12"/><rect x="20" y="19" width="4" height="6"/><rect x="26" y="13" width="4" height="12"/><rect x="34" y="13" width="4" height="4"/><rect x="34" y="21" width="4" height="4"/><rect x="42" y="15" width="3" height="3"/><rect x="48" y="15" width="3" height="3"/><rect x="45" y="22" width="3" height="3"/></g>
<text x="58" y="24" fill="#ECEAE4" font-size="13" font-family="Georgia,serif">meditor</text>
<text x="880" y="24" fill="#98948A" font-size="12">—  ▢  ✕</text>
<rect x="0" y="38" width="196" height="484" fill="#1B1D22"/>
<text x="16" y="62" fill="#98948A" font-size="11" font-family="Georgia,serif" font-style="italic">工作区</text>
<text x="16" y="84" fill="#ECEAE4" font-size="12">▾ modules/</text>
<text x="28" y="101" fill="#98948A" font-size="12">document/</text>
<text x="28" y="118" fill="#98948A" font-size="12">application/</text>
<text x="16" y="135" fill="#ECEAE4" font-size="12">▾ src/</text>
<text x="16" y="152" fill="#D96C4F" font-size="12">▸ hello.cppm ●</text>
<text x="16" y="169" fill="#98948A" font-size="12">  main.cpp</text>
<text x="16" y="186" fill="#98948A" font-size="12">  mcpp.toml</text>
<rect x="196" y="38" width="764" height="36" fill="#1F2126"/>
<rect x="196" y="38" width="144" height="36" rx="4" fill="#26241F" opacity="0.6"/>
<text x="210" y="60" fill="#ECEAE4" font-size="12">hello.cppm</text>
<text x="352" y="60" fill="#98948A" font-size="12">main.cpp   mcpp.toml</text>
<rect x="196" y="74" width="764" height="384" fill="#17181C"/>
<g font-size="12.5">
<text x="208" y="96"  fill="#5C5952">1</text><text x="236" y="96"  fill="#6B675C">// hello.cppm — 模块接口</text>
<text x="208" y="115" fill="#5C5952">2</text><text x="236" y="115" fill="#D96C4F">export module</text><text x="384" y="115" fill="#ECEAE4"> hello;</text>
<text x="208" y="134" fill="#5C5952">3</text>
<text x="208" y="153" fill="#5C5952">4</text><text x="236" y="153" fill="#D96C4F">import</text><text x="312" y="153" fill="#ECEAE4"> std;</text>
<text x="208" y="172" fill="#5C5952">5</text>
<text x="208" y="191" fill="#5C5952">6</text><text x="236" y="191" fill="#D96C4F">export void</text><text x="376" y="191" fill="#ECEAE4"> greet(</text><text x="468" y="191" fill="#8FA8C8">std::string_view</text><text x="648" y="191" fill="#ECEAE4"> who) {</text>
<text x="208" y="210" fill="#5C5952">7</text><text x="258" y="210" fill="#ECEAE4">    std::println(</text><text x="420" y="210" fill="#9FBF8E">"你好, {}!"</text><text x="540" y="210" fill="#ECEAE4">, who);</text>
<text x="208" y="229" fill="#5C5952">8</text><text x="258" y="229" fill="#ECEAE4">}</text>
<text x="208" y="248" fill="#5C5952">9</text><text x="236" y="248" fill="#D96C4F" opacity="0.9">▏</text>
</g>
<rect x="196" y="458" width="764" height="64" fill="#1B1D22"/>
<text x="210" y="476" fill="#98948A" font-size="11" font-family="Georgia,serif" font-style="italic">问题 1 · 输出 · 大纲</text>
<text x="210" y="497" fill="#D9A441" font-size="12">⚠ P0201 未使用的 import『std』 hello.cppm:4:1</text>
<text x="210" y="515" fill="#6B675C" font-size="11">mcppls · 就绪 · 诊断对齐修订 r7</text>
<rect x="0" y="522" width="960" height="38" fill="#1F2126"/>
<text x="16"  y="546" fill="#D96C4F" font-size="11">⏻ mcpp 就绪</text>
<text x="150" y="546" fill="#98948A" font-size="11">hello.cppm · C++ Modules</text>
<text x="874" y="546" fill="#98948A" font-size="11">UTF-8·LF·07:24</text>
</svg>
```

## 5. 方向 C —— 终端血统 Terminal Native（推荐基底）

**隐喻**：Dev-C++ 的现代转世，也是 mcpp 社区的母语——终端。编辑器本体克制到近乎黑屏，让代码与状态成为唯一色彩。

| 变量 | 值 |
| --- | --- |
| 深色底 | 近黑 `#0B0F14` / 面板 `#10161D` / 浮层 `#16202A` |
| 浅色底 | 冷白 `#F4F6F8` / 面板 `#E9EDF1` / 浮层 `#DFE5EB`（浅主题允许独立演化，首版以深为主） |
| 强调 | 磷绿 `#3DDC84`（深）/ 深绿 `#1B7F4D`（浅）；错误 `#FF5370`、警告 `#FFB62E`、信息 `#6EC5FF` 沿用终端语义色 |
| 墨/纸文字 | `#D7E1E8` / `#1B2530`；次级 `#7C8B98` / `#5A6B78` |
| 边框 | 1px 硬边 `#1D2833`（深）；**选中/活动一律左侧 2px 实色条**（tmux 血统），不用阴影 |
| 圆角 | 0px 面板 / 2px 控件；像素标识的方像素血统一以贯之 |
| 字体 | 全局等宽优先（UI 文本也用等宽 12.5px——终端血统的核心决策）；读数右对齐 |
| 图标 | 像素/方头线性 16 网格，与 me/mc++ 标识同族；状态图标用语义色实心 |
| 空态 | 欢迎页 = 终端启动画面：像素 logo + 一行版本读数 + 三个动作（打开/新建/Quick Start），无插画无渐变 |
| 动效 | 快而克制：80–120ms `steps(3, end)` 或线性——像终端逐行输出，**不做平滑位移**；光标块闪烁 530ms 周期（减少动画时静止）；进度 = 方块字符进度条 `▮▮▮▯▯` 式推进 |
| 独有 | 状态栏 = statusline 语言（左：会话/工程/分支；右：编码·行尾·行列·fps）；问题面板行首用 `✗ ⚠ ⓘ` 语义字形 |

```svg
<svg viewBox="0 0 960 560" xmlns="http://www.w3.org/2000/svg" font-family="ui-monospace,'Cascadia Mono',monospace">
<rect width="960" height="560" fill="#0B0F14"/>
<rect width="960" height="34" fill="#10161D"/>
<rect x="0" y="34" width="960" height="1" fill="#1D2833"/>
<g fill="#3DDC84"><rect x="12" y="11" width="4" height="12"/><rect x="18" y="17" width="4" height="6"/><rect x="24" y="11" width="4" height="12"/><rect x="32" y="11" width="4" height="4"/><rect x="32" y="19" width="4" height="4"/><rect x="40" y="13" width="3" height="3"/><rect x="46" y="13" width="3" height="3"/><rect x="43" y="20" width="3" height="3"/></g>
<text x="56" y="22" fill="#D7E1E8" font-size="12.5">meditor — hello 工程示例</text>
<text x="882" y="22" fill="#7C8B98" font-size="12">— ▢ ✕</text>
<rect x="0" y="35" width="188" height="487" fill="#0D1218"/>
<text x="12" y="57" fill="#7C8B98" font-size="11">工作区</text>
<text x="12" y="77" fill="#D7E1E8" font-size="12">▾ modules/</text>
<text x="24" y="94" fill="#7C8B98" font-size="12">  document/</text>
<text x="24" y="111" fill="#7C8B98" font-size="12">  application/</text>
<text x="12" y="128" fill="#D7E1E8" font-size="12">▾ src/</text>
<rect x="8" y="137" width="2" height="14" fill="#3DDC84"/>
<text x="14" y="148" fill="#3DDC84" font-size="12">▸ hello.cppm ●</text>
<text x="14" y="165" fill="#7C8B98" font-size="12">  main.cpp</text>
<text x="14" y="182" fill="#7C8B98" font-size="12">  mcpp.toml</text>
<text x="14" y="199" fill="#7C8B98" font-size="12">  mcpp.lock</text>
<rect x="188" y="35" width="772" height="32" fill="#10161D"/>
<rect x="188" y="35" width="2" height="32" fill="#3DDC84"/>
<text x="200" y="55" fill="#D7E1E8" font-size="12">hello.cppm</text>
<text x="340" y="55" fill="#7C8B98" font-size="12">main.cpp  mcpp.toml  +</text>
<rect x="188" y="67" width="772" height="392" fill="#0B0F14"/>
<g font-size="12.5">
<text x="200" y="88"  fill="#3B4A57">1</text><text x="228" y="88"  fill="#7C8B98">// hello.cppm — 模块接口</text>
<text x="200" y="107" fill="#3B4A57">2</text><text x="228" y="107" fill="#3DDC84">export module</text><text x="376" y="107" fill="#D7E1E8"> hello;</text>
<text x="200" y="126" fill="#3B4A57">3</text>
<text x="200" y="145" fill="#3B4A57">4</text><text x="228" y="145" fill="#3DDC84">import</text><text x="304" y="145" fill="#D7E1E8"> std;</text>
<text x="200" y="164" fill="#3B4A57">5</text>
<text x="200" y="183" fill="#3B4A57">6</text><text x="228" y="183" fill="#3DDC84">export void</text><text x="368" y="183" fill="#D7E1E8"> greet(</text><text x="460" y="183" fill="#6EC5FF">std::string_view</text><text x="640" y="183" fill="#D7E1E8"> who) {</text>
<text x="200" y="202" fill="#3B4A57">7</text><text x="250" y="202" fill="#D7E1E8">    std::println(</text><text x="412" y="202" fill="#C3E88D">"你好, {}!"</text><text x="532" y="202" fill="#D7E1E8">, who);</text>
<text x="200" y="221" fill="#3B4A57">8</text><text x="250" y="221" fill="#D7E1E8">}</text>
<text x="200" y="240" fill="#3B4A57">9</text><text x="228" y="240" fill="#3DDC84">▊</text>
</g>
<rect x="188" y="459" width="772" height="63" fill="#0D1218"/>
<rect x="188" y="459" width="772" height="1" fill="#1D2833"/>
<text x="200" y="476" fill="#D7E1E8" font-size="11">问题 1 ▏输出 ▏大纲</text>
<rect x="188" y="480" width="2" height="14" fill="#FFB62E"/>
<text x="196" y="492" fill="#FFB62E" font-size="12">⚠ P0201 未使用的 import『std』</text>
<text x="640" y="492" fill="#7C8B98" font-size="12">hello.cppm:4:1</text>
<text x="200" y="512" fill="#3B4A57" font-size="11">mcppls 就绪 · 诊断对齐 r7 · 模块图 12 节点</text>
<rect x="0" y="522" width="960" height="38" fill="#10161D"/>
<rect x="0" y="522" width="960" height="1" fill="#1D2833"/>
<text x="12"  y="546" fill="#3DDC84" font-size="11">⏻ mcpp 2026.10.21.1</text>
<text x="160" y="546" fill="#7C8B98" font-size="11">hello · main ✓</text>
<text x="700" y="546" fill="#7C8B98" font-size="11">UTF-8  LF  07:24</text>
<text x="872" y="546" fill="#D7E1E8" font-size="11">r7</text>
</svg>
```

## 6. 像素标识 me/mc++（P02 三配色）

构形规则（§15.1）：小写 `m` 保持完整像素轮廓；右侧 `e` 由开口方形环构成（开口朝右 = `c` 的联想），环的中行嵌入两枚 `+`（即 `e` 的横画）——同时读作 `me` 与 `mc++`。上屏稿中标题栏的 8×12 像素组即此构形的最小实现（m 三竖两拱 + 开口环 + 双加号）。三方向配色：A 工程橙、B 朱砂、C 磷绿（见各屏标题栏）。选定方向后产出 16/32/64/128 全尺寸资产与单色/深浅反白版。

## 7. 动效规范（按 §16.5 语义映射，方向选定后取对应曲线）

| 场景 | A 制图台 | B 纸墨 | C 终端 | 减少动画 |
| --- | --- | --- | --- | --- |
| 面板展开/收起 | 140ms decel | 200ms ease-out-cubic | 120ms steps(3) | 直接切换 |
| hover/press | 90ms 线性淡入 α .12 | 140ms 淡入 α .08 | 80ms 离散两态 | 仅变色 |
| 任务进度 | 刻度块推进 | 墨条铺展 | 方块字符条 | 数字读数 |
| 未保存标记 | 常亮圆点 | 呼吸 2s | 常亮 `●` + 标题 `*` | 常亮 |
| 错误出现 | 行首标尺弹出 120ms | 淡入 180ms | 即时出现 | 即时 |
| 完成通知 | 状态栏读数刷新 | 墨点落定 160ms | 状态行变绿一拍 | 文本更新 |

## 8. 实现映射与验收

- `Palette` 扩展：`accent`, `warning`, `error`, `info`, `syntax{keyword,type,string,comment,number}`, `selection`, `cursor` 等语义槽；深浅两套。
- `ThemeDefinition`：按钮/Tabs/面板按方向变量表重写；`reducedMotion` 全局映射 §7 末列。
- 状态栏与读数组件统一“等宽右对齐读数”原语（A/C 尤其）。
- 验收：选定方向后，欢迎页/工作台/问题面板/设置/Quick Start 五屏以真实视图树截图对照本稿 SVG 评审；对比度、触控高度、键盘焦点环逐项过（A08 清单）。

## 9. 建议（供 review，不是决定）

推荐 **C 为基底**（与 mcpp 社区母语、像素标识、Dev-C++ 对标定位同构，“近黑 + 磷绿 + 等宽”在编辑器品类里有辨识度而非 AI 通用味），吸收 **B 的浅色纸感**作为浅主题基调（避免 C 浅色变成冷灰医院感），并采用 **A 的等宽读数**规范所有度量类信息。若选混排，请指定主从。

## 10. 决策点

| ID | 决策 | 选项 |
| --- | --- | --- |
| V1 | 设计方向 | A / B / C / 混排（指定主从） |
| V2 | 标识配色 | 随 V1 / 单独指定 |
| V3 | 浅主题策略 | 首版仅深色（C 原教旨） / 深+浅同时交付 |
| V4 | UI 字体 | 全等宽（C） / UI 无衬线+读数等宽（A/B） |
