# WarpSim developer entry points. Every target is reproducible in CI.
PYTHON  ?= python3
PRESET  ?= release
BUILD    = build/$(PRESET)
PYTEST   = PYTHONPATH=python $(PYTHON) -m pytest python/tests

.PHONY: quickstart configure build test test-cpp test-py report bench lint sanitize clean

quickstart: build test-cpp test-py
	@echo "warpsim $$(PYTHONPATH=python $(PYTHON) -c 'import warpsim; print(warpsim.version())'): build ok, C++ tests ok, Python tests ok"
	@echo "Tiled matmul report (real run):"
	@PYTHONPATH=python $(PYTHON) -m warpsim.report --kernel matmul_tiled

report: build
	PYTHONPATH=python $(PYTHON) -m warpsim.report

configure:
	cmake --preset $(PRESET)

build: configure
	cmake --build --preset $(PRESET)

test: test-cpp test-py

test-cpp: build
	ctest --preset $(PRESET)

test-py: build
	$(PYTEST)

bench: build
	PYTHONPATH=python $(PYTHON) -m warpsim.bench

lint:
	cmake --preset debug
	scripts/lint.sh build/debug
	scripts/check_prose.sh

sanitize:
	cmake --preset asan && cmake --build --preset asan && ctest --preset asan
	cmake --preset ubsan && cmake --build --preset ubsan && ctest --preset ubsan

clean:
	rm -rf build python/warpsim/*.so
