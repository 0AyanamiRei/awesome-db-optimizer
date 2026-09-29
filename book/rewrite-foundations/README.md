# Building Query Compilers · Part II Foundations 中文精读

本教材的目标是连续、系统地阅读 Guido Moerkotte 的 **Building Query Compilers 第二部分 Foundations**，建立理解其中定义、性质和推导所需的基础语言。范围是本地 [Query opt.pdf](../Query%20opt.pdf#page=218) 的第 5–10 章，印刷页 197–326（PDF 页 218–347），不是第 2 章 Textbook Query Optimization。

先读[原文内容梳理](part-ii-overview.md)：它说明第二部分各章实际写了什么、篇幅如何分布、哪些位置只有提要。随后可查看[概念地图](concept-map.md)和[教学提纲](outline.md)。

## 阅读主线

按原书的知识结构，从逻辑与 NULL、函数依赖进入集合、bag、sequence 和聚合函数，再学习多态代数的类型、算子、线性及数据表示。之后研究等价变换的条件、outer join、grouping、搜索空间，以及有序代数；最后认识声明式查询表示、表示间翻译、查询包含与最小化。

Dependent join 是这套代数中的一个成员。它与其他算子同样需要定义和解释；Neumann 的论文只作为可选对照，不承担全书骨架，也不决定必读范围。已熟悉的 join ordering 算法不必重复学习，但原书 §7.15 讨论的合法搜索空间及正确性、完备性仍在阅读范围内。

## 当前成果

版本：**0.9，2026-09-22**。已逐页通读本地 PDF 的 Part II，覆盖正文、公式、例子、表格及占位内容；对部分符号和疑似问题另行查看页面图像。这个状态表示已经完成内容梳理，不表示独立验证了每一条公式、算法和所引文献。第 5 章的 L01–L06 与补充课 L06b、第 6 章的 L07–L09、以及第 7 章 §7.1 的 L10–L15 已完成。

第 5 章按下面的顺序阅读：


| 顺序  | 教学内容                                                              | 读完后应掌握                            |
| --- | ----------------------------------------------------------------- | --------------------------------- |
| L01 | [为什么二值逻辑的常识需要重新检查](lessons/01-two-valued-logic.md)                | 真值、逻辑等价和恒等式的适用范围                  |
| L02 | [NULL 改变了哪些操作](lessons/02-null-and-comparisons.md)                | 普通值相等、SQL 相等和点等号                  |
| L03 | [三个真值与两种解释上下文](lessons/03-three-valued-logic.md)                  | UNKNOWN、WHERE/CHECK 和接受行为         |
| L04 | [否定为什么会交换 UNKNOWN 的解释](lessons/04-negation-and-interpretation.md) | `⌊·⌋⊥`、`⌈·⌉⊥` 与 NOT 的关系           |
| L05 | [预处理一个布尔表达式](lessons/05-prepare-boolean-expressions.md)           | `pareval`、`pushnot`、`pushunk` 的次序 |
| L06 | [从相等谓词建立等价类](lessons/06-equivalence-classes-and-nullability.md)   | 合取出现、`=⁻` 等价类及 nullability 边界     |
| L06b | [量词、条件分配与第 5 章练习](lessons/06b-quantifiers-and-chapter-review.md) | 量词移动、空范围、蕴含/异或和三个练习 |



| 资料                              | 用途                             |
| ------------------------------- | ------------------------------ |
| [原文内容梳理](part-ii-overview.md)   | 面向读者的全景介绍，重点说明第 7 章各主题的关系      |
| [原文逐节覆盖表](coverage.md)          | 对照 PDF 书签列出全部章、节、小节及其教学去向，避免遗漏 |
| [概念地图](concept-map.md)          | 全局主题图与第 7 章内部依赖图               |
| [教学提纲](outline.md)              | 按主题拆分的小节、前置知识、原文位置和交付要求        |
| [符号表](notation.md)              | 以 Moerkotte 为主的记号，区分不同语义和同形符号  |
| [语义契约](semantics.md)            | 类型、NULL、重复、顺序、聚合及等价的约定         |
| [推导与证明安排](proof-obligations.md) | 从各主题选择代表性推导，不把证明方式局限为重数展开      |
| [边界与原文问题](boundaries.md)        | 未完成条目、适用范围及已经定位的疑似笔误           |
| [资料与阅读记录](sources.md)           | 固定版本、页码、通读记录与核验程度              |




## 如何逐节编写

每节围绕一个理解任务，写成有动机、有定义解释、有例子和必要推导的连续文章。以 5～10 分钟阅读为起点；首次完整证明允许更长。练习可选，读者反馈用于调整展开程度。

符号首先沿用本书；每节给出具体原文节号和页码。原书内容、教学补充、经过反例修正的表述分别标明。未完成的章会先提供原文导览和必要的最小背景，不把教材补写的内容说成作者已有正文。

公式排版以能够直接对照原文为准：使用 LaTeX 数学环境保留算子、上下标、括号和谓词位置，例如 `$R \bowtie_{A=C} S$`、`$\sigma_{A\ne B}(R)$`。SQL 代码另用代码块，不用 `UNION`、`AND`、`<>` 或方括号谓词代替原文代数记号。符号的中文解释放在公式旁边。已有章节仍有 Unicode 转写，尚未全书转换；L06 的连接推导已按此约定修订。

第 5 章已经完成。接下来进入[第 6 章：函数依赖](outline.md)。遇到 tuple、函数、集合等基础记号时就地解释；后续在第 7 章统一建立精确定义，不先用另一篇论文重排整本书。

第 6 章按下面的顺序阅读：

| 顺序 | 教学内容 | 读完后应掌握 |
| --- | --- | --- |
| L07 | [X → Y 究竟约束什么](lessons/07-functional-dependencies.md) | FD 的量化定义、传递链和键的直觉 |
| L08 | [Armstrong 公理、闭包与键](lessons/08-armstrong-closure-and-keys.md) | 公理、派生规则、属性闭包和最小键 |
| L09 | [FD、NULL、重复行与算子传播](lessons/09-fd-null-duplicates-and-propagation.md) | 点等号、值层 FD、重复边界和原文传播缺口 |

第 7 章 §7.1 是重点基础模块，按下面的顺序阅读：

| 顺序 | 教学内容 | 读完后应掌握 |
| --- | --- | --- |
| L10 | [用有限集合描述结果](lessons/10-sets-and-characteristic-functions.md) | schema、集合运算、特征函数和集合线性 |
| L11 | [Bag 保存了集合遗漏的信息](lessons/11-bags-and-multiplicity.md) | 重数、Bag 相等、成员、基数和 NULL 元素 |
| L12 | [Bag 运算怎样作用于重数](lessons/12-bag-operations.md) | 加法并、min 交、截断差和 `∪max` |
| L13 | [一条成立的分配律和一条反例](lessons/13-bag-laws-and-proof.md) | 逐点重数证明与反例构造 |
| L14 | [为什么要显式控制重复](lessons/14-explicit-duplicate-control.md) | set-faithfulness、`Πᴰ` 和 union distinct |
| L15 | [顺序带来了第三种相等](lessons/15-sequences-and-order.md) | Sequence、位置、连接和 sequence-linearity |

## 读者使用方式

每节先承接上一节已经建立的对象，只引入一个主要的新问题。正文中的公式都会先说明输入、输出和适用范围，再给出推导；表格用于检查有限真值或反例，不把有限例子冒充一般证明。

阅读时可以优先抓住每节结尾的“接口”：这一节新增了什么定义，下一节会把它用于什么地方。遇到原文缺口或疑似笔误，先看正文顶部的原文范围和[边界记录](boundaries.md)，不要把教学补充误读成作者已经写出的结论。
