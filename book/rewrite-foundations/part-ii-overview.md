# 原文第二部分究竟写了什么

这里的“第二部分”是 *Building Query Compilers* 的 **II Foundations，第 5–10 章**。本轮逐页阅读了本地 2026-06-23 版的印刷页 197–326，对应 PDF 页 218–347。下文概括作者实际写出的内容；不把目录标题当成已经完成的章节。完整目录对照见[逐节覆盖表](coverage.md)。

这一部分建立的是查询编译器的一套基础：值和谓词怎样解释，数据有哪些约束，集合、重复和顺序怎样表示，算子如何作用于这些对象，什么条件下可以变换表达式，以及查询之间的包含和等价怎样讨论。Dependent join 是其中一个话题；它不是这一部分的组织中心。

## 篇幅和完成情况

页码均为印刷页。章节范围包含章末空白页，不能把页数直接当作正文长度。

| 原文章节 | 页码 / PDF 页 | 实际情况 |
| --- | --- | --- |
| 第 5 章 Logic, Null, and Boolean Expressions | 199–206 / 220–227 | 有连续正文、真值表和例子；§5.6 Nullability Inference 只有标题 |
| 第 6 章 Functional Dependencies | 207–208 / 228–229 | 介绍 FD、公理与键；含 NULL 的定义后仍有占位文字，算子上的推导未展开 |
| 第 7 章 An Algebra for Sets, Bags, and Sequences | 209–314 / 230–335 | 第二部分的主体，正文至 p.313；覆盖算子、聚合、线性、表示、等价、搜索空间和有序代数 |
| 第 8 章 Declarative Query Representation | 315–316 / 336–337 | 主要内容集中在 p.315，多为短说明、标题和文献线索 |
| 第 9 章 Translation and Lifting | 317–318 / 338–339 | p.317 列出各转换方向，缺少实际推导 |
| 第 10 章 Query Equivalence, Containment, Minimization, and Factorization | 319–326 / 340–347 | 定义及集合语义下的 CQ 包含、最小化有正文；其余多为结果提要与文献 |

## 第 5 章：先决定谓词究竟是什么意思

