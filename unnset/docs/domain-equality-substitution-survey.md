# 论文中的等值绑定替换：Spark 与 DuckDB

本文聚焦《Unnesting Arbitrary Queries》（2015）第 4 节的一个优化：**用内层已有列表示域 D 的绑定列，从而省略域连接；同时考虑删除连接后失去的过滤作用。** 先说明论文问题，再独立介绍 Spark 和 DuckDB，最后比较两者。

本文及两篇独立调查只以源码为依据，没有运行测试或性能实验；计划形状均为代码推导。调查日期为 2026-09-06，结论限定于以下固定快照及文中指明的路径。


| 系统     | 源码快照                                       | 独立调查                                                             |
| ------ | ------------------------------------------ | ---------------------------------------------------------------- |
| Spark  | `d7cb6592d67f92d11239e1cead153d6fc80fce18` | [绑定需求、谓词提升、部分替换及上层匹配](spark-domain-equality-code-study.md)       |
| DuckDB | `154c2c8d5f67aa8ddbadf93598a4a29051430a0c` | [生成域消除、半连接保留及 Deliminator](duckdb-domain-equality-code-study.md) |


论文原文见 [PDF](../paper/UnnestQuery-btw2015.pdf)，中文内容见[第 1–4 节翻译](UnnestQuery-btw2015.zh.html)。

## 1. 论文优化究竟改变了什么

沿用论文 Q1：

```sql
SELECT s.name, e.course
FROM students s, exams e
WHERE s.id = e.sid
  AND e.grade = (
    SELECT MIN(e2.grade)
    FROM exams e2
    WHERE s.id = e2.sid
  );
```

D 是去相关引入的去重绑定集合。内层的 `d.id=e2.sid` 同时做了两件事：提供当前绑定的 `id`，并排除 `sid` 不属于 D 的考试行。等值条件让我们可以改由 `e2.sid` 提供绑定值；是否还要保留域的成员资格检查，是下一步选择。

对这个单键等值形状，可以区分三种方案：


| 方案        | 聚合使用的绑定键来自哪里 | 聚合前是否检查键属于 D                |
| --------- | ------------ | --------------------------- |
| 保留域连接     | D 的列         | 是，通过原域连接                    |
| 列替换并保留半连接 | `e2.sid`     | 是，通过 `exams e2 SEMI JOIN D` |
| 列替换并删除域连接 | `e2.sid`     | 不再通过这个域连接检查                 |


第一种到第二种，在 D 的完整键唯一、输出可由等值列重建并保留原比较语义的条件下，可以保持局部等价。第二种到第三种可能扩大局部结果：`D={1}`、`e2.sid={1,2}` 时，内层可能多算出键 2 的聚合结果。

论文允许后一步，依赖的是去相关后的完整上下文：按绑定分别计算结果，上层仍按该绑定接回原外层行。键 2 没有对应外层绑定，最终不会输出。它没有要求 D 与内层键的值集合相等，也没有允许把任意局部超集直接当成等价子树。

所以，代码调查要分别回答：**系统怎样维护这个语义上下文；怎样决定是否放弃聚合前的过滤。** 统计信息用于后一个成本问题，普通基数估计不能代替前一个正确性依据。

## 2. Spark：扣除可替代绑定，按剩余需求建立域

