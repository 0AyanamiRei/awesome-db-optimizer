# Demo Agent Content Management

This file is for agents managing `demos/demo`. The HTML files under `docs/` are human-facing reading material; do not turn them into process checklists. Use this Markdown file to track credibility rules, source-of-truth boundaries, and refresh work.

## Content Boundary

| File type | Audience | Purpose | Agent rule |
| --- | --- | --- | --- |
| `docs/*.html` | Human reader | Narrative explanation, diagrams, walkthroughs, examples | Keep readable and coherent; verify claims before editing. |
| `*.md` management files | Agent | Project governance, content inventory, refresh rules, known drift | Keep concise, operational, and easy to scan before work. |
| `src/`, `include/`, `examples/`, `tests/` | Compiler/test suite | Executable behavior and evidence | Treat as source of truth for behavior. |

## Content Inventory

| Human-facing page | Role | Code/test evidence | High-drift content |
| --- | --- | --- | --- |
| `docs/index.html` | Lab overview: architecture, strategies, CLI, tests, comparison insights | `CMakeLists.txt`, `search_strategy.hpp`, `main.cpp`, `tests/test_join_search.cpp`, `volcano_join_demo --compare` | Strategy comparison tables, CLI snippets, source line references. |
| `docs/dpsub-workflow.html` | DPSub deep dive and walkthrough | `src/dp_sub.cpp`, `include/volcano/dp_sub.hpp`, `tests/test_join_search.cpp`, `--strategy dpsub`, `--compare --test chain_3` | Trace counters, example plan strings, DP cell counts, line-number links. |
| `docs/mpdp-workflow.html` | MPDP deep dive and block-local join-pair walkthrough | `src/mpdp.cpp`, `include/volcano/mpdp.hpp`, `examples/test_cases.cpp`, `tests/test_join_search.cpp`, `--strategy mpdp`, `--compare --test mpdp_fig5`, `--compare --test mpdp_branching_blocks` | MPDP trace counters, Figure 5-style and branching block-cut tree descriptions, CLI comparison numbers. |
| `docs/transformational-search.html` | Transformational search deep dive and Volcano comparison | `src/transformational.cpp`, `include/volcano/transformational.hpp`, `--strategy transform`, `--compare --test chain_3` | Duplicate/rule/cache counter examples, Volcano gap claims, line-number links. |
| `docs/topdown-partitioning-workflow.html` | TopDown and Mincut walkthrough | `src/top_down_partitioning.cpp`, `src/mincut.cpp`, `include/volcano/top_down_partitioning.hpp`, `include/volcano/mincut.hpp`, `--compare` across built-in cases | Naive vs Mincut tables, BC-tree counts, branch-and-bound claims, line-number links. |

## Source Of Truth

- The demo is paper-oriented: each strategy should primarily preserve the semantics and claims of its source paper or explicitly documented adaptation.
- Shared infrastructure is optional scaffolding, not a mandate. Do not force a new paper implementation to keep Volcano-style properties, memo contracts, trace fields, CLI shape, or cross-strategy behavior merely for uniformity.
- If shared demo features would obscure or distort the target paper algorithm, isolate the algorithm first and then adapt only the smallest necessary boundary for tests, CLI, and comparison.
- Executable behavior comes from the current C++ binary and tests, not copied HTML snippets.
- Built-in case names come from `volcano_join_demo --list`.
- Strategy comparison numbers come from `volcano_join_demo --compare --test <case>`.
- Plan strings come from `volcano_join_demo --strategy <name> --test <case> [--required <property>]`.
- Algorithm names and public interfaces come from headers under `include/volcano`.
- `JoinGraph` owns relation/predicate topology; `StatsCatalog` owns cost inputs (`rows`, `scan_cost`, predicate selectivity). Cost-model changes must keep strategies reading cost inputs from `StatsCatalog`.
- Tests are evidence only for behavior they assert; printed test output is not a contract unless asserted.

## Refresh Commands

Run from `demos/demo`:

```bash
cmake --build --preset dev
./build/volcano_join_demo --list
./build/volcano_join_demo --compare --test two_table
./build/volcano_join_demo --compare --test chain_3
./build/volcano_join_demo --compare --test chain_4
./build/volcano_join_demo --compare --test star_4
./build/volcano_join_demo --compare --test cycle_4
./build/volcano_join_demo --compare --test clique_4
./build/volcano_join_demo --compare --test mpdp_fig5
./build/volcano_join_demo --compare --test mpdp_branching_blocks
ctest --preset dev
```

