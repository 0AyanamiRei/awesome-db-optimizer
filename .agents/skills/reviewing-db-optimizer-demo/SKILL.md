---
name: reviewing-db-optimizer-demo
description: Use when reviewing, accepting, or completing generated changes to the C++ database optimizer demo under demos/demo, especially optimizer strategies, cost model, join graph semantics, CLI behavior, tests, docs that claim behavior, or PR-style code review requests.
---

# Reviewing DB Optimizer Demo

## Overview

Review generated demo changes by their risk surface. Optimizer behavior needs executable evidence; pure documentation layout, navigation, and diagrams need document-level checks.

`demos/demo` is a paper-oriented demo collection. Shared Volcano-style infrastructure is scaffolding, not the design authority. For new or revised paper implementations, prefer faithful paper semantics and isolated algorithm behavior over forced cross-strategy uniformity unless the user explicitly requests a uniform framework.

## Workflow

1. Check the worktree first with `git status --short`; treat existing unrelated changes as user work.
2. Classify the staged/touched files by change type before selecting verification gates.
3. Read nearby code/tests only when source, tests, CLI behavior, or behavior-claiming docs changed.
4. Reproduce or verify behavior with the smallest relevant CLI/test command when the change makes executable claims.
5. Run the narrowest required verification gates before saying work is complete.
6. Report findings first, ordered by severity, with file/line and a repro or concrete failure mode.

## Incorporating Human Process Feedback

When the human points out that a review or verification rule is too broad, too narrow, or misapplied, treat that as a reusable process requirement. If it applies beyond the current one-off task, update this skill or `AGENTS.md` in the same turn so future work follows the refined rule.

## Content Boundary

- Treat `demos/demo/docs/*.html` as human-facing reading material.
- Treat Markdown management files, especially `demos/demo/AGENT_CONTENT_MANAGEMENT.md`, as agent-facing project governance.
- For documentation credibility work, read `demos/demo/AGENT_CONTENT_MANAGEMENT.md` before editing or reviewing HTML docs.
- Do not put agent process rules into HTML pages; keep them in Markdown.
- Treat code and executable behavior as the source of truth. Do not avoid correct source changes to keep existing docs or snapshots stable; update documentation after the implementation settles.

## Required Gates

Choose gates by what changed, not by directory alone:

| Change type | Required verification |
| --- | --- |
| Pure docs layout, prose cleanup, navigation links, diagrams, inline SVG/DOT examples | Document checks only: duplicate ids, anchors/local links relevant to touched files, SVG open/close counts, marker/aria refs, CSS brace balance; run `xmllint --noout` on edited SVG snippets when available. |
| Docs that add or modify CLI output, benchmark numbers, trace counters, best costs, algorithm behavior claims, or command examples | Document checks plus the smallest CLI/test command that proves the changed claim. |
| C++ sources, headers, CMake, tests, CLI behavior, exporter output, or generated artifact behavior | Run the demo gates from `demos/demo`: |

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

For optimizer, cost model, public headers, CLI behavior, or test changes, also run a strict warning build:

```bash
cmake -S . -B /tmp/volcano-demo-strict -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror -pedantic"
cmake --build /tmp/volcano-demo-strict
ctest --test-dir /tmp/volcano-demo-strict --output-on-failure
```

If a gate is intentionally skipped, say why in terms of the change type. If a required gate cannot be run, say why and treat the result as residual risk.

Do not run CMake/CTest mechanically for pure docs-only diagram/navigation/prose PRs. Extra verification is useful only when it proves a risk introduced by the change.

## Review Standard

Focus on these risks:

| Area | Must Check |
| --- | --- |
| Search result contract | Strategies must distinguish "no plan" from a default `PhysicalPlan`; callers must not print/export empty plans as success. |
| Cross-strategy consistency | DPSub, Transformational, TopDown(Naive), and TopDown(Mincut) should agree on best cost for supported properties, or document and test intentional differences. |
| Required properties | `RequiredProperty::Sorted(alias.column)` must either be satisfiable by all applicable strategies or rejected explicitly; arbitrary sorted columns need tests. |
| Cost/statistics contract | `JoinGraph` owns topology; `StatsCatalog` owns cost inputs. Strategies and cost-model code must read rows, scan costs, and predicate selectivity from `StatsCatalog`, with tests proving divergent graph/catalog values use the catalog. |
| Join graph invariants | Validate relation count, alias uniqueness, positive rows/scan costs, selectivity bounds, unknown aliases, empty graphs, disconnected graphs, and cross products. |
| Partition enumeration | Check duplicate/symmetric partitions, CP-free assumptions, mincut equivalence to naive enumeration, and exponential loops against the claimed demo scale. |
| Paper fidelity | Check whether shared demo abstractions distort the target paper algorithm. If they do, prefer isolating the paper implementation and adapting only the minimum CLI/test boundary. |
| Branch-and-bound | Pruning bounds must be proven lower bounds for the current cost model; suspicious constants or unused lower-bound variables are review findings. |
| CLI/export behavior | User-facing commands must fail loudly on invalid requests and avoid writing misleading DOT/JSON for absent plans. |
| Tests | Add focused regression tests for every behavior change; include at least one negative or edge case, not only happy-path examples. |
| Docs | Documentation claims about optimizer behavior must match executable tests or CLI output. |

## Output Format

For reviews, lead with findings:

```text
Findings
- Critical/High/Medium/Low: file:line - issue, impact, repro/evidence, expected fix direction.

Verification
- command -> pass/fail and important output.

Residual risk
- unverified areas or assumptions.
```

For implementation completion, include changed files and verification commands, but do not bury remaining review findings under a summary.

## Red Flags

- "All tests pass" but no edge-case or regression test was added for changed behavior.
- A strategy returns cost `0` or `SeqScan()` for a multi-relation query without explicitly reporting no plan.
- A public API parameter is present but unused, especially `StatsCatalog` in optimizer paths.
- A branch-and-bound condition prunes without a clear lower-bound proof.
- A doc page or demo output claims a behavior that no executable test checks.
- Strict warnings fail or were not run after C++ changes.
