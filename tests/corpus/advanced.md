# Ideas, beautifully expressed

[TOC]

## Mathematics / 数学公式

A small idea $E=mc^2$ can carry a large meaning. 数学与文字自然相伴。[^note]

$$
\frac{-b \pm \sqrt{b^2-4ac}}{2a}
$$

## A clear process / 清晰的流程

```mermaid
flowchart LR
    A[写下想法] --> B{准备好?}
    B -->|是| C[继续写作]
    B -->|否| A
```

## A conversation / 交互时序

```mermaid
sequenceDiagram
    participant A as Author / 作者
    participant B as Typist
    autonumber
    A->>B: Write Markdown
    B-->>A: Render the document
    Note over A,B: Keep the original source
```

[^note]: Markdown 源码始终保留。公式和图表可双击继续编辑。
    Word 使用原生脚注，HTML 可点击引用跳转。
