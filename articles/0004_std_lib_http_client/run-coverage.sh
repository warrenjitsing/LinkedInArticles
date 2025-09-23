#!/usr/bin/env bash
(
    cd build_debug || exit
    cmake --build .
    lcov --directory . --zerocounters
    rm -rf coverage_html.tar.gz coverage_html

    ctest --output-on-failure

    lcov --capture \
         --directory . \
         --output-file coverage.info &> /dev/null

    lcov --remove coverage.info \
         '/usr/*' \
         '*/_deps/*' \
         '*/tests/helpers.h' \
         '*/benchmark/*' \
         '*/apps/*' \
         '*/docs/*' \
         '*/cmake/*' \
         '*/.cache/*' \
         -o coverage.filtered.info &> /dev/null

    genhtml coverage.filtered.info \
        --output-directory coverage_html \
        --legend \
        --title "http Code Coverage" &> /dev/null


    echo "✅ Coverage HTML report generated in build_debug/coverage_html/index.html"
)

(
    cd src/rust || exit
    cargo llvm-cov --html
    echo "✅ Coverage HTML report generated in src/rust/target/llvm-cov/html"
)

(
    . .venv/bin/activate
    cd src/python
    python3 -m pip install --editable . &> /dev/null
    pytest -sv --cov=httppy --cov-report=html tests
    echo "✅ Coverage HTML report generated in src/python/htmlcov/index.html"
)