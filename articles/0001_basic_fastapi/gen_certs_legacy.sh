#!/usr/bin/env bash
openssl req -x509 \
  -nodes \
  -newkey rsa:4096 \
  -keyout key.pem \
  -out cert.pem \
  -sha256 \
  -days 365 \
  -subj '/CN=localhost' \
  -addext "subjectAltName = DNS:localhost,IP:127.0.0.1"
