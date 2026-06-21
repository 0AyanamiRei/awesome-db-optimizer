# Join Order Proof Notes

This directory contains LaTeX notes for mathematical proofs around join-order papers.

## Local Setup

Use a TeX Live or TinyTeX installation with `latexmk` and `xelatex`.

For TinyTeX, install the packages used by the current notes:

```sh
tlmgr update --self
tlmgr install ctex enumitem
```

`ctex` provides `ctexart.cls`, Chinese typesetting support, `xeCJK`, and the bundled Fandol fonts used by XeLaTeX.

## Build

From this directory:

```sh
make
```

or directly:

```sh
latexmk join-order-proof-notes.tex
```

From the repository root, this also works:

```sh
latexmk joinOrder/proofs/join-order-proof-notes.tex
```

For editor preview or continuous rebuild:

```sh
make watch
```

The local and repository-level `latexmk` configuration uses XeLaTeX because the notes contain Chinese text and use `ctexart`.
