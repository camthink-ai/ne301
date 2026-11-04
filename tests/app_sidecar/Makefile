CC ?= cc
REPO_ROOT := ../..

.PHONY: vectors clean
vectors: build/sidecar_vectors.h
build/vectors: vectors

build/sidecar_vectors.h build/positive_small.bin build/positive_counting.bin: gen_sidecar_fixtures.py | build
	python3 gen_sidecar_fixtures.py --output build

build:
	mkdir -p build

clean:
	rm -rf build test_app_sidecar
