all: binjgb
.PHONY: binjgb
binjgb: debug
	ln -sf Debug/binjgb out/binjgb
	ln -sf Debug/tester out/tester
	ln -sf Debug/debugger out/debugger

define BUILD
out/$1:
	mkdir -p out/$1
out/$1/Makefile: | out/$1
	cd out/$1 && cmake ../.. -DCMAKE_BUILD_TYPE=$1
.PHONY: $2
$2: out/$1/Makefile
	$(MAKE) --no-print-directory -C out/$1
	ln -sf $1/binjgb out/binjgb-$2
	ln -sf $1/tester out/tester-$2
	ln -sf $1/debugger out/debugger-$2
endef

$(eval $(call BUILD,Debug,debug))
$(eval $(call BUILD,Release,release))

# Emscripten stuff

EMSCRIPTEN_DIR := emscripten

out/Emscripten:
	mkdir -p out/Emscripten
out/Emscripten/Makefile: | out/Emscripten
	cd out/Emscripten && \
		cmake ../.. -DCMAKE_TOOLCHAIN_FILE=${EMSCRIPTEN_DIR}/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_BUILD_TYPE=Release
.PHONY: emscripten
emscripten: out/Emscripten/Makefile
	$(MAKE) --no-print-directory -C out/Emscripten
