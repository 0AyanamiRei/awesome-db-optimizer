# L09b · 从 FD 到开源优化器的实际代码

本节是 L09 的**源码调查补充**，不是某个产品的使用手册。调查基于 2026-10-01 能读取到的版本：PostgreSQL `REL_18_0`、MySQL `mysql-8.4.0`、TiDB `v8.5.0`、Apache Calcite `calcite-1.40.0`，以及 CockroachDB `v24.1.0` 的 FD 属性实现。链接都指向具体源码或官方文档；函数名可能随版本移动。

先建立一个阅读方法：看到 `FunctionalDependency`、`UniqueKey`、`GroupBy` 或 `areColumnsUnique`，不能马上说“这个优化器使用了 FD”。要继续追踪三件事：

1. **产生者**：主键、唯一索引、谓词等事实在哪里进入属性结构？
2. **传播者**：Filter、Project、Join、Aggregate、Outer Join 如何保留、削弱或增强它？
3. **消费者**：哪条验证、代数改写、连接消除或基数估算规则真正读取它？

## 1. 四种实现路线

开源系统大致有四条路线，常常同时存在：

| 路线 | 保存的对象 | 典型消费者 | 能否作为等价改写证明 |
| --- | --- | --- | --- |
| 唯一键元数据 | `K` 是否唯一，是否忽略 NULL | 去掉 DISTINCT、连接消除、聚合消除 | 可以，若唯一性是受保证约束 |
| 逻辑 FD 图 | `X → Y`、常量、等价、strict/lax | 分组检查、排序简化、去相关 | 可以，需跟踪 NULL 和算子语义 |
| 最高行数 / 单行属性 | `maxRows ≤ 1` | 标量子查询、单例聚合、表达式展开 | 可以，但它比一般 FD 更强 |
| 统计依赖 | 依赖强度，如 `postcode => city: 1.0` | 选择率与 NDV 估算 | **不可以**；统计误差不能改变结果 |

“唯一键元数据”经常是 FD 的压缩表示：如果 `K` 是 strict key，则有 `K → A(R)`。反方向不总是成立，因为值层 FD 不排除完全相同的重复行。

## 2. PostgreSQL：约束驱动的分组键消除、连接消除和统计依赖

### 2.1 语义改写：从 PRIMARY KEY 到 GROUP BY

