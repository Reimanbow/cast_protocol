set dotenv-load

IDF_VERSION := env("IDF_VERSION", "v5.4")
TARGET := env("TARGET", "esp32s3")
PORT := env("PORT", "/dev/ttyUSB0")

# Versions and targets to test
IDF_VERSIONS := env("IDF_VERSIONS", "v5.0 v5.4")
TARGETS := env("TARGETS", "esp32s3")

COMPONENT_NAME := env("COMPONENT_NAME", "cast_protocol")
_docker_run := "docker run --rm -v .:/project/" + COMPONENT_NAME + " -w /project/" + COMPONENT_NAME + " espressif/idf:" + IDF_VERSION

# Run host unit tests (GoogleTest)
test:
    cmake -S test/host -B test/host/build
    cmake --build test/host/build
    test/host/build/host_test

# Build for target (single version/target)
check: clean
    {{ _docker_run }} bash -c "cd test && idf.py set-target {{ TARGET }} && idf.py build"

# Test across all IDF versions and targets
check-matrix:
    #!/usr/bin/env bash
    set -euo pipefail
    failed=0
    for ver in {{ IDF_VERSIONS }}; do
        for tgt in {{ TARGETS }}; do
            echo "=== IDF $ver / $tgt ==="
            docker run --rm -v .:/project/{{ COMPONENT_NAME }} -w /project/{{ COMPONENT_NAME }} "espressif/idf:$ver" \
                rm -rf test/build
            if docker run --rm -v .:/project/{{ COMPONENT_NAME }} -w /project/{{ COMPONENT_NAME }} "espressif/idf:$ver" \
                bash -c "cd test && idf.py set-target $tgt && idf.py build"; then
                echo "=== PASS: IDF $ver / $tgt ==="
            else
                echo "=== FAIL: IDF $ver / $tgt ==="
                failed=1
            fi
        done
    done
    if [ "$failed" -ne 0 ]; then
        echo "Some combinations failed."
        exit 1
    fi
    echo "All combinations passed."

# Flash and monitor (requires device access)
flash-monitor:
    {{ _docker_run }} --device {{ PORT }} bash -c "cd test && idf.py -p {{ PORT }} flash monitor"

# Clean build artifacts (Docker for root-owned target build files)
clean:
    rm -rf test/host/build
    {{ _docker_run }} rm -rf test/build
