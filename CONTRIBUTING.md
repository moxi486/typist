# 贡献指南

## 分支与 PR

main 为唯一长期分支，功能在 short-lived 分支开发，PR 合入 main。PR 需说明改动动机与影响面，并关联 issue。

## 硬性规则

任何改动 Markdown 序列化器的 PR 必须附带往返测试用例：把最小复现语料放入 tests/corpus/，使 parse -> serialize -> parse 得到相同 AST。序列化尚未覆盖的语法（表格、任务列表）放入 tests/corpus/gfm/，只做解析层断言，并在 PR 描述中写明缺口。

## 代码规范

- C++17；入库的 .clang-format 是唯一格式标准，CI 用 clang-format --dry-run --Werror 拦截。
- 命名：类 PascalCase，方法与函数 camelCase，成员变量加 m_ 前缀。
- src/core 不得包含任何 Qt Widgets 头文件，必须能脱离 UI 独立编译与测试。
- 每个新功能带测试；无测试的 PR 不予合入。

## 环境

Linux 依赖：qt6-base-dev、libcmark-gfm-dev、libcmark-gfm-extensions-dev。macOS：brew install cmark-gfm qt@6。受限环境下不强装依赖：Qt 缺失时 core 与 core 测试仍可独立构建运行。
