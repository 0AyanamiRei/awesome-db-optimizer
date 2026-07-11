# Holistic Query Unnesting 与 Indexed Algebra 详解实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** 新增一篇以具体数据和原创 TikZ 图逐步解释 Indexed Algebra 如何支撑 2025 Holistic Query Unnesting 的中文 HTML，并与现有教程双向接入。

**Architecture:** 保持 unnset/docs 现有的纯静态 HTML 形态。六幅图以独立 TikZ 源维护，由 Python 调用 TinyTeX 编译为 PDF，再用 PyMuPDF 生成 SVG；正文以一个 department/employee/expense 主例贯穿，并用 HTML 表格保留所有关键事实的文本等价物。

**Tech Stack:** HTML5、内联 CSS、LaTeX/TikZ、TinyTeX latexmk、Python 3、PyMuPDF、HTML Validate。

---

## 文件结构

- Create: unnset/docs/holistic-query-unnesting-indexed-algebra.html — 独立中文详解页面。
- Modify: unnset/docs/nested-sql-unnesting-tutorial.html — 增加双向入口并修复裸 &。
- Create: unnset/docs/figures/.gitignore — 排除 LaTeX 中间产物。
- Create: unnset/docs/figures/holistic-style.tex — 六图共享 TikZ 样式。
- Create: unnset/docs/figures/render.py — 批量编译 LaTeX 并生成 SVG。
- Create: unnset/docs/figures/holistic-01-scope-plan.tex through holistic-06-final-plan.tex — 六幅图源。
- Create: unnset/docs/assets/holistic-01-scope-plan.svg through holistic-06-final-plan.svg — 六幅生成图。

## Task 1: 建立并验证 TikZ 图生成链

**Files:**

- Create: unnset/docs/figures/.gitignore
- Create: unnset/docs/figures/holistic-style.tex
- Create: unnset/docs/figures/render.py

- [ ] **Step 1: 记录缺失生成链的基线失败**

Run:

    test -f unnset/docs/figures/render.py &&
      python -m py_compile unnset/docs/figures/render.py

Expected: FAIL，因为 render.py 尚不存在。

- [ ] **Step 2: 添加中间产物排除规则**

.gitignore 的完整内容：

    /build/

- [ ] **Step 3: 添加共享 TikZ 样式**

holistic-style.tex 必须定义五个 HTML 颜色 176B87、8A5A12、4F6F52、A94442、5F6368，以及 op、scan、djoin、state、planedge、iuedge、muted 七个样式。djoin 使用红色粗边；IU 边使用虚线；颜色之外必须保留线型或文本标签。

- [ ] **Step 4: 实现批量渲染器**

render.py 必须满足：

- 只匹配 holistic-[0-9][0-9]-*.tex；
- 为每图运行 latexmk -pdf -halt-on-error -interaction=nonstopmode；
- 把中间文件写到 figures/build/<stem>/；
- 检查生成的 PDF 恰好一页；
- 使用 page.get_svg_image(text_as_path=True)；
- 原子写入 unnset/docs/assets/<stem>.svg；
- 任一子进程或 PDF 检查失败时返回非零状态，并给出图名与 build 目录；
- 接受可选图名参数，未传参数时渲染全部图。

完整实现骨架如下；实现任务可补充更清晰的异常消息，但不得改变路径和输出约定：

    from __future__ import annotations

    import os
    import subprocess
    import sys
    from pathlib import Path

    import fitz

    FIGURES_DIR = Path(__file__).resolve().parent
    ASSETS_DIR = FIGURES_DIR.parent / "assets"
    BUILD_ROOT = FIGURES_DIR / "build"
    FIGURE_GLOB = "holistic-[0-9][0-9]-*.tex"

    def render(tex_path: Path, assets_dir: Path, build_root: Path) -> Path:
        build_dir = build_root / tex_path.stem
        build_dir.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "latexmk",
                "-pdf",
                "-halt-on-error",
                "-interaction=nonstopmode",
                f"-outdir={build_dir}",
                tex_path.name,
            ],
            cwd=FIGURES_DIR,
            check=True,
        )
        pdf_path = build_dir / f"{tex_path.stem}.pdf"
        with fitz.open(pdf_path) as document:
            if document.page_count != 1:
                raise RuntimeError(
                    f"{tex_path.name}: expected one page, got {document.page_count}; "
                    f"build directory: {build_dir}"
                )
            svg = document[0].get_svg_image(text_as_path=True)
        assets_dir.mkdir(parents=True, exist_ok=True)
        output = assets_dir / f"{tex_path.stem}.svg"
        temporary = output.with_suffix(".svg.tmp")
        temporary.write_text(svg, encoding="utf-8")
        os.replace(temporary, output)
        print(f"rendered {output}")
        return output

    def main(argv: list[str]) -> int:
        requested = argv[1:]
        if requested:
            figures = [FIGURES_DIR / name for name in requested]
        else:
            figures = sorted(FIGURES_DIR.glob(FIGURE_GLOB))
        if not figures:
            raise RuntimeError(f"no figures matched {FIGURE_GLOB}")
        for figure in figures:
            if not figure.is_file() or not figure.match(FIGURE_GLOB):
                raise RuntimeError(f"invalid figure source: {figure}")
            render(figure, ASSETS_DIR, BUILD_ROOT)
        return 0

    if __name__ == "__main__":
        try:
            raise SystemExit(main(sys.argv))
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            print(f"render failed: {error}", file=sys.stderr)
            raise SystemExit(1) from error

