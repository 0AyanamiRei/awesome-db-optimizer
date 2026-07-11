# Holistic Query Unnesting 与 Indexed Algebra 详解设计

## 目标

在 `unnset/docs` 下新增一篇独立中文 HTML，帮助已经理解 2015 bottom-up unnesting 缺陷、但无法把 Indexed Algebra 与 2025 top-down 算法连接起来的读者，逐步看懂：

1. Indexed Algebra 索引的是查询计划及其数据流，而不是表数据；
2. operator、expression、IU、source、consumer 和 LCA 分别是什么；
3. 逐个列引用怎样得到 `accessing(dependent_join)`；
4. `accessing` 怎样成为 Holistic Query Unnesting 的路线图；
5. `outerRefs`、`cclasses`、`repr` 和绑定域 `D` 怎样在一次 top-down trace 中变化；
6. 为什么新算法避免 2015 方法在深层嵌套中制造无效绑定组合。

最终页面必须可以脱离本文档阅读，并与现有入门教程双向链接。

## 准确性基线

页面以以下一手材料为事实来源：

- Thomas Neumann, *Improving Unnesting of Complex Queries*, BTW 2025，本地文件 `unnset/paper/UnnestQuery-btw2025.pdf`。
- Philipp Fent, Guido Moerkotte, Thomas Neumann, *Asymptotically Better Query Optimization Using Indexed Algebra*, PVLDB 16(11), 2023，官方 PDF `https://www.vldb.org/pvldb/vol16/p3018-fent.pdf`。
- Thomas Neumann, *A Formalization of Top-Down Unnesting*, 2024，`https://arxiv.org/abs/2412.04294`，只用于语义边界和正确性背景。
- Thomas Neumann, Alfons Kemper, *Unnesting Arbitrary Queries*, BTW 2015，本地文件 `unnset/paper/UnnestQuery-btw2015.pdf`。

必须明确：

- 2025 论文题名是 *Improving Unnesting of Complex Queries*；“Holistic Query Unnesting”是第 3 节标题。
- Indexed Algebra 是动态辅助树索引，不是 B-tree、tuple index、provenance algebra 或 unnesting rewrite 本身。
- 2025 算法使用 Indexed Algebra 高效求 LCA 和检查路径；即使没有该索引，也能用较慢的计划遍历/列集合得到同样的标注。
- 单次 LCA 是摊还 `O(log n)`；若有 `m` 个列引用，识别阶段应描述为 `O(m log n)`，不能声称整阶段只有 `O(log n)`。
- 2015 的深层嵌套问题是性能退化而非结果错误。
- `accessing` 只定位真实外层引用，不负责构造 `D` 或证明改写等价。
- 课程幻灯片中“若 `o1 != o2`”不是严谨判定；论文条件是 `LCA(o_access, o_source) != o_access`。

## 读者与范围

目标读者已经能解释相关子查询、dependent join、自由变量和 2015 的绑定域 `D`，但不要求了解动态树或 splay tree。

正文讲到以下深度：

- 给出 operator/expression/IU 的关系；
- 把 Indexed Algebra 当作可回答 `source`、`consumer`、LCA 和路径属性查询的黑盒；
- 解释 LCA 为什么能区分正常向上数据流和跨分支相关引用；
- 完整推演 `accessing` 与 top-down unnesting 状态。

正文不展开：

- link/cut tree 的旋转、preferred path、`expose` 实现；
- 2023 论文的完整优化应用，例如 join graph、nullability path aggregate、基数估计；
- 形式化证明细节；
- C++ 优化器实现或跨系统工程比较。

link/cut tree 只在一个“到这里就够了”的说明框中出现：它让动态计划上的 LCA/路径查询保持摊还 `O(log n)`，随后立即回到 unnesting 主线。

## 教学策略

采用“一个主例贯穿 + 两个短边界例子”，不按论文目录逐节复述。

### 主例

使用原创的 `department`、`employee`、`expense` 三表查询：

```sql
SELECT d.dept_id
FROM department AS d
WHERE d.region = 'EAST'
  AND (
    SELECT COUNT(*)
    FROM employee AS e
    WHERE e.dept_id = d.dept_id
      AND (
        SELECT SUM(x.amount)
        FROM expense AS x
        WHERE x.emp_id = e.emp_id
          AND x.fiscal_year = d.fiscal_year
      ) > 10000
  ) >= 3;
```

