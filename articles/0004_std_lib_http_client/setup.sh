#!/usr/bin/env bash

# Create our Python virtual environment. This will be found in the CMakeLists and allow us to automatically build our
# wheel.
if [ ! -d ".venv" ]; then
    python3.12 -m venv .venv;
    . .venv/bin/activate;
    (
        cd src/python || (echo "python source directory not found" && exit)
        python3 -m pip install -r requirements.txt
    )
fi
