all: bgb

out/:
	mkdir -p out

out/Makefile: | out/
	cd out && cmake ..

bgb: out/Makefile
	$(MAKE) --no-print-directory -C out