这个例子同时包含：

- `employee` 对 `department` 的一层相关引用；
- `expense` 对 `employee` 的相邻层引用；
- `expense` 对 `department` 的跨两层引用；
- 两个 dependent join；
- 两层 group-by；
- 可以通过等值类 substitution，也可以显式加入绑定域 `D` 的位置。

主例中的 `dept_id`、`emp_id`、`fiscal_year` 明确假设为 `NOT NULL`，使第一次阅读不被 NULL 语义打断。NULL-safe 回接在结尾单独恢复。

### 绑定域放大例

用小关系给出真实联合绑定与独立域笛卡尔积：

```text
T1(a)       = {1, 2}
T2(a,b)     = {(1,x), (2,y)}
真实绑定 P  = {(1,x), (2,y)}
Da x Db     = {(1,x), (1,y), (2,x), (2,y)}
```

逐行展示无效组合 `(1,y)`、`(2,x)` 会在后续 join 被过滤，所以结果正确，但 group-by 已做额外工作。页面再用符号说明嵌套增加时独立域大小会连乘。

### 本地引用反例

同一个 selection 中，`expense.amount` 的 source 位于 selection 子树内，`LCA(source, consumer) = consumer`，因此不是相关引用。它和跨分支的 `d.fiscal_year` 并排展示，防止读者误以为“所有列访问都加入最近的 dependent join”。

## 页面结构

新文件为 `unnset/docs/holistic-query-unnesting-indexed-algebra.html`，包含以下章节：

1. **先拆开两个概念**：Indexed Algebra 是分析基础设施，Holistic Unnesting 是改写算法。
2. **主例与三层作用域**：表、样例数据、SQL、规范代数计划和两层 dependent join。
3. **Indexed Algebra 的最小心智模型**：operator、expression、IU、source、consumer，以及计划边和 IU 引用边的区别。
4. **LCA 怎样发现相关引用**：本地引用、相邻层引用、跨两层引用逐个计算。
5. **从列引用得到 `accessing`**：完整表格列出 access、source、LCA、动作，得到两个集合。
6. **2015 为什么会多算**：使用具体绑定数据比较真实联合域和独立域乘积。
7. **Holistic trace**：从外层 dependent join 开始，逐步记录 `accessing`、`outerRefs`、`cclasses`、`repr` 和 parent state。
8. **访问列表清空后怎么办**：并排解释显式加入 `D` 与 substitution；强调这是成本选择。
9. **最终计划**：把 SQL 主例改写成普通 join/group-by 形状，并逐项对应原 SQL 语义。
10. **正确性与边界**：NULL-safe `IS NOT DISTINCT FROM`、duplicate-free `D`、静态聚合空输入、`O(m log n)` 复杂度和 Indexed Algebra 非必需性。
11. **带着问题回论文**：按准确页码列出 2023 §2–§4.2、2025 §2.3/§3.1–§3.3 和形式化报告。

页面不重复现有教程的 2015 历史、所有 SQL 构造或 PPT 导读。

## 图示设计

> **当前阶段调整（2026-07-11）：** 用户要求暂时不处理画图问题。本节保留为未来阶段设计，但不属于当前 HTML 交付范围。当前页面不得依赖 TikZ/SVG；所有计划、LCA、状态迁移和绑定域组合均使用可搜索的 HTML 表格与文本代数树表达。实验性渲染脚手架已从本阶段最终变更移除。

所有新图均为原创 TikZ 图，使用新的业务例子、节点名、配色和版面；只借鉴论文的白底、细线、数学字体和稀疏代数树风格。不得描摹原论文 Figure 1/2/6，也不得复用其 `T1/T2/T3` 构图。

LaTeX 源文件放入 `unnset/docs/figures`，生成的 SVG 放入 `unnset/docs/assets`。图注在 HTML 中写明“概念依据”及论文小节，不声称是论文图重绘。

六幅图：

