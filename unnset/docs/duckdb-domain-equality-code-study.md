# DuckDB 如何实现论文中的等值绑定替换

本文独立追踪 DuckDB：去相关如何生成绑定域，后续怎样用内层列替代域列，以及何时保留域的过滤作用。调查日期为 2026-09-06，源码固定到 `154c2c8d5f67aa8ddbadf93598a4a29051430a0c`。本文是源码分析，计划图为代码推导，没有运行测试或性能实验。

返回[论文问题与两系统比较](domain-equality-substitution-survey.md)；另见 [Spark 独立调查](spark-domain-equality-code-study.md)。

## 1. 核心方案：识别生成域，替换绑定，再选择删除或保留过滤

DuckDB 的相关子查询展开会生成专用的 `DelimJoin` / `DelimGet` 结构。等值优化在这个已知上下文中识别域列与内层列的对应关系，改写使用域列的位置，并消除不再需要的域连接。

当前快照有两条应当分开阅读的路径：

| 路径 | 入口与职责 |
|---|---|
| 默认 CTE 路径 | `delim_join_as_cte=true`；在 dependent join flattening 收尾时，将域引用转换为生成的 CTE 引用，并尝试消除或转成半连接 |
| 保留 Delim 的路径 | 例如关闭上述设置后，由后续 `Deliminator` 优化器删除与 `DelimGet` 相连的连接 |

