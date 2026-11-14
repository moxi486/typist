# HTML 导入与网页粘贴

选择 **文件 → 导入 HTML…**，打开 `.html` 或 `.htm` 文件，检查转换后的预览与说明，再点击“导入”。结果是新的未保存 Markdown 文档；原 HTML 文件不会被覆盖或加入最近 Markdown 文件列表。导入前若有未保存内容，沿用保存、放弃、取消保护；取消预览或文件解码失败不会替换当前内容。命令行传入 HTML 路径也会直接导入为新文档。

从网页复制后，在渲染视图直接按 `Ctrl+V`，剪贴板中的 HTML 会转换为 Markdown 后插入，一次撤销即可恢复。HTML 无法转换时，使用剪贴板中的纯文本。源码视图、代码块、段落 Markdown 草稿和表格单元格使用纯文本。表格的 TSV 粘贴和图片粘贴仍优先处理。

要在源码视图插入 HTML 转换后的 Markdown 标记，请用 **编辑 → 将 HTML 粘贴为 Markdown**（`Ctrl+Alt+V`）。「粘贴 Markdown」仍使用剪贴板中的纯文本。 [Typora 复制与粘贴说明](https://support.typora.io/Copy-and-Paste/)

支持 Qt 原生富文本 HTML 子集中的标题、正文、粗斜体、删除线、行内代码、代码段、常见列表、引用、表格、分隔线、图片与链接。标题的 HTML ID 会转换为 Markdown 标题链接。代码段暂不保留语言标签；有序列表按当前编辑器模型重新编号。表格第一行成为 Markdown 表头，合并单元格展开后只在起始单元格保留内容。字体、颜色、布局、CSS、脚本和网页交互不属于 Markdown 内容，导入预览会提示检查简化结果。

HTML 文件中的相对图片和文件链接按原文件目录解析为绝对文件 URL，保存 Markdown 后仍引用原资源，**不会自动复制原网页资源目录**。绝对本地图片可显示；远程图片保留 Markdown 链接而不下载。剪贴板没有可信原文件目录，因此相对图片保留替代文字，相对链接保留文字；绝对本地文件、HTTP(S) 与邮件链接可保留。内嵌 `data:` 图片转换为替代文字。不存在的图片或编辑器不支持的资源可能只显示替代文字，请检查预览。

HTML 与转换后 Markdown 均限制为 8 MiB。文件按 BOM / HTML `meta charset` 解码，默认 UTF-8；无法解码时报告错误。解析和转换使用 Qt，不执行脚本、不联网取资源。复杂嵌套引用、列表项中的多块内容、嵌套表格和 MathML/SVG 等嵌入格式可能被简化，不保证整页网页的视觉复刻。Word、PDF 等其他格式导入仍未实现。

安装包中的 `share/typist/examples/import-writing.html` 可用于试用。`html_import` 检查格式、编码、资源、代码表格相邻语料与保存往返；窗口回归覆盖预览、双语主题、取消保护、保存和网页粘贴撤销。开发者可用 `typist import-writing.html --bench-import-html --bench-docx --bench-epub` 验证部署后的导入与导出。
