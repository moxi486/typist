# 公式、图表、脚注和目录

在「段落」菜单插入数学公式块、Mermaid 图表或目录；在「格式」菜单插入行内公式或脚注。数学公式块快捷键为 `Ctrl+Shift+M`。

行内公式写作 `$E=mc^2$`。输入闭合的 `$` 后自动渲染；独立公式使用单独成行的 `$$` 包围。公式支持分数、根式、上下标、求和、积分、矩阵等 LaTeX 数学排版。双击已渲染公式可打开源码与预览，修改后可撤销。未能渲染的公式保留原文，可切到源码模式修改。

Mermaid 使用带 `mermaid` 语言标记的代码围栏。目前支持 `flowchart / graph` 的 TD、TB、BT、LR、RL 方向，矩形、圆角、圆形、菱形节点，普通与虚线连接、边标签、分支和环。subgraph 和自定义样式等保留为源码，避免显示不完整图形。

```mermaid
flowchart LR
    A[写下想法] --> B{准备好?}
    B -->|是| C[继续写作]
    B -->|否| A
```

`sequenceDiagram` 支持 `participant` 参与者、`actor` 人物符号、显示别名、实线和虚线消息、自调用、`autonumber` 以及 `Note over` 注释。可用 `alt / else` 表示条件分支、`opt` 表示可选操作、`loop` 表示循环、`par / and` 表示并行操作、`critical / option` 表示关键操作与异常分支、`break` 表示中断片段。各片段以 `end` 结束，可以嵌套，编号会延续到各分支内的消息。

