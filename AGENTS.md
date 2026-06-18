# AGENTS.md

## Repository Expectations

- For changes or reviews under `demos/demo`, use the repo skill `reviewing-db-optimizer-demo`.
- Treat `demos/demo/docs/*.html` as human-facing reading material, not agent operating instructions.
- Treat Markdown files such as `demos/demo/AGENT_CONTENT_MANAGEMENT.md` as agent-facing project management guidance.
- For documentation credibility work in the demo, read `demos/demo/AGENT_CONTENT_MANAGEMENT.md` before editing or reviewing docs.
- Choose verification by risk surface, not by directory alone. Pure docs/navigation/diagram changes use document-level checks only.
- Run the demo build/test gates from `demos/demo` only when C++ source, headers, tests, CLI behavior, generated artifacts, or executable behavior claims changed.
- For C++ behavior changes, also verify a strict warning build or report why it was not run.
- Preserve unrelated uncommitted user changes.
