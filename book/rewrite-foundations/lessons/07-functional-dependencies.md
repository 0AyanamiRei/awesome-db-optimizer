# L07 · X → Y 究竟约束什么

原文：第 6 章引言与 §6.1，印刷页 207，[PDF 第 228 页](../../Query%20opt.pdf#page=228)。本节建立函数依赖的对象、量化范围和优化用途。订单、客户和国家的例子是教学展开；函数依赖定义来自原书。

第五章回答了“一个谓词在什么语义下求值”。现在换一个问题：如果结果中的两个属性不是独立的，优化器能否利用这种必然关系？函数依赖（functional dependency，FD）就是描述这种关系的语言。

## 从一个排序例子开始

原书用下面的查询说明 FD 的用途：

```sql
SELECT c.id, n.name
FROM customers AS c, nations AS n
WHERE c.nid = n.id
ORDER BY c.id, n.name;
```

假设 `customers.id` 是客户表的键，`nations.id` 是国家表的键。于是：

1. `c.id` 决定 `c.nid`；
2. 连接条件把 `c.nid` 与 `n.id` 配对；
3. `n.id` 决定 `n.name`；
4. 因此 `c.id` 决定 `n.name`。

如果排序已经按 `c.id` 排好，那么在这些前提下再按 `n.name` 排序不会改变顺序。优化器可以把 `ORDER BY c.id, n.name` 简化为 `ORDER BY c.id`。

这个例子依赖的是“相同的 `c.id` 不会对应两个不同的 `n.name`”，而不是某一次数据恰好如此。FD 是对关系实例中所有相关记录的约束。

## 形式化定义

设关系 `R` 的属性集合为 `A(R)`，`A₁` 和 `A₂` 是其中的两个属性子集。函数依赖：

> `A₁ → A₂`

表示：只要关系中两条记录在 `A₁` 上相同，它们就在 `A₂` 上相同。原书的定义是：

> `∀t₁,t₂ ((t₁∈R ∧ t₂∈R ∧ t₁.A₁ = t₂.A₁) ⇒ (t₁.A₂ = t₂.A₂))`

其中 `t.A₁` 表示 tuple `t` 限制到属性集合 `A₁` 后的值。把它读成程序条件就是：

```text
for every pair (t1, t2) in R:
    if t1[A1] == t2[A1]:
        require t1[A2] == t2[A2]
```

这里的 `∀` 很重要。检查几行数据只能说明这些行没有违反 FD，不能证明对所有合法输入都成立；FD 通常来自 schema 的键约束、检查条件或已经证明的变换。

## 一个具体例子

考虑下面的关系：

| `customer_id` | `nation_id` | `nation_name` |
| --- | --- | --- |
| 10 | 1 | China |
| 11 | 1 | China |
| 12 | 2 | France |

在这份结果中，`customer_id → nation_id` 和 `customer_id → nation_name` 都成立。由于 `nation_id` 为国家键，`nation_id → nation_name` 也成立；由此可以推出 `customer_id → nation_name`。

但 `nation_id → customer_id` 不成立：`nation_id=1` 对应了客户 10 和 11。FD 的箭头表示“左侧值相同会强制右侧值相同”，不是“两个属性可以互相查找”。

## FD 与键的关系

设 `A(R)` 是关系的全部属性。

- 如果 `X → A(R)`，`X` 是一个**超键**：`X` 的值足以决定整条记录的所有属性。
- 如果 `X` 是超键，并且 `X` 的任何真子集都不是超键，`X` 是一个**键**。

键的“最小”是属性集合意义上的最小，不是数值最小，也不是只要找到一个超键就停止。一个关系可以有多个候选键；每个候选键都满足最小性。

例如，设关系 `R(A,B,C,D)` 有：

> `A → B`
>
> `B → C`
>
> `AC → D`

那么 `A` 可以决定 `B` 和 `C`，但不能从已知条件推出 `D`。而 `AC` 可以决定 `D`，再结合 `A` 已决定的 `B,C`，得到 `AC → ABCD`。如果 `A` 和 `C` 各自都不能决定全部属性，`AC` 就是一个候选键。

下一节会说明这些“从已有 FD 得到新 FD”的步骤如何系统化，而不是靠每次重新读例子猜测。

[下一节：Armstrong 公理、闭包与键](08-armstrong-closure-and-keys.md) · [返回教材目录](../README.md) · [查看教学提纲](../outline.md)