Spark 在 `DecorrelateInnerQuery` 递归中维护外层引用的需求集合。遇到可识别的等值列对应，就记录 `外层引用→内层列`，扣除该变量，并把可提升的相关等值条件返回给上层连接。到达不相关子树时，需求为空便不建立 `DomainJoin`；还有需求则只为剩余变量建立域。[核心递归与 Filter 分支](https://github.com/apache/spark/blob/d7cb6592d67f92d11239e1cead153d6fc80fce18/sql/catalyst/src/main/scala/org/apache/spark/sql/catalyst/optimizer/DecorrelateInnerQuery.scala#L495)

Q1 中 `s.id` 可以由 `e2.sid` 表示，所以内层直接按 `sid` 聚合，上层保留 `s.id=e2.sid` 的匹配。如果内层再需要一个无法等值替代的 `s.cutoff`，则只为 `cutoff` 生成域，并保留 `sid,cutoff` 的分组与上层对应关系。

这里有两个独立检查：映射收集器接受直接的外层引用与内层属性对应；`canPullUpOverAgg` 检查相关谓词能否跨过聚合。窗口、集合算子、非内连接及空输入聚合需要各自的处理，不能仅凭存在一个等号就套用上述变化。

在所查替换分支中，Spark 没有按 D 的过滤收益选择保留完整域，也没有提交保留/删除两种候选进行成本比较。它的主要实现特点是**按列消除绑定需求，并维护映射和上层条件**。完整调用链、条件及示例见 [Spark 独立源码调查](spark-domain-equality-code-study.md)。

## 3. DuckDB：替换生成域列，可删除连接或保留半连接

DuckDB 先通过相关子查询展开生成 `DelimJoin` / `DelimGet`，已经明确域来源、绑定分组和上层接回关系。当前快照默认在 flatten 收尾时将其转换到 CTE 结构，并由 `GeneratedDedupRefEliminator` 处理生成域引用。[默认路径入口](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/flatten_dependent_join.cpp#L273)

等值删除分支检查生成域来源、所有域列的覆盖、绑定映射以及过滤和输出表达式是否能重建。通过后，内层列承担原域列的使用，聚合和上层引用随之更新；域连接被删除。所有相关引用都消失时，这个域的 CTE 定义也无需构造。

对论文强调的过滤损失，默认路径还有一个明确选择：域来源被判定具有选择条件，且连接位于聚合下方或需保护的存在性判断分支时，尝试 `PreserveJoinAsSemi`。此时上层依然改用内层列，底部则保留精确半连接，在昂贵计算前筛掉 D 之外的键。[删除与保留的分支](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1082)

保留 Delim 结构的另一条路径由 `Deliminator` 处理：其 `HasSelection` 成立时，保留最深的一个候选域连接，继续尝试删除其他候选。这与默认 CTE 路径的半连接转换是两套流程。

两条路径直接选择是否保留过滤时，都以结构启发式为主，没有在所查分支中比较 D 的实际覆盖率、聚合工作量和物理成本。完整过程见 [DuckDB 独立源码调查](duckdb-domain-equality-code-study.md)。

## 3.1 用同一条混合谓词 SQL 看插入时机

考虑下面同时包含等值和非等值相关条件的查询：

```sql
SELECT s.id,
       (SELECT MIN(e2.grade)
        FROM exams e2
        WHERE e2.sid = s.id AND e2.grade < s.cutoff) AS min_grade
FROM students s;
```

这里 `e2.sid = s.id` 可以用内层的 `e2.sid` 表示绑定键；`e2.grade < s.cutoff` 却不能由某个等值列替代，它仍然需要当前外层行的 `cutoff`。因此，问题不是“有没有等号”，而是**等号消掉一部分绑定后，剩下的绑定域在哪里插入，以及原来的域连接是否还承担过滤作用**。

| 观察点 | Spark | DuckDB |
| --- | --- | --- |
| 域结构何时出现 | `DecorrelateInnerQuery` 递归处理过滤条件时，先记录 `s.id → e2.sid`，从需求集合扣除 `id`；随后只为仍需的 `s.cutoff` 建立 `DomainJoin`。因此它从一开始就可能是“剩余绑定域”。 | `LogicalDependentJoin` 展开为 `DelimJoin`，内层通过 `DelimGet` 读取去重的相关绑定。此时 Delim 结构先承载完整相关上下文，等值列替换在后续 flatten/CTE 重写阶段发生。 |
| 聚合看到的绑定 | 聚合按 `e2.sid` 以及接回结果所需的 `cutoff` 维度隔离；`e2.grade < cutoff` 仍是聚合前的相关过滤。 | flatten 先把 Delim 绑定列纳入聚合的分组和输出；随后 `GeneratedDedupRefEliminator` 可将域列使用改写为 `e2.sid` 等内层列，同时保留 `cutoff` 的绑定关系。 |
| `DelimGet`/`DomainJoin` 的去向 | 若剩余需求只有可由内层列表示的部分，`DomainJoin` 根本不会创建；本例因 `cutoff` 仍被非等值谓词使用，不能据等值替换把整个域结构都消掉。 | `DelimGet` 是内层读取域的入口，`DelimJoin` 是把按域计算的结果接回外层的依赖连接。等值替换成功后，`DelimJoin` 可以删除；若域来源的选择条件会影响聚合前结果，则可改写为精确 `SEMI` 连接，继续过滤不属于域的 `sid`。 |

用计划形状表示，Spark 更接近“先缩小绑定需求，再插入域”：

```text
外层 students(s.id, s.cutoff)
  └─ 仅为 cutoff 保留的 DomainJoin
       └─ Aggregate(MIN(e2.grade), 绑定: e2.sid + cutoff)
            └─ Filter(e2.sid = 绑定的 id, e2.grade < cutoff)
```

DuckDB 更接近“先把依赖域显式放进树，再做替换”：

```text
DelimJoin(外层 students, 按相关绑定接回)
  └─ Aggregate(MIN(e2.grade), 绑定列来自 DelimGet)
       └─ Filter(e2.sid = DelimGet.sid,
                 e2.grade < DelimGet.cutoff)
            └─ exams e2
```

后续重写可能把上图中的 `DelimGet.sid` 改成 `e2.sid`，并删除 `DelimJoin`；也可能只删除其接回职责、在 `exams` 与域之间留下 `SEMI` 过滤。对于这条 SQL，非等值条件使 `cutoff` 的相关性继续存在，所以“`sid` 可替换”不等于“整个 `DelimGet`/`DomainJoin` 都没有必要”。这正是两套实现的关键差异：Spark 的插入点体现为**剩余变量需求**，DuckDB 的插入点体现为**已物化的 Delim 域及其后续消除/保留决策**。

<a id="unified-framework"></a>

## 4. 统一的代码框架：相关状态下传，返回时重建，最后处理域

Spark 和 DuckDB 可以放进同一个代码框架来读。这个框架描述的是**信息怎样在计划树中流动**，不是说两边使用同名类或同一轮函数：

```text
输入：带外层引用的子查询计划

1. 建立相关性载体
   Spark：ScalarSubquery + OuterReference
   DuckDB：LogicalDependentJoin + correlated_columns

2. 从当前算子向子树传递相关状态
   state = “下面还需要哪些外层绑定、这些绑定当前由哪些列表示”
   对 Filter / Project / Aggregate / Join / SetOp 等节点分别处理
   再递归 child

3. 从 child 返回时重建当前算子
   合并 child 返回的绑定映射和接回条件
   把接回所需的绑定列补进 Aggregate 的分组与输出
   用映射重写仍留在当前节点的表达式

4. 到达独立子树后实现绑定域 D
   若 state 为空：不引入域连接
   若仍有未替代绑定：建立“外层绑定的去重关系 + 域列 + 成员资格条件”

5. 应用等值替代并决定域连接的去向
   内层列可以表示某个域列时，更新绑定映射和上层接回条件
   域连接可以被删除，也可以保留为聚合前的 SEMI 过滤

6. 将内层结果接回外层
   按绑定键连接，恢复标量空输入、多行检查、NULL 和 COUNT 等语义
```

这里的 D 不是一个必须在代码中叫作 `D` 的对象，而是一组协同的逻辑结构：外层绑定的去重输入、域列的属性映射、内层使用这些列的条件，以及最终按绑定取回结果的连接。这个定义也解释了为什么“删除域连接”不能只看一个等号：还要同时改写引用、保持聚合按绑定隔离，并决定原来的域成员资格过滤是否仍需保留。

从遍历方向看，用户的概括可以精确成两层。第一层是**状态下传**：从根节点进入子树时，递归把相关绑定需求和上下文传下去；这可以理解成 dependent join 的依赖被下推，但不意味着把同一个算子物理搬过计划树。第二层是**返回时重建**：递归调用完成后，父节点消费子树返回的映射、条件和输出绑定。DuckDB 在这之后还有显式的 `GeneratedDedupRefEliminator::RewriteSubtree` 子树先行重写；Spark 则在 `DecorrelateInnerQuery` 返回计划后，由 `rewriteDomainJoins` 另行展开仍存在的 `DomainJoin`。因此，“自底向上处理 D”是一个有用的总体印象，但不能当成两边完全相同的函数调用顺序。

同一个阶段在两套源码中的落点如下：

| 统一阶段 | Spark 的代码落点 | DuckDB 的代码落点 |
| --- | --- | --- |
| 建立相关性载体 | `PullupCorrelatedPredicates` 处理 `ScalarSubquery`；`DecorrelateInnerQuery.apply` 读取 `OuterReference` | `PlanCorrelatedSubquery` 创建 `LogicalDependentJoin` |
| 状态下传与算子递归 | `decorrelate(plan, parentOuterReferences, aggregated, underSetOp)` | `FlattenDependentJoins::DecorrelateSubtree` 与 `PushDownCorrelatedNodeInternal` |
| 返回时补齐绑定 | `Filter` 返回 `joinCond/map`；`Aggregate` 补分组和输出 | `UnnestingState`、`ColumnBindingRewrite`、`PushDownAggregate` |
| 域的逻辑表示 | 剩余需求产生 `DomainJoin`；等值映射可在此之前扣除需求 | `DelimGet`、`DelimJoin`；flatten 时先把域接入计划 |
| 等值后的域处理 | `rewriteDomainJoins` 物化仍存在的域 | `GeneratedDedupRefEliminator` 删除连接或转为 `SEMI`；关闭 CTE 路径时由 `Deliminator` 处理 |
| 外层接回 | `RewriteCorrelatedScalarSubquery.constructLeftJoins` | `FinalizeDependentJoin` 及后续 Delim/CTE 重写 |

这些落点分别对应 Spark 的 [`DecorrelateInnerQuery.apply`](https://github.com/apache/spark/blob/d7cb6592d67f92d11239e1cead153d6fc80fce18/sql/catalyst/src/main/scala/org/apache/spark/sql/catalyst/optimizer/DecorrelateInnerQuery.scala#L464) 与 [`constructLeftJoins`](https://github.com/apache/spark/blob/d7cb6592d67f92d11239e1cead153d6fc80fce18/sql/catalyst/src/main/scala/org/apache/spark/sql/catalyst/optimizer/subquery.scala#L901)，以及 DuckDB 的 [`DecorrelateSubtree`](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/flatten_dependent_join.cpp#L306) 与 [`GeneratedDedupRefEliminator::RewriteSubtree`](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1082)。

所以两者的统一表述应是：**先把相关子查询表示成带绑定状态的逻辑计划，再沿计划树下传依赖、在返回时重建算子和绑定输出，最后把仍需的域实现出来或用等值映射消除，并把结果按绑定接回外层。** Spark 倾向于在域占位符创建前扣除可替代绑定；DuckDB 倾向于先生成 Delim 域，再在另一轮中替换或保留域过滤。这是同一框架中的不同实现时机。

## 5. 两者的差异，应该怎样理解


| 比较项         | Spark 的所查路径           | DuckDB 的所查路径                            |
| ----------- | --------------------- | --------------------------------------- |
| 直接处理的对象     | 向下传递的外层绑定需求           | 已生成的域引用及其连接                             |
| 等值优化的动作     | 建立属性映射、提升适用谓词、扣除绑定需求  | 建立列替换图、重建输出和过滤、删除连接或转为 SEMI             |
| 部分替换的粒度     | 可逐个变量扣除，只为剩余变量建立域     | 本文所查生成域连接消除函数要求覆盖该引用的全部基础域列；不是相同的逐列缩域分支 |
| 聚合怎样保持绑定隔离  | 将上层连接需要的内层属性加入分组与输出   | flatten 已加入绑定分组；替换时维护分组及上层列引用           |
| 额外内层键怎样消失   | 返回的相关条件用于上层匹配         | 保留上层接回关系，并更新绑定使用位置                      |
| 是否显式保留原域过滤  | 直接等值分支没有按过滤收益保留 D 的选择 | 默认 CTE 路径可转为精确半连接；Deliminator 可保留最深候选   |
| 直接决策是否比较域成本 | 未发现                   | 未发现；过滤保护采用结构启发式                         |


**Spark 的特点是按变量分析剩余需求。** 它能够在同一分支中混合“内层列提供一部分绑定”和“域提供其余绑定”。但“在创建 DomainJoin 前处理”主要说明算法组织顺序；如果另一个系统在规划结束前删除同样的节点，不能据此判定最终执行更快。缩小域维度也可能丢失绑定列之间的组合约束，使内层计算增加。

**DuckDB 当前默认路径对论文的过滤问题有更直接的处理。** 半连接分支表明，列替换和保留过滤可以同时成立。因此，研究它时不宜只看 DelimGet 是否消失，还要看绑定值来自哪里、成员资格检查是否仍在，以及该检查位于聚合前还是聚合后。其不足是保护依据仍偏向“存在什么条件和算子”，尚未在这里量化收益。

**两者都不能称为已经完整实现论文要求的成本比较。** Spark 后续有动态分区裁剪和运行时过滤；DuckDB 后续有连接过滤下推及运行时过滤。这些机制在适用条件下可能恢复部分提前过滤收益，但不能等同于当前等值替换分支在比较“保留完整 D”与“独立计算内层”。[Spark 后续过滤批次](https://github.com/apache/spark/blob/d7cb6592d67f92d11239e1cead153d6fc80fce18/sql/core/src/main/scala/org/apache/spark/sql/execution/SparkOptimizer.scala#L65)、[DuckDB 连接过滤下推](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/optimizer/join_filter_pushdown_optimizer.cpp#L160)

对分布式环境也应保持这个区分：源码中的提前替换本身没有回答额外聚合、域广播、Shuffle、过滤器分发和共享外层输入的总成本。当前调查能够确认两个系统怎样产生这些逻辑方案，不能据此给出分布式性能排名。
