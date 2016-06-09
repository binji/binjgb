all: binjgb

out/:
	mkdir -p out

out/Makefile: | out/
	cd out && cmake ..

.PHONY: binjgb
binjgb: out/Makefile
	$(MAKE) --no-print-directory -C out
