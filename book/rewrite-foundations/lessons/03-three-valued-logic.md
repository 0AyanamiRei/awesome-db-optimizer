# L03 · 三个真值与两种解释上下文

原文：§5.3 Three-Valued Logic，印刷页 202，[PDF 第 223 页](../../Query%20opt.pdf#page=223)，重点是图 5.4、UNKNOWN 的两种解释及三值等价的定义。订单例子与逐步求值是教学展开。正文使用直接显示的数学符号，无需 LaTeX 插件；其中 `⊥` 表示 UNKNOWN，`¬`、`∧`、`∨` 分别表示 NOT、AND、OR。

上一节我们知道，当订单金额为 NULL 时，`amount > 100` 的结果是 UNKNOWN。现在把它放进一个稍大的条件：

```sql
amount > 100 AND status = 'paid'
```

假设这条订单已经付款，只是金额尚未填写。那么右边为 TRUE，左边为 UNKNOWN，整个条件应该得到什么？

如果金额后来确认为 150，条件会成立；如果确认为 80，条件就不成立。已有信息不足以决定结果。因此，本书采用的三值逻辑规定：

> UNKNOWN ∧ TRUE → UNKNOWN

这里的箭头表示“求值得到”。但如果这条订单确定没有付款，右边就是 FALSE。无论金额比较的结果如何，“金额超过 100 且已付款”都不可能成立，于是：

> UNKNOWN ∧ FALSE → FALSE

这两个例子说明，**逻辑运算遇到 UNKNOWN，并不总是返回 UNKNOWN。** 要看另一个输入是否已经足够确定整个条件。

先补上最简单的 NOT。否定会交换 TRUE 与 FALSE；原先无法确定的条件，取否定之后仍无法确定。


| p       | ¬p      |
| ------- | ------- |
| TRUE    | FALSE   |
| FALSE   | TRUE    |
| UNKNOWN | UNKNOWN |


所以，`NOT (amount > 100)` 并不会接受一个“未知金额其实不大于 100”的结论。金额为 NULL 时，内部比较得到 UNKNOWN，外层 NOT 仍得到 UNKNOWN。

再看 AND 与 OR。原书图 5.4 将它们列成两个矩阵；这里按输入组合逐行展开，便于比较：


| p       | q       | p ∧ q   | p ∨ q   |
| ------- | ------- | ------- | ------- |
| TRUE    | TRUE    | TRUE    | TRUE    |
| TRUE    | FALSE   | FALSE   | TRUE    |
| TRUE    | UNKNOWN | UNKNOWN | TRUE    |
| FALSE   | TRUE    | FALSE   | TRUE    |
| FALSE   | FALSE   | FALSE   | FALSE   |
| FALSE   | UNKNOWN | FALSE   | UNKNOWN |
| UNKNOWN | TRUE    | UNKNOWN | TRUE    |
| UNKNOWN | FALSE   | FALSE   | UNKNOWN |
| UNKNOWN | UNKNOWN | UNKNOWN | UNKNOWN |


理解这张表可以抓住两个决定性输入。对于 AND，一个 FALSE 就足以让整体为 FALSE；没有 FALSE 时，只有两个 TRUE 才能得到 TRUE，其余情况为 UNKNOWN。对于 OR，一个 TRUE 就足以让整体为 TRUE；没有 TRUE 时，只有两个 FALSE 才能得到 FALSE，其余情况为 UNKNOWN。

例如把开头的 AND 换成 OR，对于“金额未知，但确定已付款”的订单，整个条件就是 TRUE：虽然金额这一边不确定，“至少满足一个条件”已经有了确定的依据。

上面的说法用于解释真值，不规定数据库必须按什么顺序计算表达式。这里仍然讨论确定、无副作用的逻辑条件。

现在可以回答第一节留下的问题。令 `p` 代表 `amount > 100`，考察：

```sql
WHERE amount > 100 OR NOT (amount > 100)
```

当金额为 NULL 时，逐步求值如下：

> p → UNKNOWN；¬p → UNKNOWN；p ∨ ¬p → UNKNOWN

最后的结果不是 TRUE。因此，二值逻辑中的排中律 `p ∨ ¬p ≡ TRUE` 在这里不成立。类似地，当 `p` 为 UNKNOWN 时，`p ∧ ¬p` 也为 UNKNOWN，不能无条件写成 FALSE。

并不是所有熟悉的规律都因此失效。例如吸收律 `p ∨ (p ∧ q) ≡ p` 在本书的三值逻辑中仍成立。若 `p` 为 TRUE 或 FALSE，上一节的分类证明仍适用；若 `p` 为 UNKNOWN，分别令 `q` 为 TRUE、FALSE、UNKNOWN，内部 AND 得到 UNKNOWN、FALSE、UNKNOWN，再与外面的 UNKNOWN 做 OR，三次都得到 UNKNOWN。新增情况也与 `p` 相同，所以这条规律保留下来了。

图 5.4 后，原文还用已有运算定义了蕴含与异或：`a ⇒ b` 定义为 `¬a ∨ b`；异或定义为 `(a ∨ b) ∧ ¬(a ∧ b)`。这里“定义为”表示给已有表达式一个名字，不需要另设一套求值规则。原书的异或符号是带点的 OR；本节用文字称呼它，避免依赖组合字形。

到目前为止，我们一直在计算**条件的真值**。数据库接下来还要回答另一个问题：这个真值能否让一条记录被保留，或者让一次写入通过约束？

看同一个条件 `amount > 100`，分别用在 WHERE 和 CHECK 中。

```sql
SELECT order_id, amount
FROM orders
WHERE amount > 100;
```

WHERE 只保留条件结果为 TRUE 的记录。FALSE 和 UNKNOWN 都不会让记录通过。因此，刚才的 `p OR NOT p` 虽然对所有非 NULL 金额为 TRUE，却会过滤掉 NULL 金额的记录；直接删掉 WHERE 条件则会保留这些记录。两者结果不同。

CHECK 的规则不同。考虑下面这张没有其他约束的示例表：

```sql
CREATE TABLE checked_orders (
    order_id INTEGER,
    amount INTEGER CHECK (amount > 100)
);
```

这条 CHECK 只在条件得到 FALSE 时判定违反约束；TRUE 和 UNKNOWN 都不会违反它。于是，同一个比较会产生下面的行为：


| amount | amount > 100 | WHERE 是否保留 | 是否通过这条 CHECK |
| ------ | ------------ | ---------- | ------------ |
| 150    | TRUE         | 是          | 是            |
| 80     | FALSE        | 否          | 否            |
| NULL   | UNKNOWN      | 否          | 是            |


表中的“通过”仅指这条 CHECK。它没有证明未知金额一定大于 100，只表示目前没有得到违反条件的 FALSE。如果业务同时要求金额必须填写，需要另外声明 `NOT NULL`；单独的 `CHECK (amount > 100)` 没有承担这个要求。

因此，不能笼统地说“SQL 把 UNKNOWN 当作 FALSE”。WHERE 与 CHECK 对它的接受方式不同，而且这两者都没有改变前面那张三值真值表。

原书为这两种接受方式引入了显式的转换记号：`⌊p⌋⊥` 与 `⌈p⌉⊥`。原书把 `⊥` 印为下标，这里紧接在右括号之后显示。底部带横线的括号 `⌊…⌋` 将 UNKNOWN 转成 FALSE，顶部带横线的括号 `⌈…⌉` 将 UNKNOWN 转成 TRUE；原有的 TRUE 和 FALSE 都保持不变。这不是数值的取整运算。


| p       | ⌊p⌋⊥：将未知解释为假 | ⌈p⌉⊥：将未知解释为真 |
| ------- | ------------ | ------------ |
| TRUE    | TRUE         | TRUE         |
| FALSE   | FALSE        | FALSE        |
| UNKNOWN | FALSE        | TRUE         |


WHERE 的接受行为可以用 `⌊p⌋⊥` 表示：转换后为 TRUE 才保留。CHECK 的通过行为可以用 `⌈p⌉⊥` 表示：转换后为 TRUE 就不违反这条约束。两种转换都把三个可能的真值变成两个，但保留的是不同的接受规则。

要注意转换所在的位置。`⌊¬p⌋⊥` 表示先在三值逻辑中计算 `¬p`，再解释最终结果；它并不等于“先把 `p` 的 UNKNOWN 换成 FALSE，再取否定”。取 `p` 为 UNKNOWN，前者得到 FALSE，后者却得到 TRUE。下一节会专门推导解释操作怎样穿过 NOT，不能直接把转换搬进表达式内部。

这里也出现了两种需要区分的“相同”。原书说两个三值表达式等价，要求对所有允许的输入赋值，原始结果完全一样：TRUE 对 TRUE、FALSE 对 FALSE、UNKNOWN 对 UNKNOWN。**仅仅在 WHERE 中保留相同的记录，还不足以证明这种等价。**

例如比较 `p ∧ ¬p` 与常量 FALSE。前者在 `p` 为 TRUE、FALSE、UNKNOWN 时，分别得到 FALSE、FALSE、UNKNOWN；后者始终为 FALSE。它们不满足三值等价，但 WHERE 对这两者都不接受，所以过滤行为相同。换到 CHECK，差别就显现出来：`p` 为 UNKNOWN 时，前者通过，常量 FALSE 却拒绝。

可以把同一条记录的计算过程分成两个阶段来读：

> 字段值 → 按比较和逻辑规则求出真值 → 按 WHERE 或 CHECK 的规则决定是否接受

这样，NULL 如何进入比较、UNKNOWN 如何参与运算、最终记录是否通过，就有了各自清楚的位置。后面看到一个“等价变换”时，也能先检查它承诺的是原始真值相同，还是某个上下文中的接受行为相同。

如果愿意，可以检查这个条件：`NOT (amount > 100) OR amount IS NULL`。金额为 NULL 时，左侧为 UNKNOWN，右侧为 TRUE，整体为 TRUE；它在 WHERE 中会保留这条记录。这个例子再次说明，UNKNOWN 不会无条件传遍整个表达式。

下一节是 **L04：否定为什么会交换 UNKNOWN 的解释**，继续阅读 §5.3 的 pp.203–204。

[上一节：NULL 与比较](02-null-and-comparisons.md) · [返回教材目录](../README.md) · [查看教学提纲](../outline.md)
