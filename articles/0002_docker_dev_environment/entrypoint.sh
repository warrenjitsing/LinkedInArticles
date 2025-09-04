#!/bin/sh
sudo service ssh restart

# Check for GPG key and gitconfig on the persistent data volume
if [ -e data/private.pgp ]; then
    gpg --import data/private.pgp
fi

if [ -e data/.gitconfig ]; then
    cp data/.gitconfig ~/.gitconfig
fi

# Generate a self-signed TLS certificate for JupyterLab if one doesn't exist
if [ ! -f "data/cert.pem" ] || [ ! -f "data/key.pem" ]; then
    echo "--- Generating self-signed TLS certificate for JupyterLab ---"
    openssl req -x509 \
      -nodes \
      -newkey rsa:4096 \
      -keyout data/key.pem \
      -out data/cert.pem \
      -sha256 \
      -days 365 \
      -subj '/CN=localhost'
fi

# Activate venv and start JupyterLab in a detached tmux session
. .venv_jupyter/bin/activate
tmux new -d -s jupyterlab "jupyter lab --ip=0.0.0.0 --port=8888 --no-browser --certfile=data/cert.pem --keyfile=data/key.pem"

# Execute the command passed to `docker run`, or default to bash
bash