所以，当前主干不能概括成“所有域消除都等到后续优化器阶段才做”。下文以默认 CTE 路径为主，再单独介绍另一条路径。[默认设置](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/common/settings.json#L542)、[flatten 收尾入口](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/flatten_dependent_join.cpp#L273)

## 2. 先认识 DelimJoin、DelimGet 和上层匹配

`DelimJoin` 标记了相关子查询展开的上下文；其 `duplicate_eliminated_columns` 表示用于去重绑定的列。`DelimGet` 是内层读取这些绑定的逻辑节点。论文的 D 对应这里的去重绑定集合，并不是用户查询中额外写的一张表。

展开聚合时，`AddDelimColumnsToGroup` 把绑定键加入分组，保证各绑定独立聚合；`CreateDelimJoinConditions` 用 NULL 安全比较把返回的绑定接回原外层。之后消除域列时，需要连同这些使用位置一起更新。[分组键维护](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/flatten_dependent_join.cpp#L756)、[上层绑定条件](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/flatten_dependent_join.cpp#L252)

以论文 Q1 的 `e2.sid=s.id` 为例，与本次优化有关的骨架是：

```text
上层：原外层与内层聚合结果按绑定键连接
                    ↑ 保留这个对应关系
内层：Aggregate [d.id] → MIN(e2.grade), d.id
        Join d.id = e2.sid
          DelimGet D
          exams e2
```

这只是关键子树示意。空输入聚合等语义还可能生成额外域连接，不能把它当成任意标量子查询的完整计划。

## 3. 默认 CTE 路径的完整调用顺序

```text
FlattenDependentJoins::DecorrelateIndependent
  → DecorrelateSubtree：展开相关性，维护绑定状态
  → DelimJoinCTERewriter::Rewrite
    → 将适用的过滤条件推入 DelimJoin 输入
    → RewriteDelimJoinsToCTEs
      → MaterializeDelimJoinAsCTE
        → 上层 DelimJoin 改为普通 ComparisonJoin
        → DelimGet 改为当前生成域的 CTERef
        → GeneratedDedupRefEliminator::Remove
        → 若引用全部消失：返回，不构造域的 CTE 定义
        → 若仍有引用：构造共享外层输入及去重域的 CTE 定义
    → GeneratedDomainJoinEliminator：清理剩余可消除的生成域连接
    → VerifyNoDelim
```

`GeneratedDedupRefEliminator` 由优化器总开关和 `DELIMINATOR` 是否被禁用共同控制。这里先创建 CTE 引用，再决定是否需要 CTE 定义；创建逻辑引用不表示域已经执行或物化。[CTE 转换与消除入口](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L2479)、[整体重写流程](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L2693)

## 4. 哪些等值连接可以被替换

核心函数为 `GeneratedDedupRefEliminator::RemoveJoin`。本文只讨论其中的等值分支；源码另有范围更窄的非等值处理，不属于这次论文等值优化的主线。

| 检查 | 为什么需要 |
|---|---|
| 连接为受支持的 INNER / SEMI 形状 | 不将删除内连接的结论推广到外连接等语义 |
| 恰好一侧可识别为当前生成域的引用 | 确认域的来源和去相关上下文；允许追踪受支持的 Filter / Project 包装 |
| 比较为 `=` 或 `IS NOT DISTINCT FROM` | 建立域输出与另一侧列的等值关系 |
| 另一侧是可提取的列绑定，替换图能够接受映射 | 后续各算子的列引用能够一致地改写 |
| `CoversAllDedupColumns` 通过 | 所有基础去重域列都被覆盖；该函数要求每列恰好出现一次 |
| 域输出表达式及过滤表达式都能重建 | 重写后不能留下对已删除域侧的悬空引用 |

覆盖检查尤其重要：D 按完整绑定元组去重，并不保证任意一个域列单独唯一。只发现多列 D 中一列相等，不能据此删除整个域连接。[域引用识别](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L949)、[覆盖检查](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1022)、[RemoveJoin](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1509)

这里的覆盖是**表达式和输出列的覆盖**，不是查询统计信息以证明 D 与内层列的值集合相同。

## 5. 删除连接时，具体修改了什么

等值分支建立域列到内层列的 `BindingReplacementGraph`，重建域侧输出和 Filter，并把原连接替换为非域一侧及必要过滤。随后把列替换应用到重写上下文中的使用位置；向上返回时还通过 `ColumnBindingRewrite::ApplyToChild` 维护父节点引用。

普通等值 `d.id=e2.sid` 在列替换后可以成为 `e2.sid=e2.sid`。这个条件对 NULL 不为真，因此不能把它一律删成 TRUE。代码保留重写后的比较；只对恒真的 NULL 安全自比较作专门删除。原域侧可重写的过滤条件也需要保留。[等值分支的过滤重建和全局引用更新](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1586)

对 Q1 的上述子树，删除分支可以概括为：

```text
Aggregate [e2.sid] → MIN(e2.grade), e2.sid
  Filter：保留普通等值要求的非 NULL 语义
    exams e2

绑定来源：d.id 改由 e2.sid 表示
上层使用：继续通过聚合输出的绑定键接回原外层
```

局部结果可能多出 D 中不存在的键，但每个已有绑定仍在自己的分组内计算，额外键由上层匹配排除。最后一个生成域引用也消失时，CTE 转换入口才会跳过域定义的构造；删除某一个连接并不保证整个域都不再被其他位置使用。

## 6. 保留过滤：列替换后，把连接转为半连接

`GeneratedDedupRefEliminator::Remove` 先用 `HasDelimiterDomainSelection` 检查域来源一侧的选择条件，再将结果传给 `RewriteSubtree`。后者在以下情况下尝试保留域过滤：

```text
域来源被判定有选择条件
并且
当前连接位于聚合下方，或属于需要保护的存在性判断分支
```

触发后调用 `PreserveJoinAsSemi`；对于 `Filter + CrossProduct` 形状，有对应的 `PreserveFilterCrossProductAsSemi`。保留分支也要通过等值映射、域列覆盖和可重建性检查。检查失败时，这个局部分支保留原形状。[选择标志来源](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1766)、[删除或保留的分支](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1082)

`PreserveJoinAsSemi` 的动作是：将非域输入放在保留输出的一侧，把连接改为 SEMI，并把上层使用的域列替换为内层列。替换访问器在半连接处停止，保留半连接内部对 D 的真实比较。于是得到：

```text
Aggregate [e2.sid] → MIN(e2.grade), e2.sid
  SemiJoin e2.sid = d.id
    exams e2
    D 的生成引用
```

**绑定值已经由内层列提供，但“该行的键是否属于 D”仍在聚合前精确检查。** 对这个简单等值形状，D 的完整键唯一，所以半连接既保留成员资格过滤，也不会按 D 的重复次数放大内层行。[半连接转换与替换边界](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L1281)

后续 `GeneratedDomainJoinEliminator` 清理生成域连接时，也检查聚合或存在性判断下的选择性域，避免把之前保留的过滤直接删掉。[清理阶段的保护条件](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L2113)

## 7. “有选择条件”是不是统计驱动的判断

不是。`HasDelimiterDomainSelection` 根据计划结构递归检查 Filter、扫描过滤及其所在分支，并跳过某些与当前绑定无关的可空侧或子查询判断侧。

其中 Filter 分支用 `IsNonSelectiveJoinPredicate` 将某些列等值条件归为不触发保护的条件；扫描过滤的检查则单独跳过 `IS NOT NULL`。这是代码中的启发式分类，不证明这些条件对实际数据没有过滤效果。函数没有以域 NDV、与内层的键重叠率或代价估算来决定返回值。[选择条件识别](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L432)、[域来源路径检查](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/delim_join_cte_rewriter.cpp#L554)

因此，这条实现已经显式照顾论文指出的过滤损失，但尚不能描述为完成了“估计保留 D 与删除 D 的代价，择优选择”。例如，没有显式 Filter 的小外层表也可能产生很强的域过滤；仅靠语法结构无法判断这一点。这是对判断依据的分析，没有通过本轮实验测量具体计划。

## 8. 另一条路径：Deliminator 如何处理 DelimGet

保留 Delim 结构时，普通优化器中的 `Deliminator` 才有相应候选。其注册位置在 Join Order 之前。[优化器入口](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/optimizer/optimizer.cpp#L296)

```text
Deliminator::Optimize
  → FindCandidates：寻找 DelimJoin 及其右侧的 DelimGet 连接
  → 候选按深度排列
  → HasSelection 成立：保留最深的一个候选连接
  → RemoveJoinWithDelimGet：尝试删除其余候选
  → 全部 DelimGet 消失：上层改为普通 ComparisonJoin
```

等值删除分支要求受支持的 INNER / SEMI 连接、连接条件数等于 DelimGet 列数、两侧为列引用等条件。它建立列映射，保留域侧 Filter，为普通等值加 `IS NOT NULL`，删除底部连接，然后改写上层引用。[DelimGet 删除逻辑](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/optimizer/deliminator.cpp#L187)

这里的过滤保护较粗：`HasSelection` 从整个候选 DelimJoin 递归检查，并非默认 CTE 路径那套域来源追踪；保护动作是把最深候选排除出删除列表。其 TODO 提到未来使用采样选择率，当前代码没有实现该成本选择。[候选选择与保护](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/optimizer/deliminator.cpp#L56)、[HasSelection](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/optimizer/deliminator.cpp#L119)

这两条路径不能混写成先后必经阶段：默认 CTE 重写结束时会检查已无 Delim 节点；后续 `Deliminator` 虽仍注册，也不能据此推断它还会处理已消失的节点。

## 9. 空输入语义与后续运行时过滤

域删除受到整个去相关结构的约束。`FlattenDependentJoins` 对全局聚合维护额外域匹配，并为 COUNT 等聚合记录结果修正；当前 CTE 消除函数也没有将所有 LEFT、SINGLE 等连接作为普通等值删除候选。因此，简单 MIN 子树的示意不能直接套用到 COUNT、外连接或任意多行标量子查询。[全局聚合与 COUNT 处理](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/planner/subquery/flatten_dependent_join.cpp#L805)

后续 `JoinFilterPushdownOptimizer` 在适用条件下可以沿聚合分组键追踪过滤目标；物理连接的 Bloom filter 判断又会使用实际 build 行数和估计 probe 基数。这能为某些普通连接提供提前过滤，但其触发还受连接方向、比较和可下推位置限制，不能把它视为域删除的必然补偿。[穿过聚合的过滤目标追踪](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/optimizer/join_filter_pushdown_optimizer.cpp#L271)、[物理 Bloom filter 判断](https://github.com/duckdb/duckdb/blob/154c2c8d5f67aa8ddbadf93598a4a29051430a0c/src/execution/operator/join/physical_hash_join.cpp#L1301)

就论文的等值优化而言，DuckDB 最值得关注的是：**在已知生成域的上下文中更新所有绑定使用位置，并把“内层列提供绑定值”和“域继续做成员资格过滤”分开处理**。当前默认路径已经有精确半连接保留方案，直接选择依据仍以结构启发式为主。
