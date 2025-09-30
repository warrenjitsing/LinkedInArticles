#!/usr/bin/env bash
sudo apt-get update && sudo apt-get install -y llvm lcov hyperfine libcurl4-openssl-dev libboost-all-dev
cargo install cargo-llvm-cov