PostgreSQL 的 `remove_useless_groupby_columns` 位于 [src/backend/optimizer/plan/initsplan.c](https://github.com/postgres/postgres/blob/REL_18_0/src/backend/optimizer/plan/initsplan.c#L397)。它的目标就是删除“由其他 GROUP BY 列函数决定”的多余列。

当前实现采用一个保守、容易验证的充分条件：

- 只处理普通 query block，不处理 grouping sets；
- 只看简单 `Var`，不推理任意稳定表达式；
- 从基表的唯一、立即生效、非部分索引中挑选列集；
- 普通唯一索引的键列必须 `NOT NULL`；
- `NULLS NOT DISTINCT` 索引可以在可空列上提供相应唯一性；
- 在 GROUP BY 列集中找一个真子集，删除其余同表列。

源码注释还说明了计划缓存为何需要依赖约束和索引失效：索引被删除或列的非空属性改变，缓存计划必须失效。这里的证明不是“本次样例恰好唯一”，而是 catalog 约束在计划有效期内成立。

例子：`GROUP BY t.id, t.name`，若 `id` 是非空主键，PostgreSQL 可以改为 `GROUP BY t.id`。它减少 sort/hash key 的宽度，但不会自动删除 `Aggregate`；多行仍可能属于同一个 id，`SUM` 仍要算。

### 2.2 语义检查：允许 SELECT 列依赖 GROUP BY

在 [check_functional_grouping](https://github.com/postgres/postgres/blob/REL_18_0/src/backend/catalog/pg_constraint.c#L1723) 中，PostgreSQL 为 SQL 语义检查判断某个关系的主键是否包含于 grouping columns。当前代码只使用 primary key；注释明确说，普通 UNIQUE 还需要把 `NOT NULL` 事实纳入依赖记录，而现有 constraint dependency 表示不够方便。

因此下面查询可被接受：

```sql
SELECT t.id, t.name, count(*)
FROM t
GROUP BY t.id;
```

前提是 `t.id` 是该表的主键。这个检查与前一小节的 GROUP BY 缩减相关但不相同：前者决定查询是否合法，后者改变计划中实际使用的分组键。

### 2.3 左连接消除：需要唯一性，而不只是 FD

PostgreSQL 在 [src/backend/optimizer/plan/analyzejoins.c](https://github.com/postgres/postgres/blob/REL_18_0/src/backend/optimizer/plan/analyzejoins.c#L144) 的 `join_is_removable` 检查左连接能否完全删除。它要求右侧是单个基表、上层不需要右侧列，并通过 `rel_supports_distinctness` 与连接条件证明右侧最多匹配一行。

这正对应 L09 的区别：右表主键给出 `key → all columns`，但删除 `LEFT JOIN` 还需要“每个左行最多产生一行”。内连接则还需要匹配存在性；PostgreSQL 不能仅凭右侧唯一键补出这个条件。

### 2.4 统计依赖：同名概念，证据等级不同

PostgreSQL 的 [extended statistics 文档](https://www.postgresql.org/docs/18/planner-stats.html#PLANNER-STATS-EXTENDED-FUNCTIONAL-DEPS) 使用 `CREATE STATISTICS ... (dependencies)` 采集列组之间的依赖强度，例如 `zip => city: 1.0`。在源码 [src/backend/statistics/dependencies.c](https://github.com/postgres/postgres/blob/REL_18_0/src/backend/statistics/dependencies.c#L983) 中，它被用于修正多个常量等值谓词的选择率，避免把相关条件当独立事件相乘。

官方文档列出限制：目前只用于列与常量的简单等值和常量 IN，不用于列列等值、范围、LIKE；若两个常量实际上不相容，估算也可能非零。它绝不能成为删除过滤、连接或 DISTINCT 的语义许可。

## 3. MySQL：把 FD 推导用于 ONLY_FULL_GROUP_BY 检查

MySQL 8.4 的主要入口是 [sql/aggregate_check.cc](https://github.com/mysql/mysql-server/blob/mysql-8.4.0/sql/aggregate_check.cc)。`Group_check::is_fd_on_source` 从 GROUP BY 列建立集合 $E_1$，迭代加入：

1. 在 $E_n$ 中找到的非空主键 / 唯一键对应的整张表；
2. WHERE 或 INNER JOIN 中的列列等值；
3. 视图、派生表和子查询已经证明的依赖。

`find_fd_in_joined_table` 还会处理 join 条件，但 outer join 的方向决定依赖是否安全。MySQL 官方文档给出一个很有用的反例：`countrylanguage LEFT JOIN country` 时，左表的 `(CountryCode,Language)` 仍决定右侧；把左右表对换后，多个 NULL-complemented 行会合并到同一组，而 `co.Name` 可能不同，因此查询被拒绝。[Functional Dependencies and Outer Joins](https://dev.mysql.com/doc/refman/8.4/en/group-by-functional-dependence.html#group-by-functional-dependence-outer-joins)

MySQL 因此主要把 FD 当作**查询合法性证明**：在 `ONLY_FULL_GROUP_BY` 模式下，SELECT 中未聚合列若由 GROUP BY 列决定，就可以省略。它不等于一定会在执行计划中缩短 hash/sort key，也不能由“被接受”推断已有聚合消除规则。

官方文档还展示了多列主键与跨表等值的组合：

```text
{cl.CountryCode, cl.Language} -> {cl.*}
{cl.CountryCode} -> {co.Code}
{co.Code} -> {co.*}
```

这正是 L07 的传递链在 SQL 语义检查中的直接工程化版本。

## 4. TiDB：显式 FDSet，传播规则细，消费者要单独核对

TiDB v8.5.0 有专门的 [`pkg/planner/funcdep`](https://github.com/pingcap/tidb/tree/v8.5.0/pkg/planner/funcdep) 包。`FDSet` 的边同时记录：

- `from` / `to` 列集合，支持复合决定集；
- `strict`：NULL 决定值按相同处理；
- `equiv`：列间等价；
- `conditionNC`：某些列被 null-reject 后才可见的条件 FD；
- 非空列、常量列、GROUP BY 列和最多一行信息。

### 4.1 闭包不是普通边遍历

[`closureOfStrict`](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/funcdep/fd_graph.go#L65) 只有在 `from` 是当前闭包的子集时才加入 `to`，所以能正确处理 `AC → D`。[`closureOfLax`](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/funcdep/fd_graph.go#L96) 明确只做一层 lax 传播，因为 lax FD 不具备普通传递性。`ReduceCols` 则尝试从输入列集删除可由剩余列推出的列，常用于缩小键。

### 4.2 叶节点与选择节点

[`DataSource.ExtractFD`](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/core/operator/logicalop/logical_datasource.go#L419) 从主键、非空唯一索引和可空唯一索引分别加入 strict 或 lax FD。可空唯一索引只有在决定列被证明非空后，才升级为 strict。

[`LogicalSelection.ExtractFD`](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/core/operator/logicalop/logical_selection.go#L227) 继承子节点 FD，再从谓词提取非空、常量、等价，最后按输出 schema 投影。这个顺序很关键：`WHERE a > 5` 先提供 a 非空，随后才可能把 `a ⇝ ...` 强化为 `a → ...`。

### 4.3 内连接、外连接与条件 FD

[`ExtractFDForInnerJoin`](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/core/operator/logicalop/logical_join.go#L744) 先合并左右属性，再加入连接谓词产生的非空、常量和等价信息。它把跨表 `a=b` 作为等价关系处理，而不是只把它当作过滤选择率。

[`ExtractFDForOuterJoin`](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/core/operator/logicalop/logical_join.go#L787) 调用 `MakeOuterJoin`。在 [fd_graph.go 的 `MakeOuterJoin`](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/funcdep/fd_graph.go#L628) 中，右侧可能被补 NULL 的依赖不会盲目保留；连接等价和常量会放进 `ncEdges`，待上层 null-reject 条件出现后再恢复可见。这个实现直接体现 L09 的“补 NULL 是新的数据构造”。

### 4.4 一个容易误读的边界：FDSet 存在，不等于每条改写都读它

TiDB 的 [aggregation elimination](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/core/rule_aggregation_elimination.go#L50) 在所查版本中检查的是输入 `Schema().Keys`：若 GROUP BY 覆盖 unique key，就尝试把聚合转成 projection；`COUNT`、`SUM`、`AVG` 和 DISTINCT 还有各自的表达式限制。代码并没有在这里直接调用 `FDSet.ClosureOfStrict`。

这条观察很重要：

```text
FD 图 -> 可以证明很多性质
Schema().Keys -> 该规则选择的实现接口
```

两者可能由共同的 schema/谓词事实构建，但读源码时必须追到真正的消费者。TiDB 的 `ExtractFD` 还被用于新的 ONLY_FULL_GROUP_BY 检查：`logical_plan_builder.go` 会检查表达式是否在 GROUP BY、常量或 strict closure 中；这是 FD 图的另一个消费者。

## 5. Apache Calcite：元数据查询把唯一性提供给规则

Calcite 通常不把所有 FD 直接暴露成一个通用 FD 图，而是通过 `RelMetadataQuery` 提供按关系节点查询的性质，例如 `areColumnsUnique` 与 `getUniqueKeys`。

### 5.1 Join 的唯一性组合

[`RelMdUniqueKeys.getUniqueKeys(Join, ...)`](https://github.com/apache/calcite/blob/calcite-1.40.0/core/src/main/java/org/apache/calcite/rel/metadata/RelMdUniqueKeys.java#L210) 先组合左右输入的唯一键；随后分析等值连接列：

- 右侧在 join key 上唯一时，可以保留左侧唯一键（前提是 join 不在左侧产生 NULL）；
- 左侧在 join key 上唯一时，对称处理右侧；
- 连接对侧为常量或最多一行时，还能把连接列从键中移除。

它还提醒一个复杂度问题：枚举左右所有唯一键的组合可能爆炸，所以规则有时改问 `areColumnsUnique`，只回答当前需要的一个问题。

### 5.2 用唯一性消除聚合与连接

`[AggregateRemoveRule](https://github.com/apache/calcite/blob/calcite-1.40.0/core/src/main/java/org/apache/calcite/rel/rules/AggregateRemoveRule.java#L64)` 先要求输入在 group set 上唯一，然后只对可转成单例表达式的聚合进行改写。源码中的 `canFlatten` 只接受 singleton/static aggregate；`SUM0` 等特殊情况会主动退出，防止语义或规则循环问题。

`[ProjectJoinRemoveRule](https://github.com/apache/calcite/blob/calcite-1.40.0/core/src/main/java/org/apache/calcite/rel/rules/ProjectJoinRemoveRule.java#L65)` 在投影不使用被删除一侧列时，检查 join key 的 `areColumnsUnique`。左连接检查右侧唯一，普通连接则按对应一侧处理。它没有仅凭“join 条件是等值”删除连接。

这是一种很值得模仿的接口分层：规则只问“这些列是否唯一”，元数据 provider 决定如何从 scan、project、join、aggregate 递归推导。FD 是底层理论，规则依赖的是足够小、可缓存的证明问题。

## 6. CockroachDB：最接近教材中的完整 FD 属性对象

CockroachDB 的 [pkg/sql/opt/props/func_dep.go](https://github.com/cockroachdb/cockroach/blob/v24.1.0/pkg/sql/opt/props/func_dep.go) 明确把 `FuncDepSet` 定义为“编码 base 或 derived relation 中有用列关系”的属性。它记录 strict/lax FD、等价列、strict/lax key 和 empty key（最多一行）。

源码注释列出直接消费者：

- 消除不必要的 DISTINCT；
- 简化 ORDER BY；
- 删除 `Max1Row`；
- 把 semi-join 映射为 inner join。

这个列表说明 FD 的价值不只在 GROUP BY。尤其 `HasMax1Row` 和空 key 把“FD 闭包覆盖全部列”与“结果真的至多一行”分开，正好对应 L09 中值依赖和出现次数的边界。

CockroachDB 还把 strict/lax 的 NULL 语义写在属性对象的类型中，并用 `ReduceCols` 保持一个较小的候选键，而不是暴力枚举所有候选键。它的设计注释追溯到 Paulley 的 *Exploiting Functional Dependence in Query Optimization*，同时明确指出自己的 lax 定义与论文不同；这提醒我们，跨系统比较时必须先比较定义，再比较函数名。

## 7. 这些实现放在一起看

| 问题 | PostgreSQL | MySQL | TiDB | Calcite | CockroachDB |
| --- | --- | --- | --- | --- | --- |
| FD 的主要表示 | PK/唯一索引 + 局部推导 | Group_check 的递增闭包 | 显式 `FDSet` | 唯一键/列唯一性 metadata | 显式 `FuncDepSet` |
| NULL | PK、NOT NULL、NULLS NOT DISTINCT | outer join 方向与 NULL-complement | strict/lax/条件 FD | `ignoreNulls` 参数 | strict/lax key |
| GROUP BY 合法性 | PK 子集检查 | `ONLY_FULL_GROUP_BY` | strict closure 检查 | 通常由 validator/metadata 组合 | 属性供规则使用 |
| GROUP BY 键缩减 | 有独立 planner pass | 文档重点不是物理缩减 | 聚合消除读 `Schema().Keys` | 由规则/trait 组合 | 由属性服务规则 |
| 连接消除 | 左连接 + 至多一行 | 主要是语义检查 | join FD 传播；规则另查 | ProjectJoinRemoveRule | semi/max-one-row 等 |
| 统计依赖 | 独立 extended statistics | 本节调查未见同类 FD 统计 | 统计与 FD 属性分开 | metadata 通常是确定性质 | cost 属性与逻辑属性分层 |

差异背后的共同原则是：**先保守地证明，再把证明暴露给小规则**。越接近执行结果的等价改写，越不能使用“可能成立”的统计 FD；越接近成本估算，越可以使用带强度和误差的相关性。

## 8. 建议读者复刻的最小属性接口

如果要在自己的 optimizer demo 中实现一版教学级 FD，可从下面的接口开始，而不是一开始就实现所有候选键：

```text
derive(scan)       -> strict/lax keys, not-null, base FDs
derive(filter)     -> child + constants/equalities/not-null
derive(project)    -> remap columns, add deterministic-expression FDs
derive(inner join) -> product + join equalities + key/cardinality checks
derive(left join)  -> preserve outer facts; weaken inner facts; track null condition
derive(aggregate)  -> group key -> every output column; max-one-row per group
project(cols)      -> closure first, then remove invisible columns
is_unique(cols)    -> answer only the rule's current question
has_max_one_row()  -> do not infer from value FD alone
```

每条优化规则都应记录它需要的证明：

```text
remove DISTINCT       needs strict key on projected output
shorten GROUP BY      needs equal partition (usually two-way determination)
remove LEFT JOIN      needs inner uniqueness + no inner columns above
remove INNER JOIN     needs inner uniqueness + match existence
flatten aggregate     needs at-most-one row per group + aggregate-specific semantics
fix selectivity        may use statistical dependency, never for equivalence rewrite
```

这张清单就是把 §6.3 的“dependency graphs”补成可以落到代码审查的检查表。真正实现时，再按目标系统的 NULL、bag、outer join、grouping sets 和约束可信度补充条件。

[上一节：FD、NULL、重复行与算子传播](09-fd-null-duplicates-and-propagation.md) · [返回教材目录](../README.md)
