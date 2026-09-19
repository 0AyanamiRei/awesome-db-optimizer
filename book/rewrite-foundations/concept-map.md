# Foundations 概念地图

阅读对象是 [Building Query Compilers 的 Part II](../Query%20opt.pdf#page=218)。下图先表示主题组织，再表示第 7 章内部依赖；它不是查询执行流程，也不是以某一种改写算法为终点的路线。

## 第二部分的主题结构

```mermaid
flowchart LR
    ROOT["Part II · Foundations"]
    ROOT --> C5["第 5 章 · 逻辑、NULL、布尔表达式"]
    ROOT --> C6["第 6 章 · 函数依赖"]
    ROOT --> C7["第 7 章 · Set / Bag / Sequence 代数"]
    ROOT --> C8["第 8 章 · 声明式查询表示"]
    ROOT --> C9["第 9 章 · Translation and Lifting"]
    ROOT --> C10["第 10 章 · 等价、包含、最小化"]
    C5 -. "谓词与相等语义" .-> C7
    C6 -. "分组及唯一性条件" .-> C7
    C7 -. "代数表示" .-> C9
    C8 -. "其他查询表示" .-> C9
    C8 -. "CQ / 映射语言" .-> C10
    C7 -. "结果语义" .-> C10
```

第 8 章以提要与文献为主，第 9 章主要是标题；图中保留它们的位置，不意味着原书已经把这些主题完整讲完。第 10 章中的“查询等价”还包括判定问题，与第 7 章具体代数恒等式的使用层次不同。

## 第 7 章的内部结构

```mermaid
flowchart TD
    DATA["7.1 · Set / Bag / Sequence<br/>重数、顺序、显式去重"]
    AGG["7.2 · 聚合函数<br/>分解、可逆性、重复敏感性"]
    TYPE["7.3.1–2 · 类型与签名<br/>Tuple、A、F、绑定"]
    OPS["7.3.3–12 · 算子家族<br/>投影、选择、Map、分组、Nest/Unnest、Join、Groupjoin、D-Join"]
    LIN["7.4 · 强弱线性<br/>从单元素推广到一般输入"]
    REP["7.5 · 数据表示与转换<br/>显式重复、计数、TID、位置"]
    EQ["7.6–8 · 等价与重排<br/>类型和属性条件、谓词拆离"]
    DJ["7.9 · D-Join 基本等价"]
    OJ["7.10 · Outerjoin<br/>补 NULL、严格谓词、适用条件"]
    GROUP["7.11 · Grouping 等价<br/>聚合分解、重数补偿、FD"]
    REDUCE["7.12–14 · 冗余连接与 Reducer<br/>7.14 仅标题"]
    SEARCH["7.15 · 核心搜索空间<br/>合法性、冲突表示、完备性边界"]
    SEQ["7.16 · 有序代数<br/>保序算子、可用与失效的等价式"]
    DATA --> AGG
    DATA --> TYPE
    TYPE --> OPS
    AGG --> OPS
    OPS --> LIN
    OPS --> REP
    DATA --> REP
    LIN --> EQ
    TYPE --> EQ
    EQ --> DJ
    EQ --> OJ
    AGG --> GROUP
    EQ --> GROUP
    REP --> GROUP
    OJ --> GROUP
    EQ --> REDUCE
    EQ --> SEARCH
    OJ --> SEARCH
    DATA --> SEQ
    OPS --> SEQ
```

图外但持续使用的前提是：第 5 章为比较、过滤、分组及 outer join 提供 NULL 语义，第 6 章为分组键、唯一性和部分消除规则提供 FD 条件。§7.17–18 的文献与 ToDo 用于识别来源和未完成部分，不另设一条概念主线。

## 阅读顺序与依赖的区别

正文主要按原书顺序展开。§7.1 会先出现线性的初步定义，§7.4 再系统讨论；§7.2 先定义聚合函数，§7.3 定义分组算子，§7.11 才讨论复杂分组变换。教材保留这种递进，并通过回链复用前面知识。

Sequence 分两次学习：§7.1.4 建立序列对象与相等，§7.16 学习保序算子。因此不能把“先讲 bag”理解成“sequence 不在范围内”。同样，free attributes 与 binding 服务于全部含表达式参数的算子，并不只为 dependent join 准备。

对应的逐节安排见[教学提纲](outline.md)，原文全目录的覆盖情况见[覆盖表](coverage.md)。
