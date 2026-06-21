TEX := join-order-proof-notes.tex

.PHONY: pdf watch clean distclean

pdf:
	latexmk $(TEX)

watch:
	latexmk -pvc $(TEX)

clean:
	latexmk -c $(TEX)

distclean:
	latexmk -C $(TEX)
