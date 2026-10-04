# L09 · FD、NULL、重复行与算子传播

原文：§6.2–§6.4，印刷页 208，[PDF 第 229 页](../../Query%20opt.pdf#page=229)。这三节在原书中大多没有写完，先看清哪些是原文：

| 小节 | 原书写了什么 | 本节怎么处理 |
| --- | --- | --- |
| §6.2 含 NULL 的 FD | 一个定义，随后是 `XXX explain why, discuss lax dependencies` | 解释定义，补上“为什么” |
| §6.3 算子上的 FD 推导 | 只有 `XXX dependency graphs` | 给出标明为教学补充的检查表 |
| §6.4 Bibliography | 只有标题 | 不处理 |

> **本节回答的问题**
>
> 1. 列里有 NULL 时，FD 定义中的“相同”怎么比较？
> 2. FD 成立，是否说明没有重复行？
> 3. 经过选择、投影、连接之后，哪些 FD 还能用？
> 4. 优化器把这些事实存在哪里，又用它们证明哪些改写？

2026-10-01 扩写说明：第 1–3 部分解释原书已有定义；从第 4 部分起是**教学补充**，不代表作者已完成 §6.3。开源实现另见 [L09b：从 FD 到开源优化器的实际代码](09b-fd-in-open-source-optimizers.md)。本课较长，可先读第 4–6 部分建立传播模型，再读第 7–9 部分看用途。

## 1. NULL 让普通 `=` 失灵

先回忆第五章的三种比较（L02、L05）：

| 比较 | `NULL` 与 `NULL` | `NULL` 与 `1` | `1` 与 `1` |
| --- | --- | --- | --- |
| SQL 相等 `=` | UNKNOWN | UNKNOWN | TRUE |
| 点等号 $\doteq$ | TRUE | FALSE | TRUE |
| `=⁻`（UNKNOWN 按 FALSE 解释） | FALSE | FALSE | TRUE |

看这张表（教学补充）：

| `A` | `B` |
| --- | --- |
| NULL | 7 |
| NULL | 8 |

直觉上它违反了 `A → B`：两行的 `A` “一样”，`B` 却不同。但如果照搬 L07 的定义，前提是 `NULL = NULL`，结果为 UNKNOWN。“UNKNOWN ⇒ …” 该算成立还是违反？定义没有给出答案。

如果改用 `=⁻`，前提变成 FALSE，蕴含式自动为真，这张表反而被判定为**满足**这种较弱的依赖。它不能用于证明“所有 NULL 组成的一组里，B 也是常量”。但这种语义并非毫无用途：它可以描述可空唯一键在非 NULL 部分的保证，第 4 部分会解释。

## 2. 原书的定义：改用点等号（原文）

原书把定义中的 `=` 换成点等号：

$$
\forall t_1, t_2\;\big([\,t_1 \in R \land t_2 \in R \land t_1.A_1 \doteq t_2.A_1\,] \Rightarrow [\,t_1.A_2 \doteq t_2.A_2\,]\big)
$$

读法和 L07 完全一样，只是“相同”改为：**两个 NULL 算相同，NULL 和非 NULL 值算不同。** 点等号永远只给出 TRUE 或 FALSE，定义不再悬空。

| 数据 | `A → B` | 理由 |
| --- | --- | --- |
| (NULL, 7), (NULL, 7), (1, 9) | 满足 | 两行 NULL 在 `A` 上相同，在 `B` 上也相同 |
| (NULL, 7), (NULL, 8) | 违反 | `A` 上相同，`B` 上不同 |
| (NULL, 7), (1, 8) | 满足 | NULL 与 1 不同，不构成比较对 |

这和第七章集合、bag、分组判断“是否相同”的方式一致：分组时所有 NULL 进同一组。

原书的 `XXX` 还提到 lax dependencies，但没有定义。第 4 部分将明确选取一种工程实现中的定义，不把它追认成原书定义。

### 别把它和等价类混为一谈

L06 为 WHERE 条件收集 `=⁻` 等价类；这里用 $\doteq$ 定义约束。两者都避免了 UNKNOWN，但用途不同：

| 对象 | NULL 与 NULL | 用在哪里 |
| --- | --- | --- |
| SQL `A = B` | UNKNOWN | 写查询条件 |
| `A =⁻ B` | FALSE | WHERE 中的二值化谓词，建立等价类（L06） |
| $A \doteq B$ | TRUE | 定义含 NULL 的 FD |

## 3. FD 成立 ≠ 没有重复行

FD 比较的是**值**，不关心一行出现了几次。看这个 bag：

| `id` | `name` |
| --- | --- |
| 1 | Ada |
| 1 | Ada |
| 2 | Lin |

`id → name` 成立：`id` 相同的行，`name` 都相同。按 L08 的定义，`id` 甚至决定了全部属性，是“超键”。可是 `(1, Ada)` 出现了两次，`id` 并不能唯一标识一行。

所以要分清两种信息：

| 信息 | 回答的问题 | 例子 |
| --- | --- | --- |
| 值层 FD | 左值相同，右值是否相同 | `id → name` |
| 无重复 / 行身份 | 同一组值最多出现几次 | `id` 上的 UNIQUE 约束，或第七章的 TID |

L08 的超键和键只在关系是集合（没有重复行）时，才等于“能唯一标识一行”。进入第七章的 bag 之后，只凭一条 FD 不能证明结果无重复，也不能单凭它删除一个可能放大重复的连接。

## 4. strict / lax：让可空唯一键也能提供信息

本节采用 TiDB `funcdep` 的工程约定：strict FD 就是本书的点等号 FD；lax FD 只对**决定列全部非 NULL 且相同**的两行作出承诺，右侧仍按点等号比较。该实现明确提醒，它与所引文献的另一种 lax 定义不同，因此不能只看名称就混用推理规则。[TiDB 定义说明](https://github.com/pingcap/tidb/blob/v8.5.0/pkg/planner/funcdep/doc.go#L25)

写成公式，$X \rightsquigarrow Y$ 表示：

$$
\forall t_1,t_2\in R:\quad
\big((t_1.X=t_2.X)\ \mathrm{IS\ TRUE}\big)
\Rightarrow t_1.Y\doteq t_2.Y.
$$

这里多列的 `=` 是逐列相等的合取。任一决定列为 NULL，就不会触发前提；**不是**说被决定列必须非 NULL。

例如：

```sql
CREATE TABLE accounts (
  id    INTEGER PRIMARY KEY,
  email VARCHAR(100) UNIQUE,
  name  VARCHAR(100)
);
```

假定此数据库的普通 `UNIQUE` 允许多个 NULL，则 `(1,NULL,'Ada')` 与 `(2,NULL,'Lin')` 可以共存。由这个约束只能推出 `email ⇝ {id,name}`，不能推出 strict `email → name`。`GROUP BY email` 会把这两行合在一起，组内的 name 并不固定。

加入 `WHERE email IS NOT NULL` 后，决定列不再含 NULL，lax FD 可以升级成 strict FD。同时，来自唯一约束的“最多一行”保证也可用于这个过滤结果。这两个结论相关，但仍是两种性质。

要特别区分：

- **strict FD**：相同决定值只能对应一种被决定值；可以有完全相同的重复行。
- **strict key**：按点等号比较，相同键值最多有一次行出现；因此它还蕴含键到全部输出列的 strict FD。
- **lax key**：只对全部非 NULL 的键值保证最多一次；含 NULL 的键值可能重复。

这里的 key 是 SQL bag 上的出现次数保证，与 L08 的集合关系键分开命名。某些系统支持 `UNIQUE NULLS NOT DISTINCT`，那类约束的 NULL 语义又不同，不能把上面的普通 UNIQUE 结论一概套用。

### 为什么 lax 不能直接跑 Armstrong 传递闭包

考虑三列数据：

```text
A   B      C
1   NULL   7
1   NULL   8
```

`A ⇝ B` 成立，甚至 strict `A → B` 也成立；`B ⇝ C` 成立，因为 B 为 NULL，不触发 lax 前提。但 `A ⇝ C` 不成立。中间属性 B 的 NULL 使传递链断掉了。

因此必须分别记录依赖强度、非空属性和等价信息。安全的基本做法是：strict FD 使用 L08 的闭包；lax FD 在决定列被证明非空后升级，再参与 strict 闭包。不能把两种箭头合并成一个普通有向图。

## 5. FD 在优化器中是一种逻辑属性

优化器通常不会为了证明改写，每次扫描真实数据寻找 FD。它从受信任的约束和算子语义得到事实，为**每个中间关系**推导可复用的属性。

下面是教学用的属性集合，不对应某个产品的完整结构体：

```text
RelProps(R)
  output columns       列 ID 与类型
  not-null columns     在 R 的输出中保证非空的列
  equalities/constants 哪些列值相等，哪些列固定
  strict/lax FDs       哪些值能决定哪些值
  keys                 哪些列能限制行出现次数
  cardinality bounds   语义保证的最多行数等
```

```mermaid
flowchart LR
    S[有效约束与表达式语义] --> D[按算子推导逻辑属性]
    D --> P[查询闭包、唯一性与最多行数]
    P --> R[证明改写前提]
    R --> C[对合法候选计划估算成本]
    T[统计信息] --> C
```

**FD 通常不是一个执行算子。** 它帮助删除算子、缩小算子的键、证明某个计划可以使用已有顺序，或者提供行数上界。执行器最后运行的是改写后的 join、aggregate、sort 等算子。

同样一张基表，经过不同子计划，属性可能不同。内连接、外连接和过滤后的列即便源自同一基表，也不能直接共享未经调整的非空与唯一性结论。列 ID、作用域和算子位置都是证明的一部分。

### 优化器需要的是可靠推理，不一定是全部 FD

设已知依赖集合为 $F$。问“X 能不能决定 Y”，通常只需要算一次 $X_F^+$，检查 $Y\subseteq X_F^+$；没有必要物化全部 $F^+$。

对于复合决定集 `AC → D`，必须 A、C **一起**进入闭包才能推出 D。它不是 `A → D` 与 `C → D` 两条边。实作可以使用列位图加带多个输入的边，也可以对某种改写直接检查唯一索引，无须统一成一个巨大依赖图。

还应区分 `A → B` 和“知道 B 是哪个值”。FD 只保证一致性，不保存映射函数。例如知道 `postcode → city`、`postcode='p1'`，只能推出结果中 city 固定；不知道 `'p1'` 对应哪个城市，就不能随意删除 `city='X'` 这个过滤条件。

双向 FD 也不等于列相等：`A → B`、`B → A` 可以描述 `(1,10),(2,20)`。只有更强的 `A = B` 或 NULL 安全相等事实，才允许直接把表达式里的 A 替换为 B。

## 6. 每个算子怎样传播属性

本部分的 FD 默认指 strict FD，重复行采用 bag 语义；表达式确定、无副作用，并使用相容的类型、相等和排序规则。规则是语义上的充分条件，不保证每个产品都实现了它们。

### 6.1 过滤：保留旧事实，增加局部事实

$\sigma_p(R)$ 只删行，输入的 FD 和键仍成立：任何输出反例都必定已经是输入反例。

在所有输出行都必须满足的条件中：

- `A = 7` 增加 `∅ → A`，并证明 A 非空。
- `A IS NULL` 也增加 `∅ → A`，但这个常量是 NULL。
- `A = B` 增加值相等、双向 FD，并证明两列非空。
- `A IS NOT DISTINCT FROM B` 增加 NULL 安全相等和双向 FD，但不证明非空。
- `A > 7` 通常不固定 A，却能证明 A 非空，从而可能升级以 A 为决定列的 lax FD。

`A=7 OR B=8` 不能直接推出 A 固定；`CHECK(A=7)` 也不能被当成 `WHERE A=7`，因为 SQL CHECK 可能接受 UNKNOWN。从约束导出 FD 时必须检查该约束真正禁止了哪些数据。

### 6.2 投影与表达式：值依赖保留，唯一性可能丢失

纯列投影保留所有只涉及输出列的已蕴含依赖。若 `A → B`、`B → C`，投影掉 B 后仍有 `A → C`。实现可以先做必要的闭包推导再裁剪列，而不是机械地删掉所有提到 B 的记录。

但丢掉唯一键可能制造重复：原表 `(id,name)=(1,'Ada'),(2,'Ada')` 中 id 唯一，`SELECT name` 的输出却有两个 Ada。

对于确定的计算列 `Z=f(A,B)`，可加入 `AB → Z`。不能默认 `Z → AB`：`ABS(-1)=ABS(1)`，取绝对值会丢信息；`random()` 更不能由输入列决定。只有另行证明函数在适用域上可逆、且转换和比较语义相容，才能反向推导。

### 6.3 内连接：旧 FD 不怕行复制，旧键却怕

对于 $R\bowtie_p S$，两侧原有的值层 FD 都保留。连接可能复制输入行，但复制不会让同一个 X 对应两个不同 Y；这也解释了为何“保留 FD”和“保留唯一性”不是同一条规则。

若连接条件含 `R.a=S.b`，匹配结果还满足两列相等和双向 FD。由此可以把跨表的依赖串起来。

例如 customers 的 id 是键，orders 每位客户有多条订单：连接后 `c.id → c.name` 仍成立，但 c.id 已不再是连接结果的键。若两侧各有键 $K_R,K_S$，它们的组合可以标识一对输入行；要单独保留 $K_R$ 为输出键，还需证明每条 R 行至多匹配一条 S 行。

### 6.4 外连接：补 NULL 是一次新的数据构造

`R LEFT JOIN S` 保留 R 的原有值层 FD；R 的键是否保留，仍取决于每条 R 行会不会匹配多条 S 行。S 会被补出全 NULL 记录，不能直接继承它的全部 FD，更不能宣称 ON 条件对所有输出行成立。

一个能击穿“右侧 FD 全部保留”的反例：

```text
R(id):       1, 2
S(id,x,y):   (1,NULL,7)
条件：R.id = S.id

输出 R.id  S.id  S.x   S.y
       1     1  NULL    7
       2  NULL  NULL  NULL
```

S 的输入只有一行，所以 `S.x → S.y` 成立；输出的两行在 S.x 上都是 NULL，S.y 却分别为 7 和 NULL，依赖失效。`∅ → S.y` 这样的输入常量依赖也可能失效。

但并非右侧信息全都消失。若 S.id 在输入中是非空主键，则 `S.id → S.*` 可以保留为值层 FD：非 NULL id 的匹配行仍来自同一记录，全 NULL 的 id 则对应全 NULL 的 S 侧值。**S.id 依然不一定是输出键**，因为多条未匹配 R 行都会得到 NULL。

对于仅含 `R.a=S.b` 的普通等值左连接，固定 R.a 后，输出 S.b 要么等于它，要么全部为 NULL，因而可以得到 `R.a → S.b`。若 ON 还含不由 R.a 决定的左侧条件，就不能这样推：

```text
R(a,flag): (1,1), (1,0)
S(b):      (1)
ON R.a=S.b AND R.flag=1
输出 (R.a,S.b): (1,1), (1,NULL)
```

这时 R.a 不能决定 S.b。条件必须完整地参与证明。

若上层还有 `WHERE S.id IS NOT NULL`，补 NULL 行被排除，某些依赖才重新可用。工程上可以重新推导，也可以保存“排除这类补 NULL 行后才成立”的条件依赖。TiDB 的 `ncEdges` 就是后者的实例，详见 L09b。

### 6.5 分组、半连接与集合操作

普通 `GROUP BY G` 每个 G 值只输出一行，所以输出 G 是 strict key，并决定全部聚合结果。NULL 会合成一组。要限定为一个普通分组集；`ROLLUP`、`GROUPING SETS` 会混合多个分组层次，原列上的这个结论不再可以直接套用。

全局聚合 `SELECT COUNT(*) FROM R` 没有分组列，空输入也输出一行 0；普通非空键的分组在空输入上输出零行。后面的聚合消除必须保留这个差别。

半连接和反连接只保留或删除左侧的行出现，因此保留左侧 FD 与键；它们不会像普通连接那样按匹配次数复制左行。

`UNION ALL` 不能简单取两侧 FD 的交集：左边 `(1,'Ada')`、右边 `(1,'Lin')` 各自满足 `id → name`，并起来就不满足。`UNION DISTINCT` 虽去除了完整重复行，也不能修复这条依赖。它只保证**全部输出列一起**是 strict key。相比之下，交和差的输出是相应输入的子集或子 bag，可以保留其已有的值层 FD。

## 7. 一条查询，从扫描属性推到优化机会

假设有以下受保证的 schema；订单可以多条属于同一客户：

```sql
CREATE TABLE customers (
  id INTEGER PRIMARY KEY,
  region VARCHAR(20) NOT NULL
);
CREATE TABLE orders (
  order_id INTEGER PRIMARY KEY,
  customer_id INTEGER NOT NULL REFERENCES customers(id),
  amount INTEGER NOT NULL
);

SELECT c.id, c.region, SUM(o.amount) AS total
FROM customers c
JOIN orders o ON o.customer_id = c.id
WHERE c.region = 'east'
GROUP BY c.id, c.region
ORDER BY c.id, c.region;
```

按逻辑处理顺序看属性，而不是直接猜最终执行计划：

1. **扫描 customers**：知道 `c.id → c.region`，c.id 还是 strict key。
2. **扫描 orders**：知道 `o.order_id → {o.customer_id,o.amount}`，o.order_id 是 strict key。
3. **内连接**：增加 `o.customer_id = c.id`，推出 `o.order_id → c.region`。由于客户主键至多匹配一行，o.order_id 仍是连接结果的键；c.id 因为可能有多笔订单，不再是键。
4. **过滤**：新增 `∅ → c.region`。这是当前中间结果的事实，不是 customers 整表的事实。
5. **聚合**：输入中已有 `c.id → c.region`，因此按 `(c.id,c.region)` 和按 c.id 得到相同组划分；聚合输出又重新具有 c.id 这个键。
6. **排序**：c.region 被前面的 c.id 决定，而且此例中还是常量，作为后续排序键没有作用。

这是证明链，**不表示四个被调查的系统都会自动做完全部步骤**。物理实现还要考虑索引、数据规模、排序和 hash 的成本。

## 8. 优化究竟节省了什么，以及需要哪种证明

### 8.1 缩短排序键：只需要前缀决定后续值

若 `X → Y`，`ORDER BY X,Y,Z` 中 Y 不会进一步打破 X 内部的平局，因此可以缩成 `ORDER BY X,Z`。这里 X 表示排在 Y **之前**的那组表达式；不能据此把 `ORDER BY Y,X` 缩成 Y。

收益可能只是少比较一个宽字符串，也可能是缩短所需顺序后，某个已有索引顺序已经满足要求，从而避免 Sort。后一项还需要索引方向、NULL 位置、collation 等物理条件匹配；FD 本身不保证任何输入已经有序。

也不能把 FD 当单调性：`id → price` 不表示按 id 排好的输入也按 price 有序。

### 8.2 缩短分组键：组划分不变，聚合仍然存在

若 `X → Y`，则 `(X,Y)` 与 X 的分组划分相同：XY 相同当然意味着 X 相同；反过来，X 相同又由 FD 保证 Y 相同。两个方向都证明了，才能保证每个聚合看到同样的输入 bag。

因此第 7 部分可以只以 c.id 作为实际分组键，把 region 作为组内常量保留或在本例重建为 `'east'`。输出列没有因此消失。SQL 前端是否允许直接写 `SELECT c.id,c.region ... GROUP BY c.id` 是另一个问题，L09b 会比较 PostgreSQL 与 MySQL。

收益是减少 hash key / sort key 的宽度、比较成本及某些聚合状态开销。因为每位客户可能有多笔订单，**SUM 仍然必须计算**。

更一般地，把分组列集 G 换成 H，需要在输入上保证二者决定彼此，使组划分相同；不能任取 `G → H` 就换。`id → city` 并不允许把“按 id 分组”换成“按 city 分组”。这也与[边界记录 B08](../boundaries.md#b08--更换分组键不能只保留单向-fd)一致。

### 8.3 消除 DISTINCT：需要出现次数保证

若投影列含输入的 strict key，投影结果不会重复，可以去掉 DISTINCT。只有 `id → name` 不够，第 3 部分的两个 `(1,'Ada')` 已经是反例。

另一个较弱但有用的机会是：已知 `X → Y` 时，`DISTINCT X,Y` 可按 X 判重，并保留对应的 Y。这是**缩短判重键**，不是消除判重。两者的证明需求不同。

普通 nullable UNIQUE 也不足以消除 `SELECT DISTINCT email`：多个 NULL 仍需合并。必须考虑非空过滤，或者键本身是否按 NULLS NOT DISTINCT 保证唯一。

### 8.4 消除聚合：每组至多一行，还要改写聚合表达式

若非空分组列集包含输入 strict key，每个实际存在的组恰有一行。此时普通 `MIN(v)`、`MAX(v)` 可变成 v；`COUNT(*)` 变成 1；`COUNT(v)` 变成 `CASE WHEN v IS NULL THEN 0 ELSE 1 END`。

```sql
-- 假定 id 是输入的 strict key
SELECT id, MIN(v), COUNT(v) FROM t GROUP BY id;

-- 对应的教学改写
SELECT id, v, CASE WHEN v IS NULL THEN 0 ELSE 1 END FROM t;
```

实际系统还要维护输出类型、聚合 FILTER、特殊聚合的语义和 HAVING。`SUM` 等函数的结果类型可能不同于参数类型，不能只删节点不做类型处理。

空输入的全局聚合是直接反例：把 `SELECT COUNT(*) FROM empty_table` 改成逐行投影 `SELECT 1 FROM empty_table`，会从一行 0 变成零行。FD 与最多一行的上界都不消除这个问题。

### 8.5 消除连接：值确定之外，还要保留重数和存在性

先看只使用左侧列的情况：

```sql
SELECT o.order_id
FROM orders o LEFT JOIN customers c ON o.customer_id = c.id;
```

若 c.id 唯一，每条订单要么匹配一条客户，要么得到一条补 NULL 行；投影后总是恰好输出一次订单。因此可删左连接，且**不需要外键来保证存在匹配**。

如果改成内连接，同样的唯一性只证明“至多一次”；没有匹配的订单会被删掉。要消除内连接，还需证明“至少一次”，例如有效的非空外键引用有效客户键，且客户侧没有额外过滤、可见性限制或附加 ON 条件破坏匹配。这是 FD / 唯一性与**包含依赖**的协作。

外键列可空时，普通 `=` 不匹配 NULL；即使删除内连接，也可能必须保留 `customer_id IS NOT NULL`。是否受延迟检查、约束是否已验证，也会影响优化器能否信任这个证明。

若右侧只有 `c.id → c.name` 却允许重复的 `(1,'Ada')`，左连接会复制订单。值没有变，重数变了，因此不能删连接。

### 8.6 半连接转内连接、标量子查询与去相关

`EXISTS` 对一条左行只回答有没有匹配；内连接按匹配次数输出。若右侧在关联列上至多一行，两者才可在仅输出左列时互换，从而给优化器更多 join 实现选择。这里无需证明一定匹配，因为无匹配时二者都会排除左行。

标量子查询必须返回至多一行，零行则产生 NULL。例如固定外层 `o.customer_id` 后，通过客户唯一键查找 region，有语义上的最多一行保证。优化器可以利用这个证明避免多行错误检查，或为去相关构造适当的连接；“估计行数约为 1”不能替代它。

反例仍是完全相同的重复行：即使所有结果列都由空集决定，子查询也可能返回十条相同记录，仍触发多行错误。需要的是唯一键 / cardinality bound，而不只是 FD 闭包。

分组下推到连接之前也会利用这些性质，但还需证明连接重数、聚合可分解性、NULL 和空组处理。FD 不是把任意 `SUM`、`AVG` 穿过 join 的通行证；这部分留给第七章的 grouping 与 join 专题。

## 9. 统计 FD：影响选哪个计划，不提供等价改写许可

设一张地址表有十万行，其中符合 `postcode='p1'` 的有 100 行，符合 `city='C'` 的有一万行，并且那 100 行全部属于 C。独立性估算会得到：

$$
100000\times\frac{100}{100000}\times\frac{10000}{100000}=10.
$$

实际是 100 行，因为第二个条件没有进一步筛掉那一百行。这个**自拟算例**说明：关联列的条件不能总是把选择率简单相乘。

PostgreSQL 的 `CREATE STATISTICS ... (dependencies)` 采集带强度的依赖统计，用于修正特定谓词组合的选择率。它与用于证明改写的主键/唯一性事实处于不同层次；即使采样给出强度 1，也不能作为删除过滤或连接的语义保证。[PostgreSQL 18 统计说明](https://www.postgresql.org/docs/18/planner-stats.html#PLANNER-STATS-EXTENDED-FUNCTIONAL-DEPS)

统计过时或近似只应造成计划选得不好，不能使结果出错。FD 能推出组内一致性，也不代表优化器知道常量之间的对应关系；`postcode='p1' AND city='错误城市'` 不能在没有额外证据时改成单个条件。

## 10. 用四个检查问题收束本课

读到任何“基于 FD 的优化”时，先回答：

1. **事实来源**：这是受保证的约束、当前算子的结论，还是采样统计？
2. **作用位置**：这个 FD 在哪一个中间关系上成立？经过补 NULL、投影或 UNION 后是否还成立？
3. **需要的性质**：只需值一致，还是还需唯一性、匹配存在、最多一行、确定表达式？
4. **实现证据**：找到了数据结构，还是已追踪到实际消费它的规则和启用条件？

**自测一**：`T(A,B)` 是 `(NULL,1),(NULL,1),(2,3)`，`A → B` 成立吗？A 是 strict key 吗？

**自测二**：客户连接订单后仍有 `customer_id → customer_name`，为什么可以缩短分组键，却不一定能删除 SUM？

**自测三**：过滤让某列成为常量，是否说明能把它从所有 GROUP BY 中删除？

<details>
<summary>答案与边界</summary>

一：FD 成立，A 不是 strict key，因为第一行出现两次。

二：FD 保证同一客户组内名称相同，不保证只有一笔订单；组划分可以不变，但聚合仍要处理多条行出现。

三：不能不加检查地删除最后一个分组键。对空输入，按一个常量分组输出零组，去掉所有分组键后的全局聚合却可能输出一行。实现需要特殊处理这个语义差异。

</details>

继续读 [L09b：从 FD 到开源优化器的实际代码](09b-fd-in-open-source-optimizers.md)，检查这些证明如何落到真实工程。之后回到第七章 [Set、Bag、Sequence](10-sets-and-characteristic-functions.md)，统一建立结果对象与重数的形式化定义。

[上一节：如何从已有 FD 推出新 FD](08-armstrong-closure-and-keys.md) · [返回教材目录](../README.md) · [查看教学提纲](../outline.md)