Use the smallest subset when only one page or case is touched. Run all compare commands when changing strategies, cost model, trace counters, test cases, CLI table formatting, or broad comparison docs.

For C++ behavior changes, also run a strict warning build or report why it was not run. A local command that avoids touching the dev build directory:

```bash
cmake -S . -B /tmp/volcano-demo-strict -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror -pedantic"
cmake --build /tmp/volcano-demo-strict
ctest --test-dir /tmp/volcano-demo-strict --output-on-failure
```

## Current Canonical Compare Snapshot

Last refreshed in this thread on 2026-06-22 from `demos/demo` after `cmake --build --preset dev`.

| Case | Strategy | Partitions | Rejected | PlansCosted | CacheHits | Cached | Dups | Rules | DPCells | Pruned | BestCost |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `two_table` | DPSub | 2 | 0 | 16 | 0 | 7 | 0 | 0 | 7 | 0 | 3200.00 |
| `two_table` | MPDP | 2 | 0 | 6 | 0 | 3 | 0 | 0 | 3 | 0 | 3200.00 |
| `two_table` | Transformational | 0 | 0 | 10 | 6 | 5 | 2 | 2 | 0 | 0 | 3200.00 |
| `two_table` | TopDown(Naive) | 1 | 0 | 9 | 0 | 5 | 0 | 0 | 0 | 0 | 3200.00 |
| `two_table` | TopDown(Mincut) | 1 | 0 | 9 | 0 | 5 | 0 | 0 | 0 | 0 | 3200.00 |
| `chain_3` | DPSub | 10 | 2 | 57 | 0 | 20 | 0 | 0 | 20 | 0 | 19651.56 |
| `chain_3` | MPDP | 8 | 0 | 19 | 0 | 6 | 0 | 0 | 6 | 0 | 30200.00 |
| `chain_3` | Transformational | 0 | 0 | 34 | 27 | 12 | 16 | 24 | 0 | 0 | 19651.56 |
| `chain_3` | TopDown(Naive) | 7 | 1 | 25 | 7 | 12 | 0 | 0 | 0 | 0 | 19651.56 |
| `chain_3` | TopDown(Mincut) | 6 | 0 | 25 | 7 | 12 | 0 | 0 | 0 | 0 | 19651.56 |
| `chain_4` | DPSub | 32 | 12 | 136 | 0 | 42 | 0 | 0 | 42 | 0 | 41500.24 |
| `chain_4` | MPDP | 20 | 0 | 44 | 0 | 10 | 0 | 0 | 10 | 0 | 109600.00 |
| `chain_4` | Transformational | 0 | 0 | 78 | 71 | 22 | 60 | 100 | 0 | 0 | 41500.24 |
| `chain_4` | TopDown(Naive) | 26 | 8 | 52 | 25 | 22 | 0 | 0 | 0 | 0 | 41500.24 |
| `chain_4` | TopDown(Mincut) | 18 | 0 | 52 | 25 | 22 | 0 | 0 | 0 | 0 | 41500.24 |
| `star_4` | DPSub | 38 | 14 | 163 | 0 | 50 | 0 | 0 | 50 | 0 | 24360.00 |
| `star_4` | MPDP | 24 | 0 | 52 | 0 | 11 | 0 | 0 | 11 | 0 | 24360.00 |
| `star_4` | Transformational | 0 | 0 | 93 | 86 | 26 | 72 | 120 | 0 | 0 | 24360.00 |
| `star_4` | TopDown(Naive) | 34 | 10 | 61 | 32 | 26 | 0 | 0 | 0 | 0 | 24360.00 |
| `star_4` | TopDown(Mincut) | 24 | 0 | 61 | 32 | 26 | 0 | 0 | 0 | 0 | 24360.00 |
| `cycle_4` | DPSub | 46 | 10 | 264 | 0 | 69 | 0 | 0 | 69 | 0 | 14800.00 |
| `cycle_4` | MPDP | 36 | 0 | 76 | 0 | 13 | 0 | 0 | 13 | 0 | 14800.00 |
| `cycle_4` | Transformational | 0 | 0 | 136 | 132 | 33 | 164 | 196 | 0 | 0 | 14800.00 |
| `cycle_4` | TopDown(Naive) | 43 | 9 | 86 | 52 | 33 | 0 | 0 | 0 | 0 | 14800.00 |
| `cycle_4` | TopDown(Mincut) | 34 | 0 | 86 | 52 | 33 | 0 | 0 | 0 | 0 | 14800.00 |
| `clique_4` | DPSub | 50 | 0 | 466 | 0 | 111 | 0 | 0 | 111 | 0 | 4900.00 |
| `clique_4` | MPDP | 50 | 0 | 104 | 0 | 15 | 0 | 0 | 15 | 0 | 4900.00 |
| `clique_4` | Transformational | 0 | 0 | 190 | 186 | 43 | 290 | 290 | 0 | 0 | 4900.00 |
| `clique_4` | TopDown(Naive) | 49 | 0 | 119 | 74 | 43 | 0 | 0 | 0 | 0 | 4900.00 |
| `clique_4` | TopDown(Mincut) | 49 | 0 | 119 | 74 | 43 | 0 | 0 | 0 | 0 | 4900.00 |
| `mpdp_fig5` | DPSub | 5166 | 4422 | 5103 | 0 | 1032 | 0 | 0 | 1032 | 0 | 1641477.39 |
| `mpdp_fig5` | MPDP | 744 | 0 | 1497 | 0 | 90 | 0 | 0 | 90 | 0 | 75903325.79 |
| `mpdp_fig5` | Transformational | 0 | 0 | 2466 | 2887 | 304 | 8240 | 14008 | 0 | 0 | 1641477.39 |
| `mpdp_fig5` | TopDown(Naive) | 7089 | 5933 | 1359 | 1379 | 304 | 0 | 0 | 0 | 0 | 1641477.39 |
| `mpdp_fig5` | TopDown(Mincut) | 1156 | 0 | 1359 | 1379 | 304 | 0 | 0 | 0 | 0 | 1641477.39 |
| `mpdp_branching_blocks` | DPSub | 1336 | 850 | 3805 | 0 | 865 | 0 | 0 | 865 | 0 | 11811.99 |
| `mpdp_branching_blocks` | MPDP | 486 | 0 | 979 | 0 | 73 | 0 | 0 | 73 | 0 | 11811.99 |
| `mpdp_branching_blocks` | Transformational | 0 | 0 | 1683 | 1872 | 280 | 4038 | 6438 | 0 | 0 | 11811.99 |
| `mpdp_branching_blocks` | TopDown(Naive) | 1937 | 1115 | 961 | 882 | 280 | 0 | 0 | 0 | 0 | 11811.99 |
| `mpdp_branching_blocks` | TopDown(Mincut) | 822 | 0 | 961 | 882 | 280 | 0 | 0 | 0 | 0 | 11811.99 |

## Known Drift To Watch

- No known compare-output drift remains after the 2026-06-22 refresh in this thread.
- `docs/dpsub-workflow.html`, `docs/mpdp-workflow.html`, `docs/transformational-search.html`, `docs/topdown-partitioning-workflow.html`, and `docs/index.html` have been aligned to the current canonical compare snapshot above.
- Some HTML source links include concrete line numbers. Treat those as fragile; prefer file/function references when editing prose unless a line-number link is specifically useful for short-lived reading.

## Editing Rules For Agents

1. Implement the paper-faithful behavior first; then update docs, snapshots, and prose to match. Do not preserve docs by weakening code changes.
2. Do not edit HTML numbers from memory. Refresh or quote the relevant command output first.
3. Keep HTML reader-focused: explain the idea, then show evidence. Do not add agent process notes to HTML.
4. Keep Markdown agent-focused: inventories, rules, known drift, verification commands, and TODO-style management state belong here.
5. When a doc changes algorithm behavior claims, run the CLI command proving that claim and record it in the final response.
6. When a doc changes only layout/prose/navigation, use document-level checks from the repo review skill instead of running CMake mechanically.
7. When fixing drift, prefer replacing large copied output blocks with smaller excerpts plus a source command, unless the full table is central to the lesson.