`autonumber` 显示消息编号；默认从 1 开始，步长为 1。`autonumber 起始值` 重设起始值并使用步长 1，`autonumber 起始值 步长` 同时设置两者。数字可为非负整数或最多两位小数，也可省略小数点前的 0；原生实现允许每个输入值最多 9 位整数，支持 0 起始和 0 步长，并按百分之一精确累加。编号省略无意义的尾零。负数、科学记数、超过精度或范围的值保留为可编辑源码。 [Mermaid 编号语法](https://mermaid.js.org/syntax/sequenceDiagram.html#start-and-increment-values-v11150)

`autonumber off` 隐藏后续消息的编号，单独写 `autonumber` 按当前计数和步长重新显示。每条消息（含隐藏编号的消息和自调用）都会推进计数，注释、分组、激活与控制指令不占编号。编号指令不能写在参与者分组中，也不能插在创建 / 销毁指令与其关联消息之间。

```mermaid
sequenceDiagram
    actor User as 作者
    participant Editor as 编辑器
    autonumber 1.25 .25
    User->>+Editor: 保存文档
    Note over User,Editor: 注释不占编号
    autonumber off
    Editor->>Editor: 内部校验
    autonumber
    Editor-->>-User: 已保存
    autonumber 10 5
    User->>Editor: 再次保存
    Editor-->>User: 保存完成
```

参与者可以用 `@{"type":"类型"}` 指定符号：`boundary`（边界）、`control`（控制）、`entity`（实体）、`database`（数据库）、`collections`（集合）、`queue`（队列）；`actor` 和 `participant` 分别选择人物和矩形。`participant`、`actor` 和 `create participant / actor` 均可使用配置，`type` 优先于声明关键字。六类新符号和人物的名称显示在图形下方，创建消息按符号宽度连接，生命线从名称下方开始。 [Mermaid 参与者类型](https://mermaid.js.org/syntax/sequenceDiagram.html#boundary)

配置中的 `"alias"` 设置显示名称，同时填写外部 `as 名称` 时以外部名称为准；省略 `type` 则沿用声明关键字的默认符号。别名支持中文、JSON 转义引号、花括号和 `<br>` 换行。原生配置使用单行标准 JSON 对象，只接受字符串类型的 `type` 和 `alias`；未知类型、其他配置字段、YAML 写法、多行或无效 JSON 保留为可编辑源码。

```mermaid
sequenceDiagram
    participant UI@{"type":"boundary","alias":"写作界面"}
    participant Service@{"type":"control"} as 文档控制
    participant Doc@{"type":"entity"} as 文档
    participant Disk@{"type":"database"} as 磁盘
    participant Entries@{"type":"collections"} as 文档集合
    participant Tasks@{"type":"queue"} as 保存队列
    UI->>+Service: 保存文档
    Service->>Doc: 校验内容
    Service->>Disk: 写入正文
    Service-)Tasks: 异步索引
    Tasks->>Entries: 更新集合
    Service-->>-UI: 已保存
```

标签可用 `<br>` 换行，排版会为消息和注释留出空间。长标签最多显示四行，完整内容保留在源码。支持最多 8 个参与者、60 条消息或注释、24 个控制片段与 6 层嵌套。其他控制指令与配置保留为可编辑源码。 [Mermaid 时序图语法](https://mermaid.js.org/syntax/sequenceDiagram.html)

```mermaid
sequenceDiagram
    actor User as 作者
    participant Editor as 编辑器
    participant Disk as 磁盘
    autonumber
    User->>Editor: 保存文档
    alt 内容有修改
        loop 逐项校验
            Editor->>Editor: 核对格式
        end
        par 保存正文
            Editor->>Disk: 写入 Markdown
        and 保留草稿
            Editor->>Disk: 更新恢复文件
        end
    else 内容相同
        Editor-->>User: 已是最新版本
    end
    opt 显示结果
        Note over User,Editor: 保存完成
    end
```

`box 名称 ... end` 把参与者组织为纵向分组。颜色可放在名称之前，支持 SVG 命名色（如 `Aqua`、`Purple`）、`rgb(...)` 和 `rgba(...)`；省略颜色时为透明背景。若名称本身是颜色词，可写 `box transparent Aqua`。分组标题自动换行并留出顶部空间，最多显示四行，余下内容以省略号提示，完整名称保留在源码。 [Mermaid 参与者分组](https://mermaid.js.org/syntax/sequenceDiagram.html#grouping--box)

分组块内填写 `participant` / `actor` 声明，消息写在组块结束之后。每位参与者只能属于一个组，成员列必须连续；最多支持 8 个非嵌套分组和总共 8 位参与者。空组、嵌套组、组内消息、无效 RGB/RGBA 颜色及 HSL / 十六进制颜色保留为可编辑源码。

```mermaid
sequenceDiagram
    box rgba(80,160,240,.12) 写作端
        actor User as 作者
        participant Editor as 编辑器
    end
    box transparent 存储服务
        participant Disk as 磁盘
    end
    User->>+Editor: 保存文档
    Editor->>+Disk: 写入正文
    Disk-->>-Editor: 写入完成
    Editor-->>-User: 已保存
```

`critical` 图示关键操作，可以省略 `option`，也可以用多个 `option 条件` 描述异常处理。`break 条件` 图示指定条件下中断当前交互。它们可与条件、循环和并行片段嵌套；`option` 必须直接属于当前 `critical`，不能跨越尚未结束的内层片段。缺少 `end` 或分支归属错误时保留为可编辑源码。 [Mermaid 关键操作](https://mermaid.js.org/syntax/sequenceDiagram.html#critical-region)、[中断片段](https://mermaid.js.org/syntax/sequenceDiagram.html#break)

片段标题按可用宽度自动换行，也可用 `<br>` 手动换行，最多显示四行；超出的内容以省略号提示，完整条件保留在源码与保存文件中。

```mermaid
sequenceDiagram
    actor User as 作者
    participant Editor as 编辑器
    participant Disk as 磁盘
    autonumber
    User->>+Editor: 保存文档
    critical 完整写入数据
        Editor->>Disk: 保存正文
    option 存储空间不足
        break 停止本次保存
            Editor-->>User: 显示错误
        end
    option 访问被拒绝
        Editor->>Editor: 保留草稿
    end
    Editor-->>-User: 处理完成
```

`rect rgb(R,G,B)` 或 `rect rgba(R,G,B,A)` 为一段交互添加背景高亮，以 `end` 结束，可嵌套并包含其他控制片段。RGB 通道取 0–255 的整数，RGBA 的透明度取 0–1；RGB 使用不透明色，RGBA 从外到内与底层颜色合成。高亮绘制在生命线、消息和注释下方，并与其他片段共用 24 个片段、6 层嵌套的上限。命名色、十六进制色、无效颜色和分支错配保留为可编辑源码。 [Mermaid 背景高亮](https://mermaid.js.org/syntax/sequenceDiagram.html#background-highlighting)

```mermaid
sequenceDiagram
    actor User as 作者
    participant Editor as 编辑器
    participant Disk as 磁盘
    autonumber
    rect rgba(80,160,240,.18)
        Note left of User: 核对内容
        User->>+Editor: 保存文档
        rect rgba(180,100,220,.2)
            Editor->>+Disk: 写入正文
            Disk-->>-Editor: 写入完成
        end
        Editor-->>-User: 保存完成
    end
```

时序图支持以下标准消息线型；实线与虚线都支持自调用及 `+ / -` 激活简写。双向消息在两端绘制箭头，自调用时分别沿出发和返回方向绘制。 [Mermaid 消息语法](https://mermaid.js.org/syntax/sequenceDiagram.html#messages)

| 实线 | 虚线 | 显示 |
| --- | --- | --- |
| `->` | `-->` | 无箭头线段 |
| `->>` | `-->>` | 实心箭头 |
| `<<->>` | `<<-->>` | 两端实心箭头 |
| `-x` | `--x` | 末端叉号 |
| `-)` | `--)` | 异步开放箭头 |

```mermaid
sequenceDiagram
    actor User as 作者
    participant Editor as 编辑器
    participant Worker as 工作进程
    User->Editor: 无箭头消息
    Editor-->User: 虚线消息
    User->>+Editor: 保存文档
    Editor-)+Worker: 异步写入
    Worker--)-Editor: 写入完成
    Editor<<->>Editor: 双向自调用
    Editor<<-->>User: 核对结果
    Editor-->>-User: 已保存
```

半箭头和 `()` 中心连接等其他箭头语法保留为可编辑源码。

`Note left of 名称` 和 `Note right of 名称` 把注释放在单个参与者左侧或右侧；`Note over 名称` 沿参与者生命线居中，`Note over A,B` 跨越两位参与者，交换顺序不影响位置。关键字不区分大小写，内容可用 `<br>` 换行。旁注会为激活条、相邻生命线和画布边缘留出空间。侧向注释仅支持一个参与者，多个参与者请用 `over`；无效位置或销毁后的引用保留为可编辑源码。 [Mermaid 注释语法](https://mermaid.js.org/syntax/sequenceDiagram.html#notes)

```mermaid
sequenceDiagram
    actor User as 作者
    participant Editor as 编辑器
    Note left of User: 先整理思路<br>再开始写作
    User->>+Editor: 保存文档
    loop 逐项核对
        Note left of Editor: 保留 Markdown 原文
        Note right of Editor: 检查格式与引用
    end
    Note over Editor,User: 内容完整
    Editor-->>-User: 已保存
```

时序图可用 `activate 名称` 和 `deactivate 名称` 显示参与者的执行区间。消息箭头后的 `+` 激活接收方，`-` 停用发送方。激活同一参与者时会叠加激活条，停用时按后进先出的顺序收回；未结束的激活条会延伸到图底部。支持最多 64 段激活、每位参与者最多 8 层。停用未激活参与者的图表会保留为可编辑源码。 [Mermaid 激活条语法](https://mermaid.js.org/syntax/sequenceDiagram.html#activations)

```mermaid
sequenceDiagram
    actor User as 作者
    participant Editor as 编辑器
    participant Disk as 磁盘
    autonumber
    User->>+Editor: 保存文档
    loop 核对内容
        Editor->>+Editor: 递归检查
        Editor-->>-Editor: 检查完成
    end
    Editor->>+Disk: 写入正文
    Disk-->>-Editor: 写入完成
    Editor-->>-User: 保存完成
```

用 `create participant 名称` 或 `create actor 名称 as 别名` 创建参与者，其后的消息必须发给新参与者。创建消息连接新出现的标题，生命线从标题下方开始。用 `destroy 名称` 标记下一条发送或接收消息为销毁消息，在消息处绘制叉号并结束生命线，同时收回仍未关闭的激活条。`-x` 和 `--x` 分别表示实线和虚线叉号消息；单独使用叉号消息不会销毁参与者。 [Mermaid 创建与销毁语法](https://mermaid.js.org/syntax/sequenceDiagram.html#actor-creation-and-destruction-v1030)

每个标识符只支持一个生命周期。创建前可声明显示顺序，但不能已参与消息、注释或激活。创建或销毁指令与关联消息之间仅允许空行和注释；重复创建、未关联消息、销毁后继续引用等情况会保留为可编辑源码。

```mermaid
sequenceDiagram
    participant Editor as 编辑器
    actor User as 作者
    User->>Editor: 保存文档
    create participant Task as 保存任务
    Editor->>+Task: 创建任务
    Task->>Task: 写入 Markdown
    create actor Reviewer as 审核员
    Task->>Reviewer: 请求复核
    Reviewer-->>Task: 已通过
    destroy Task
    Task-->>-Editor: 保存完成
    destroy Reviewer
    Editor--xReviewer: 结束审核
    Editor-->>User: 已保存
```

`pie` 绘制饼图。用双引号包围名称，每行写一个数值。扇区按源码顺序顺时针排列，图例可通过 `showData` 显示数值。标题可写在 `pie` 同一行或单独一行。支持整数和小数，最多 64 项，并要求总值大于零。零值留在图例中。 [Mermaid 饼图语法](https://mermaid.js.org/syntax/pie.html)

```mermaid
pie showData title 写作时间（分钟）
    "起草" : 45
    "核对" : 22.5
    "修订" : 22.5
```

`timeline` 按源码顺序绘制时间线。用 `title` 添加标题，用 `section` 分组，事件可用冒号和空格接在时期后，或在续行以冒号和空格开头。时期和事件支持文字、日期以及 `<br>` 换行。每行最多放四个时期，时间线最多支持 24 个时期和 64 个事件。 [Mermaid 时间线语法](https://mermaid.js.org/syntax/timeline)

```mermaid
timeline
    title 文档发布计划
    section 准备
        2026-11-17 : 收集材料 : 完成初稿
        2026-11-18 : 核对术语
                   : 审阅示例
    section 发布
        2026-11-19 : 发布新版 <br> 下载包
```

`mindmap` 按缩进组织节点。首个节点是根，每行接到最近一个缩进更少的节点。一级分支分布在根节点两侧，同一分支及其后代使用相同的配色。支持普通文字、方框 `id[名称]`、圆角框 `id(名称)`、圆形 `id((名称))`、六边形 `id{{名称}}`、云形 `id)名称(` 和星芒 `id))名称((`。

标签支持中文自动换行与 `<br>`，每个节点最多显示四行，完整文字保留在源码中。可在节点后另起一行用 `::icon(fa fa-book)`、`fa-lightbulb` 或 `fa-check` 加入内置的书本、灯泡和对勾图标。单图最多 48 个节点、8 层。其他图标、Markdown 富文本标签、自定义样式和配置保留为可编辑源码。 [Mermaid 思维导图语法](https://mermaid.js.org/syntax/mindmap.html)

```mermaid
mindmap
  root((写作计划))
    research[资料整理]
    ::icon(fa fa-book)
      阅读参考文档
      汇总术语<br>与例子
    drafting(撰写初稿)
      分节写作
      调整结构
    review{{复核}}
      校对和发布
```

`quadrantChart` 绘制四象限图。可用 `title` 添加标题、用 `x-axis` 和 `y-axis` 写轴的两端文字，也可只写低值端。象限编号从右上方起逆时针排列。用 `名称: [x, y]` 添加散点，两坐标均在 0 到 1 之间，最多 32 个点。省略散点时可用作空白矩阵。标注密集时，图内放不下的点名和坐标会列在图下。过长的标签会截短显示，源码保留完整内容。点样式、样式类及图表配置会保留为可编辑源码。 [Mermaid 四象限图语法](https://mermaid.js.org/syntax/quadrantChart.html)

```mermaid
quadrantChart
    title 写作改进优先级
    x-axis 低投入 --> 高投入
    y-axis 低收益 --> 高收益
    quadrant-1 规划后实施
    quadrant-2 优先实施
    quadrant-3 维持现状
    quadrant-4 暂缓
    模板整理: [0.23, 0.76]
    移动端适配: [0.72, 0.68]
    快捷键微调: [0.18, 0.27]
    主题重绘: [0.74, 0.32]
```

`sankey` 和 `sankey-beta` 绘制桑基图。每条记录按来源、去向、数值填写三列 CSV。名称中包含逗号或换行时用双引号包围，字段内的双引号写作两个连续引号。空行及 `%%` 注释行可以用于分组。

图从左向右展示分流和汇流。流带宽度与数值成比例，节点旁显示流入或流出两者中的较大总量。同一对节点的多条记录会合并为一条流带，零值记录不生成流带。支持最多 64 个节点和 128 条记录，至少一条记录需要正值。循环流向和自定义配置保留为可编辑源码。 [Mermaid 桑基图语法](https://mermaid.js.org/syntax/sankey.html)

```mermaid
sankey
    %% 来源,去向,工作量
    写作计划,整理素材,12
    写作计划,直接起草,8
    整理素材,撰写正文,9
    整理素材,参考归档,3
    直接起草,撰写正文,8
    撰写正文,"复核、校对",17
```

`gitGraph` 绘制 Git 分支和提交历史。初始分支为 `main`。用 `commit` 添加提交、`branch` 创建并切换分支、`checkout` 或 `switch` 切换已有分支，再用 `merge` 合并另一分支的最新提交。用 `id: "名称"` 添加提交名称、用 `tag: "版本"` 添加标签。未指定 ID 的提交按顺序显示为 `c1`、`c2`。

用 `type: HIGHLIGHT` 突出显示提交，`type: REVERSE` 表示撤销，合并提交带双圈。分支默认按创建顺序排布，可用 `branch 名称 order: 正整数` 排序；带顺序值的分支排在其余分支之后，`main` 始终在首位。可选择 `gitGraph LR:`、`gitGraph TB:` 或 `gitGraph BT:` 布局。支持最多 8 个分支与 32 个提交。过长的名称会截短显示，源码保留完整内容。`cherry-pick` 与自定义配置保留为可编辑源码。 [Mermaid GitGraph 语法](https://mermaid.js.org/syntax/gitgraph.html)

```mermaid
gitGraph
    commit id: "起点"
    branch writing
    commit id: "完成初稿"
    checkout main
    commit id: "修复问题" type: REVERSE
    checkout writing
    commit id: "复核" type: HIGHLIGHT
    checkout main
    merge writing id: "合并写作" tag: "v1.0"
    commit id: "发布"
```

`xychart` 和 `xychart-beta` 支持柱形图 `bar`、折线图 `line` 和组合图。在图表声明后加 `horizontal` 可以横向绘制。两条轴都可命名，X 轴可指定类别或数值范围，Y 轴可指定范围或由数据推算。柱形图自动推算时包含零点。带名称的系列显示在图例中。

类别与数据按位置对应。图表最多支持 8 个系列，每个系列最多 32 个值。标签较密时会减少刻度标签，导出仍保留全部已绘制数据。 [Mermaid XY 图语法](https://mermaid.js.org/syntax/xyChart)

```mermaid
xychart
    title "每月产量（件）"
    x-axis "月份" [一月, 二月, 三月, 四月]
    y-axis "数量" 0 --> 100
    bar "实际" [40, 65, 72, 83]
    line "目标" [50, 60, 75, 80]
```

`stateDiagram` 和 `stateDiagram-v2` 绘制基础状态图。用 `[*]` 表示开始或结束，用 `-->` 连接状态。冒号后的文字可说明状态或转换，`state "显示名称" as ID` 可为状态命名。支持方向设置、双向转换、自转换与 `<<choice>>` 分支。复合状态、并发、注释框和自定义样式保留为可编辑源码。 [Mermaid 状态图语法](https://mermaid.js.org/syntax/stateDiagram.html)

```mermaid
stateDiagram-v2
    state "等待编辑" as Idle
    state "编辑中" as Active
    [*] --> Idle
    Idle --> Active : 打开文档
    Active --> Active : 自动保存
    Active --> Idle : 关闭文档
    Active --> [*] : 退出应用
```

`classDiagram` 支持类名、自定义显示名称、类注解、属性和方法。成员可写在类的花括号内，也可写作 `Class : 成员`。保留可见性符号，静态成员加下划线，抽象方法用斜体。支持继承、实现、组合、聚合、关联与依赖，关系可附文字说明和双端多重性。用 `direction` 设置上下或左右布局。单图最多 24 个类、48 条关系，每类最多 18 个成员。命名空间、泛型、笔记、交互和样式指令保留为可编辑源码。 [Mermaid 类图语法](https://mermaid.js.org/syntax/classDiagram.html)

```mermaid
classDiagram
    class Document["文档"] {
        -String title
        +save() bool
    }
    class MarkdownDocument["Markdown 文档"] {
        +toMarkdown() String
    }
    class Section["章节"]
    Document <|-- MarkdownDocument
    Document "1" *-- "1..*" Section : 包含
```

`gantt` 绘制按日期排列的项目计划。用 `title` 添加标题、`section` 分组。日期采用 `YYYY-MM-DD`，工期支持分钟 `m`、小时 `h`、天 `d` 和周 `w`，精确到分钟。可直接写开始和结束日期，也可指定任务 ID，通过 `after` 接续一个或多个任务；省略开始时间时接续上一项，首项须提供日期或依赖。使用 `done`、`active`、`crit` 和 `milestone` 标记状态。

`excludes` 可指定 `weekends` 或单个日期。排除日在图中有底色。以工期指定的任务遇到排除日时会顺延。任务尾部遇到排除日时，图条在休假前结束，后续任务从休假后接续。明确指定结束日期的任务保留原日期。单图最多 24 项任务和约十年跨度。自定义日期格式、`until`、自定义坐标轴或样式会保留为可编辑源码。 [Mermaid 甘特图语法](https://mermaid.js.org/syntax/gantt.html)

```mermaid
gantt
    title 文档发布计划
    dateFormat YYYY-MM-DD
    excludes weekends, 2026-11-25
    section 准备
        整理资料 :done, draft, 2026-11-19, 2d
        技术复核 :active, review, after draft, 2d
    section 发布
        校对样例 :crit, check, after review, 1d
        发布新版 :milestone, after check, 0d
```

脚注使用 `[^note]` 引用和 `[^note]: 注释内容` 定义；后续行缩进四个空格。点击引用跳转到定义，正文与定义均保留在 Markdown 中，未引用的定义也会保留。定义区当前直接展示可编辑的原始内容。Word 导出使用原生脚注；HTML 输出带跳转的脚注区；PDF 将定义放在文末。

单独一段的 `[TOC]` 或 `[toc]` 会生成正文目录，也可输入后按 Enter 渲染。目录根据当前标题自动更新，点击条目跳到对应标题。保存时仍是 `[TOC]`，生成文字不会写回 Markdown。PDF 将目录展开为可跨页的段落；Word 输出包含缓存条目与内部链接的 TOC 域，可在 Word 中更新目录。

HTML / PDF / DOCX 中的公式和受支持图表内嵌为双倍分辨率图片。Word 中的公式图片不能作为原生公式对象编辑；继续修改原始 Markdown 可重新导出。HTML 无需外部脚本或网络。引用、嵌套结构中无法原生排版的块保留 Markdown 源码；代码围栏内、行内代码、转义美元符号与常见货币文本不会被改成公式。