- [ ] **Step 5: 验证 Python 工具**

Run:

    python -m py_compile unnset/docs/figures/render.py

Expected: PASS，无输出。

- [ ] **Step 6: 提交生成链**

    git add unnset/docs/figures/.gitignore \
      unnset/docs/figures/holistic-style.tex \
      unnset/docs/figures/render.py
    git diff --cached --check
    git commit -m "docs: add TikZ figure rendering pipeline"

## Task 2: 绘制六幅原创 LaTeX 图并生成 SVG

**Files:**

- Create: unnset/docs/figures/holistic-01-scope-plan.tex
- Create: unnset/docs/figures/holistic-02-lca-accessing.tex
- Create: unnset/docs/figures/holistic-03-binding-blowup.tex
- Create: unnset/docs/figures/holistic-04-top-down-trace.tex
- Create: unnset/docs/figures/holistic-05-domain-or-repr.tex
- Create: unnset/docs/figures/holistic-06-final-plan.tex
- Create: unnset/docs/assets/holistic-01-scope-plan.svg through holistic-06-final-plan.svg

- [ ] **Step 1: 记录六图缺失的基线失败**

Run:

    for n in 01 02 03 04 05 06; do
      test -n "$(find unnset/docs/figures -maxdepth 1 \
        -name "holistic-$n-*.tex" -print -quit)"
    done

Expected: FAIL，因为图源尚不存在。

- [ ] **Step 2: 绘制作用域与 LCA 两图**

所有图使用 article、geometry、tikz，加载 positioning、fit、calc、arrows.meta、matrix，并 input holistic-style.tex。图内只放英文缩写、关系名和短数学标签，中文解释留在 HTML 图注。

holistic-01-scope-plan.tex 必须包含两个 dependent join J_D、J_E，三个 scan R_D/R_E/R_X，两层 group-by，以及 d.dept_id、d.fiscal_year、e.emp_id 三种外层引用。

holistic-02-lca-accessing.tex 必须同时画出：

    source R_D -> consumer S_E -> LCA J_D
    source R_D -> consumer S_X -> LCA J_D
    source R_E -> consumer S_X -> LCA J_E
    source R_X -> consumer S_X -> LCA S_X (local, not marked)
    accessing(J_D) = {S_E, S_X}
    accessing(J_E) = {S_X}

- [ ] **Step 3: 绘制绑定域与 top-down trace 两图**

holistic-03-binding-blowup.tex 必须画出 P={(1,x),(2,y)}、D_a={1,2}、D_b={x,y} 与 2x2 组合矩阵；有效格使用绿色实框，无效但稍后才过滤的格使用红色斜线，不能只靠颜色区分。

holistic-04-top-down-trace.tex 必须包含四个编号状态卡：

    1. J_D: outerRefs={d.id,d.year}, accessing={S_E,S_X}
    2. S_E: cclasses += {d.id,e.did}, remove S_E
    3. J_E: merge parent access S_X
    4. S_X: add {e.id,x.eid},{d.year,x.year}; choose D or repr

向下处理使用实线，递归返回/列重写使用虚线。

- [ ] **Step 4: 绘制成本选择与最终计划两图**

holistic-05-domain-or-repr.tex 左侧画显式 D join，右侧画 repr substitution；两边标题明确为 same semantics, different cost choice。

holistic-06-final-plan.tex 只允许普通 join、selection 和 group-by，不出现 dependent join；从下到上对应 binding_domain、employee_spend、qualified 三层，并标出分组键 dept_id/fiscal_year/emp_id。

- [ ] **Step 5: 从干净目录渲染全部图**

Run:

    rm -rf unnset/docs/figures/build
    python unnset/docs/figures/render.py

Expected: 输出六行以 `rendered unnset/docs/assets/holistic-` 对应路径结尾的消息，退出码 0。

- [ ] **Step 6: 验证生成物完整且可解析**

