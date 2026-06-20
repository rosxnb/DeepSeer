#!/usr/bin/env bash
# Generate a local CA certificate and private key for MITM interception.
# Usage: ./tools/gen_ca.sh [output_dir]

set -euo pipefail

OUT_DIR="${1:-.}"
CERT_FILE="${OUT_DIR}/ca.crt"
KEY_FILE="${OUT_DIR}/ca.key"

echo "Generating MITM CA certificate..."

openssl req -x509 -new -nodes \
    -newkey rsa:2048 \
    -sha256 \
    -days 3650 \
    -subj "/CN=DeepSeer/O=rosxnb/OU=Generic Payload Extractor with AI" \
    -keyout "${KEY_FILE}" \
    -out "${CERT_FILE}" \
    2>/dev/null

echo "CA certificate: ${CERT_FILE}"
echo "CA private key: ${KEY_FILE}"
echo ""
echo "To trust this CA on macOS:"
echo "  sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain ${CERT_FILE}"
echo ""
echo "To trust this CA on Linux:"
echo "  sudo cp ${CERT_FILE} /usr/local/share/ca-certificates/mitm-ca.crt"
echo "  sudo update-ca-certificates"
