# AGENTS.md

## Repository Expectations

- For changes or reviews under `demos/demo`, use the repo skill `reviewing-db-optimizer-demo`.
- Treat `demos/demo` as a collection of paper-oriented demos. Do not assume the goal is to force all algorithms into one Volcano-style framework; shared infrastructure is a convenience layer, not the design authority.
- When implementing a paper demo, optimize for faithful paper semantics and isolated algorithm behavior first. Only keep shared demo abstractions when they do not distort the target algorithm or when the user explicitly asks for cross-strategy uniformity.
- Code and executable behavior are the source of truth. Do not avoid correct code changes to preserve existing documentation, snapshots, or prose; update docs and snapshots after the implementation settles.
- Treat `demos/demo/docs/*.html` as human-facing reading material, not agent operating instructions.
- Treat Markdown files such as `demos/demo/AGENT_CONTENT_MANAGEMENT.md` as agent-facing project management guidance.
- For documentation credibility work in the demo, read `demos/demo/AGENT_CONTENT_MANAGEMENT.md` before editing or reviewing docs.
- Choose verification by risk surface, not by directory alone. Pure docs/navigation/diagram changes use document-level checks only.
- Run the demo build/test gates from `demos/demo` only when C++ source, headers, tests, CLI behavior, generated artifacts, or executable behavior claims changed.
- For C++ behavior changes, also verify a strict warning build or report why it was not run.
- Preserve unrelated uncommitted user changes.
