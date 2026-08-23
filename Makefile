# WarpSim developer entry points. Every target is reproducible in CI.
PYTHON  ?= python3
PRESET  ?= release
BUILD    = build/$(PRESET)
PYTEST   = PYTHONPATH=python $(PYTHON) -m pytest python/tests

.PHONY: quickstart configure build test test-cpp test-py bench lint sanitize clean

quickstart: build test-cpp test-py
	@echo "warpsim $$(PYTHONPATH=python $(PYTHON) -c 'import warpsim; print(warpsim.version())'): build ok, C++ tests ok, Python tests ok"
	@echo "Kernel reports arrive with milestone M4; the tiled matmul report is printed here from M5 onward."

configure:
	cmake --preset $(PRESET)

build: configure
	cmake --build --preset $(PRESET)

test: test-cpp test-py

test-cpp: build
	ctest --preset $(PRESET)

test-py: build
	$(PYTEST)

bench:
	@echo "make bench becomes live in milestone M5 (naive against tiled matmul with attribution)."
	@exit 1

lint:
	cmake --preset debug
	scripts/lint.sh build/debug

sanitize:
	cmake --preset asan && cmake --build --preset asan && ctest --preset asan
	cmake --preset ubsan && cmake --build --preset ubsan && ctest --preset ubsan

clean:
	rm -rf build python/warpsim/*.so