1. `holistic-01-scope-plan.tex/svg`：SQL 三层作用域与规范 dependent-join 计划。
2. `holistic-02-lca-accessing.tex/svg`：三条跨分支 IU 引用弧、LCA 落点和两个 `accessing` 集合。
3. `holistic-03-binding-blowup.tex/svg`：真实绑定两行与 2x2 独立域组合矩阵。
4. `holistic-04-top-down-trace.tex/svg`：四个 top-down 状态检查点及父子状态合并。
5. `holistic-05-domain-or-repr.tex/svg`：显式 `D` 与 substitution 两个等价计划形状。
6. `holistic-06-final-plan.tex/svg`：全部 dependent join 消失后的普通 join/group-by 计划。

每幅图必须有：

- 描述性 `alt`；
- 解释图中应该观察什么的 `figcaption`；
- 可独立编译的 `.tex`；
- 不依赖浏览器脚本的 SVG 输出。

## LaTeX 生成路线（未来阶段）

使用本机已有 TinyTeX、`latexmk`、TikZ 与 PyMuPDF，不安装额外系统包：

1. 每幅 `.tex` 使用 `article + geometry + tikz` 固定页面尺寸，不依赖缺失的 `standalone.cls`。
2. `latexmk -pdf -halt-on-error` 生成单页 PDF。
3. 一个小型 `unnset/docs/figures/render.py` 批量调用 LaTeX，并用 `fitz.Page.get_svg_image(text_as_path=True)` 生成 SVG。
4. 中间 PDF、aux、log 输出到 `unnset/docs/figures/build`，不纳入版本控制。

共享节点颜色和 TikZ 样式放入 `unnset/docs/figures/holistic-style.tex`，图文件只保留各自内容。

## 页面视觉与导航

新页面沿用现有教程的静态单文件视觉语言：颜色 token、hero、资料卡、sticky 胶囊导航、section、callout、表格和响应式双栏。新页面不引入 MathJax、KaTeX、Mermaid 或其他运行时依赖；公式继续使用 HTML、Unicode 和 `<sub>`。

导航改动：

- 现有 `nested-sql-unnesting-tutorial.html` 的“2025 改进”后加入跨页“2025 深入”。
- 现有 4.5 Indexed Algebra 段落后加入新页的展开阅读链接。
- 新页 hero 和 footer 反链 `nested-sql-unnesting-tutorial.html#paper-2025`。
- 顺手把现有 hero 中裸 `&` 改为 `&amp;`，并把小写 doctype 规范为 HTML Validate 接受的 `<!DOCTYPE html>`，不做其他无关重写。

## 可访问性与失败处理

- 当前阶段不嵌入新图；计划结构和状态迁移必须完全由正文、表格和文本树表达。
- 每个表格提供明确表头；颜色之外再使用线型、标签或符号区分状态。
- sticky 导航在小屏横向滚动，六幅大图在 860px 以下单列显示。
- 外部论文链接只作延伸阅读；核心内容和本地 2015/2025 PDF 链接可以离线访问。
- LaTeX 编译失败应由 `render.py` 以非零状态退出，并保留对应 log 路径。

## 验证范围

当前阶段只修改 HTML 与设计/计划文档，不继续修改 TikZ、SVG 或 Python 渲染辅助脚本，也不触及 C++ 或可执行 demo，因此只运行 HTML 文档级验证：

- HTML5 validation；
- 所有本地 `src`/`href` 资源存在；
- 页面内 fragment 对应现有 `id`；
- 无重复 ID；
- 所有 `<img>` 有非空 `alt`；
- `git diff --check`；
- 浏览器/截图人工抽查桌面和窄屏布局。

不运行 `demos/demo` C++ 构建或测试。

## 完成标准

- 新页面可以让读者不先读 2023 Indexed Algebra 全文，也能准确推导 2025 Figure 2 式 `accessing` 标注。
- 主例从 SQL、数据、计划、LCA、状态 trace 一直推到无 dependent join 的最终计划，中间没有跳步。
- 至少两个例子包含具体输入关系和中间/最终关系，而不只是符号树。
- 即使没有新图，所有例子仍可通过具体关系数据、HTML 表格和文本代数树完整推演。
- 页面准确区分 Indexed Algebra、Holistic Unnesting 和成本选择。
- 新旧页面双向可达，所有文档级检查通过。