本章从熟悉的二值逻辑开始，列出交换、结合、分配、否定和量词相关的规律。随后引入 NULL，说明函数和比较在含 NULL 的输入上必须重新定义。这里最重要的区分是普通 SQL 相等与把两个 NULL 视为相等的点等号：前者可能产生 UNKNOWN，后者服务于重复判定和分组等场景。见 [§5.1–5.3，pp.199–203](../Query%20opt.pdf#page=220)。

作者进一步区分 UNKNOWN 的两种解释：WHERE 中只有 TRUE 保留，CHECK 的接受条件则不同。真值不完全相同的两个谓词，放在某个特定解释上下文中可能表现相同。对否定尤其不能直接把 UNKNOWN 当成 FALSE 后再随意变换。

§5.4 讨论布尔表达式预处理，包括部分求值、先把否定下推的 `pushnot`，再处理 UNKNOWN 解释的 `pushunk`。§5.5 用“合取出现”的条件建立由相等谓词诱导的等价类，并讨论 NULL 对属性替换的影响。§5.6 只有 Nullability Inference 标题，§5.7 给出参考文献。见 [pp.204–206](../Query%20opt.pdf#page=225)。

**教学重点**应是比较语义、解释上下文和条件替换，而不只是背一张三值真值表。空着的 nullability 小节需要标明，不能声称已从本书读到了完整推导算法。

## 第 6 章：描述数据中的约束

作者先通过排序简化的例子说明 FD 对优化的价值，再定义 X→Y：两条记录在 X 上相同，就必须在 Y 上相同。接着给出 Armstrong 公理、派生规则、依赖闭包、超键和键。§6.2 使用点等号给出含 NULL 的 FD 定义，但随即留下 `XXX`；§6.3 的属性依赖传播仍是占位内容。见 [pp.207–208](../Query%20opt.pdf#page=228)。

这些约束后来会用于判断分组是否被拆细、某次连接是否重复放大一组数据、一个分组能否被消除。教学中要同时区分“tuple 值满足 FD”和“每个 tuple 出现几次”：FD 本身不能排除 bag 中完全相同的重复行。

## 第 7 章：基础对象、算子、性质与变换

本章开头明确把代数用于 SQL、OQL 和 XPath/XQuery，算子可以多态，并允许复杂表达式甚至嵌套代数表达式出现在参数中。因此，nested values、一般 map、sequence、nest/unnest 和 groupjoin 都属于作者的基础语言。不能先缩成一套只含平坦 SQL bag 的代数，再把其余内容视为可有可无。

### §7.1：集合、bag、sequence 与显式去重

§7.1.1 复习有限集合及其运算，用特征函数表达相等和包含，并初步引入线性。§7.1.2 改为非负整数重数，逐项说明集合规律为什么不一定适用于 bag，并通过推导和反例比较分配律。§7.1.3 提出显式重复控制和 set-faithfulness：尽量使用 bag 算子，在需要的位置显式去重。§7.1.4 定义有限序列、首尾、连接和顺序，并引入序列线性。见 [pp.209–216](../Query%20opt.pdf#page=230)。

这部分同时回答三个问题：元素是否存在、出现几次、处于什么位置。它们决定后面“相同结果”的含义。

### §7.2：聚合函数自身的性质

这里先研究 min、max、count、sum、avg，不先讨论 GROUP BY 算子的移动。内容包括 NULL 和 DISTINCT，聚合函数的签名、分解、可逆性，avg 所需的 sum/count 状态，聚合向量，以及重复敏感和不敏感的分类。见 [pp.216–220](../Query%20opt.pdf#page=237)。

“可分解”解决同一个聚合怎样从局部结果合并；“可拆分的聚合向量”则涉及哪些表达式分别依赖不同输入。它们是后面预聚合和 grouping 变换的前提，应在首次出现时讲清，不能到复杂证明中临时补充。

### §7.3：建立完整的算子家族

先给类型表达式、A(e)、F(e)、绑定约定和多态签名，再给语义定义。随后依次介绍：

- 保留重复和去重两类 projection，selection，一般 map 与扩展 map，以及 rename。
- Unary grouping、nest、两类 unnest、unnest-map 和 flatten。
- Product、inner/semi/anti/left/full outer join，带默认值的 outer join，以及 d-join。
- Groupjoin、特殊 min/max 算子，以及其他 dependent operators。

见 [pp.220–233](../Query%20opt.pdf#page=241)。其中 grouping 把输入分成组，groupjoin 为左侧的每个元素构造右侧匹配组；两者在重复、空组和输出粒度上需要分开理解。这里的 unnest 算子展开的是集合值，不应直接等同于整套 correlated-subquery decorrelation 算法。

### §7.4：用线性减少逐条证明的工作

作者区分强线性、弱线性、左右参数的线性，以及 duplicate faithfulness，给出算子分类和部分证明。随后说明如何从 singleton 情形推广到一般输入，并用属性的产生、删除和引用条件分析重排。见 [pp.233–239](../Query%20opt.pdf#page=254)。

这是一条独立的方法论主线。它既不等同于“所有证明都展开重数”，也不意味着“两个线性算子就一定可交换”。仍需检查单元素上的行为、类型和属性依赖。原文关于弱线性复合的表述有额外问题，见[边界记录](boundaries.md)。

### §7.5：语义对象可以有不同表示

作者比较显式重复行、带 multiplicity 列、带 TID 的表示；对 sequence 还讨论位置集合。随后给出表示间转换、set/bag/sequence 间转换，以及计数表示下的 product、join 和 projection。最后用 partial preaggregation 连接到聚合分解。见 [pp.239–243](../Query%20opt.pdf#page=260)。

带 TID 的表示本身可以是一个集合，但它代表的用户数据仍可能是 bag。只保存 multiplicity 可以表示重复，却不一定能还原 sequence 的顺序。这是语义与表示之间的区别，不能归为无关的实现细节。

### §7.6–7.8：等价的含义与基本变换模式

§7.6 给出对所有合法替换、类型正确实例成立的等价含义。§7.7 讨论幂等、一元算子重排、向二元算子左右分支移动、同时作用于两个分支、二元算子的交换结合，以及 l/r-asscom 和分配。§7.8 单独区分谓词的拆离/附着和 selection 的下推/上拉。见 [pp.243–255](../Query%20opt.pdf#page=264)。

这些表格是带条件的索引。一个 `+` 不能脱离消费者/生产者关系、NULL 条件和重复要求直接变成实现规则。教学应选少量代表性模式完整推导，再教读者如何查表。

### §7.9–7.10：两个需要专门条件的算子专题

§7.9 讨论 d-join 的绑定、结合形式、与 map/unnest/flatten 的联系、无依赖时退化为普通 join，以及依赖随变换移动的情况。它主要占 pp.255–257，而不是整个 Foundations 的中心。

§7.10 从不同 NULL 比较谓词的具体结果开始，用反例说明 outer join 的结合和重排为何脆弱，再推导 null-rejecting 条件、外连接简化、outer union 与 generalized outerjoin。某些推广只在集合语义成立，作者明确给出 bag 反例。见 [pp.257–266](../Query%20opt.pdf#page=278)。

### §7.11：分组变换是一个大型主题

这节从 pp.266–292 展开，包含八个小节，篇幅远超过 d-join 专节。它把前面的 FD、聚合分解、bag 重数、默认值和算子定义结合起来：

| 原文小节 | 实际问题 |
| --- | --- |
| 7.11.1 Elementary Fact | 怎样更换分组键，何时一组只有一条记录，何时 grouping 可由 map 代替 |
| 7.11.2 Join | 提前分组如何改变匹配倍数；count 如何补偿；eager/lazy 各种情形；何时能去掉最终 grouping |
| 7.11.3 Left Outerjoin | 怎样分别处理匹配数据和补 NULL 的数据，保持聚合结果 |
| 7.11.4 Left Outerjoin with Default | 默认值加入后，相应推导怎样调整 |
| 7.11.5 Full Outerjoin | 两边均需保留时的分解、默认值和重数组合 |
| 7.11.6 D-Join | 把 grouping 相关的讨论延伸到 dependent join |
| 7.11.7 Groupjoin | grouping、join/outerjoin 与 groupjoin 的相互表示、转换条件和示例 |
| 7.11.8 Intersection and Difference | 借助计数表示处理交与差中的分组 |

教学会保留这整个主题，先解释“分组粒度”和“记录被复制多少次”，再推导代表公式；其余公式按家族对照。这样可以保持系统性，又不把每个近似变体都写成孤立的一课。

### §7.12–7.15：从局部等价到约束和搜索空间

§7.12 讨论冗余 join/outerjoin 消除：只是不使用右侧列还不够，bag 下还要考虑每条记录至少/至多有多少个匹配。§7.13 给出 semi/anti join reducer，并用分布式数据传输解释用途。§7.14 Outerjoin Simplification 只有标题，已有相关正文在 §7.10.1。见 [pp.292–294](../Query%20opt.pdf#page=313)。

§7.15 则是实质性的长节（pp.294–307）：从可用变换定义 core search space，介绍 DPsube、适用性测试、SES/TES 和冲突规则，依次比较 CD-A/B/C，再讨论一元算子、混合算子、退化谓词和笛卡尔积。关键是“只产生合法计划”和“不漏掉范围内的合法计划”的区别。作者在 p.307 对 cross products / degenerate predicates 明确放弃完整覆盖，不能只读标题就概括成无条件完备。

用户熟悉 join reorder，可以缩短算法机械执行的复习；这一节关于代数限制如何影响可达计划的内容仍应读。

### §7.16–7.18：有序代数和文献边界

§7.16 引入 SAL/NAL 背景，对序列递归定义 selection、projection、map、product/join、grouping、groupjoin 和其他算子，讨论仍成立或失效的等价。序列下 product 和 join 不再一般可交换；去重投影本身也有特别的顺序约定，不能把“保序代数”理解成每个操作都自动保留所有顺序。见 [pp.308–312](../Query%20opt.pdf#page=329)。

§7.17 汇集多个代数体系与主题的文献，§7.18 记录 ToDo，正文结束于 p.313。这些内容用于确认覆盖范围和后续资料需求，不假装已经存在完整讲解。

## 第 8、9 章：查询表示与翻译的预留位置

第 8 章列出 relational calculus、Datalog、tableaux、monoid comprehension 和 expressiveness；有少量关于 tableaux 能表示哪些查询的说明，其余多为文献。第 9 章列出 query language→calculus、query language→algebra、calculus→algebra 和反向转换，基本没有正文。见 [pp.315–318](../Query%20opt.pdf#page=336)。

教材需要保留这两章的概念位置，并补足阅读第 10 章所必需的 CQ、head/body 和变量映射背景。补充会明确标作教学补充，不借此把空白章节扩写成未经核验的“原书完整内容”。

## 第 10 章：等价、包含与最小化是怎样的问题

开头定义对所有数据库实例的 query equivalence 和 containment，再用重复子目标在 set/bag 下表现不同的例子说明语义选择的影响。§10.1.1 在集合语义下介绍 conjunctive queries、containment mapping、包含判定的映射方向、最小查询及同构，以及合取查询的并。§10.1.2 加入不等式，讨论稠密全序域、映射之外的蕴含条件，以及一个方向不再充分刻画全部情况的反例。见 [pp.319–323](../Query%20opt.pdf#page=340)。

§10.1.3–5 的否定、约束和聚合大多是文献入口；§10.2 的 bag semantics 主要是研究提要。§10.3 罗列 XPath 子语言与包含复杂度的文献结果，不是一般 sequence 包含的完整教程。§10.4 最小化与 §10.5 公共子表达式检测主要是标题和引用，§10.6 补充文献。标题虽然包含 factorization，但没有独立展开一套完整 factorization 方法。

本章教学应先让读者理解问题、例子和映射方向，再说明作者给出了哪些结论、哪些仍需外部资料。涉及复杂度的历史结论要保留语言片段和语义限定，不能泛化为任意 SQL 的复杂度结论。

## 对教学结构的直接影响

新的教学主线由原书决定：第 5、6 章是正式起点；聚合、线性、数据表示、sequence 和查询包含都拥有独立模块；grouping 按原文展开为较大模块；d-join 作为局部专题保留。对第 8、9 章以及其他空缺，分别标记原文提要和教学补充。

详细小节及原文映射见[教学提纲](outline.md)。本次通读完成了范围和依赖梳理；后续每课仍会复核所用公式，特别是原书尚未完成、表述过强或符号存在问题的位置。