Run:

    python - <<'PY'
    from pathlib import Path
    from xml.etree import ElementTree
    svgs = sorted(Path("unnset/docs/assets").glob("holistic-[0-9][0-9]-*.svg"))
    assert len(svgs) == 6, [p.name for p in svgs]
    for svg in svgs:
        assert svg.stat().st_size > 1000, svg
        ElementTree.parse(svg)
    print("validated 6 SVG figures")
    PY

Expected: validated 6 SVG figures。

- [ ] **Step 7: 提交图源与生成物**

    git add unnset/docs/figures/holistic-*.tex \
      unnset/docs/assets/holistic-*.svg
    git diff --cached --check
    git commit -m "docs: add indexed algebra TikZ figures"

## Task 3: 编写独立中文详解 HTML

**Files:**

- Create: unnset/docs/holistic-query-unnesting-indexed-algebra.html

- [ ] **Step 1: 记录页面缺失的基线失败**

Run:

    test -f unnset/docs/holistic-query-unnesting-indexed-algebra.html

Expected: FAIL。

- [ ] **Step 2: 建立页面外壳和导航**

复用现有页面的颜色 token、hero、source box、sticky nav、section、callout、figure、table 和 860px 响应式规则。页面必须是自包含静态 HTML，不引入 JavaScript、MathJax、KaTeX 或外部 CSS。

固定章节 ID：

    concept-split
    running-example
    indexed-model
    lca
    accessing
    bottom-up-cost
    holistic-trace
    domain-or-repr
    final-plan
    boundaries
    reading-map

Hero 必须反链 nested-sql-unnesting-tutorial.html#paper-2025，source box 必须链接本地 2015/2025 PDF、PVLDB 2023 官方 PDF 和 arXiv 形式化报告。

- [ ] **Step 3: 写完 Indexed Algebra 与标注主线**

章节 1–5 必须包含：

- Indexed Algebra、data index、unnesting rewrite 三列对照；
- 完整 department/employee/expense SQL；
- 三表各至少三行样例数据；
- operator/expression/IU/source/consumer 定义表；
- 普通向上数据流与跨分支引用的判定；
- 逐引用的 access/source/LCA/action 表，明确本地 x.amount 不标注；
- accessing(J_D)={S_E,S_X} 和 accessing(J_E)={S_X}；
- 每次 LCA 摊还 O(log n)，总识别阶段 O(m log n)；
- 没有 Indexed Algebra 时仍可较慢地得到相同信息。

引用 holistic-01-scope-plan.svg、holistic-02-lca-accessing.svg，并给出非空 alt 与教学型 figcaption。

- [ ] **Step 4: 写完 2015 放大与 top-down trace**

章节 6–9 必须包含：

- T1/T2 小数据的真实联合域 P 与 D_a x D_b 四组合；
- 明确无效组合会被后续过滤，结果仍正确；
- 状态表列出当前节点、accessing、outerRefs、cclasses、repr 和动作；
- 嵌套 J_E 怎样合并父层仍指向 S_X 的访问；
- 并排比较加入 D 与 substitution，强调成本选择而非语义差异；
- 最终无相关 SQL/CTE 形状和普通代数计划；
- 两层 group-by 为什么必须带上绑定键。

引用 holistic-03-binding-blowup.svg 至 holistic-06-final-plan.svg。

- [ ] **Step 5: 写完边界与阅读地图**

章节 10–11 必须包含：

- IS NOT DISTINCT FROM / 论文 IS 语义；
- D duplicate-free 前提；
- static aggregate 空输入语义；
- Indexed Algebra 是性能基础设施而非正确性前提；
- 2023 PDF pp.2–6 / sections 2–4.2；
- 2025 PDF pp.6–16 / sections 2.3、3.1–3.3；
- 2024 formalization 的用途；
- 课程 PPT 判定条件的勘误提醒。

- [ ] **Step 6: 运行页面结构与资源检查**

