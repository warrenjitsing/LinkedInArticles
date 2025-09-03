#!/usr/bin/env bash
USERNAME="$USER"

# Create our persistent directories on the host if they don't exist.
# This ensures they are created with the correct host user permissions.
mkdir -p repos data

docker run -it \
  --name "dev-container" \
  --restart always \
  -v "$(pwd)/data:/home/$USERNAME/data" \
  -v "$(pwd)/repos:/home/$USERNAME/repos" \
  -p 127.0.0.1:10200:22 \
  -p 127.0.0.1:10201:8888 \
  dev-container:latest