Run:

    page=unnset/docs/holistic-query-unnesting-indexed-algebra.html
    test "$(rg -o '<section id="[^"]+"' "$page" | wc -l)" -eq 11
    test "$(rg -o '<img [^>]*alt="[^"]+"' "$page" | wc -l)" -eq 6
    for id in concept-split running-example indexed-model lca accessing \
      bottom-up-cost holistic-trace domain-or-repr final-plan boundaries reading-map; do
      rg -q -F "id=\"$id\"" "$page"
    done

Expected: PASS，无输出。

- [ ] **Step 7: 提交新页面**

    git add unnset/docs/holistic-query-unnesting-indexed-algebra.html
    git diff --cached --check
    git commit -m "docs: explain indexed algebra holistic unnesting"

## Task 4: 将新页面接入现有教程

**Files:**

- Modify: unnset/docs/nested-sql-unnesting-tutorial.html

- [ ] **Step 1: 记录跨页入口缺失的基线失败**

Run:

    rg -q 'holistic-query-unnesting-indexed-algebra.html' \
      unnset/docs/nested-sql-unnesting-tutorial.html

Expected: FAIL。

- [ ] **Step 2: 增加三个入口并修复 HTML entity**

只做以下改动：

1. 首行小写 doctype 改为 HTML Validate 接受的 `<!DOCTYPE html>`。
2. Hero 阅读材料中的 Neumann & Kemper 改为 Neumann &amp; Kemper。
3. sticky nav 的“2025 改进”之后加入跨页链接“2025 深入”。
4. 4.5 Indexed Algebra 说明后加入 note，链接新页的 #indexed-model。
5. footer 加入新页面链接，不修改其他章节正文。

- [ ] **Step 3: 验证双向链接与本地资源**

Run:

    rg -q 'holistic-query-unnesting-indexed-algebra.html' \
      unnset/docs/nested-sql-unnesting-tutorial.html
    rg -q 'nested-sql-unnesting-tutorial.html#paper-2025' \
      unnset/docs/holistic-query-unnesting-indexed-algebra.html

Expected: PASS。

- [ ] **Step 4: 提交导航接入**

    git add unnset/docs/nested-sql-unnesting-tutorial.html
    git diff --cached --check
    git commit -m "docs: link indexed algebra deep dive"

## Task 5: 完整文档验收与事实复核

**Files:**

- Verify: unnset/docs/*.html
- Verify: unnset/docs/figures/*
- Verify: unnset/docs/assets/holistic-*.svg

- [ ] **Step 1: 从干净中间目录重建六图**

Run:

    rm -rf unnset/docs/figures/build
    python unnset/docs/figures/render.py
    python -m py_compile unnset/docs/figures/render.py

Expected: 六图均显示 rendered，Python 检查退出码 0。

- [ ] **Step 2: 校验 SVG、HTML fragment、本地资源、重复 ID 和 alt**

Run:

    python - <<'PY'
    from html.parser import HTMLParser
    from pathlib import Path
    from urllib.parse import urlsplit
    from xml.etree import ElementTree

    class Audit(HTMLParser):
        def __init__(self):
            super().__init__()
            self.ids, self.refs, self.images = [], [], []
        def handle_starttag(self, tag, attrs):
            attrs = dict(attrs)
            if "id" in attrs:
                self.ids.append(attrs["id"])
            if tag == "a" and "href" in attrs:
                self.refs.append(attrs["href"])
            if tag in {"img", "script"} and "src" in attrs:
                self.refs.append(attrs["src"])
            if tag == "img":
                self.images.append(attrs.get("alt", ""))

    docs = Path("unnset/docs")
    for page in docs.glob("*.html"):
        audit = Audit()
        audit.feed(page.read_text(encoding="utf-8"))
        assert len(audit.ids) == len(set(audit.ids)), f"duplicate id: {page}"
        assert all(alt.strip() for alt in audit.images), f"empty alt: {page}"
        for ref in audit.refs:
            parsed = urlsplit(ref)
            if parsed.scheme or ref.startswith(("mailto:", "data:")):
                continue
            target = page if not parsed.path else page.parent / parsed.path
            assert target.exists(), f"missing {ref} from {page}"
            if parsed.fragment and target.suffix == ".html":
                other = Audit()
                other.feed(target.read_text(encoding="utf-8"))
                assert parsed.fragment in other.ids, f"missing fragment {ref}"

    svgs = sorted((docs / "assets").glob("holistic-[0-9][0-9]-*.svg"))
    assert len(svgs) == 6
    for svg in svgs:
        ElementTree.parse(svg)
    print("document audit passed: 6 SVG figures")
    PY

Expected: document audit passed: 6 SVG figures。

- [ ] **Step 3: 运行 HTML5 validator 和 diff 检查**

Run:

    npx --yes html-validate unnset/docs/*.html
    git diff --check
    git status --short

Expected: HTML Validate 0 errors，git diff --check 无输出。

- [ ] **Step 4: 逐条事实复核**

对照设计说明检查并记录：

    [ ] 2025 论文题名与 Holistic 节标题没有混淆
    [ ] Indexed Algebra 没有被描述为数据索引或 unnesting rewrite
    [ ] LCA 判定是 LCA(access,source) != access
    [ ] accessing(J_D) 与 accessing(J_E) 逐引用可复算
    [ ] 复杂度写成每次 O(log n)、m 次 O(m log n)
    [ ] 2015 病理明确是性能问题而非结果错误
    [ ] D 与 repr 明确是成本选择
    [ ] NULL-safe、duplicate-free、static aggregate 条件均存在

若任一项不满足，修改 HTML 后重新执行 Steps 1–3。

- [ ] **Step 5: 检查版本控制结果**

Run:

    git log --oneline -6
    git status --short
    git diff HEAD^ --check

Expected: 能看到设计、生成链、图、页面和导航的独立提交；工作树干净。
