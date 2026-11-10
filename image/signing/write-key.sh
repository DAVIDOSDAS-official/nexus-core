#!/usr/bin/env bash
# Write COSIGN_PRIVATE_KEY (a GitHub secret) to a file readable only by
# its owner, cleaned of what pasting through a browser adds: carriage
# returns, indentation, blank lines -- PEM allows none of them.
# Used by .github/workflows/weekly-image.yml, which deletes the file
# again in the same step (0.1.82: the key no longer sits on disk
# through the tests and the build).
#
#     image/signing/write-key.sh <file>
set -euo pipefail
out="${1:?usage: write-key.sh <file>}"
umask 077
printf '%s\n' "${COSIGN_PRIVATE_KEY:?COSIGN_PRIVATE_KEY is not set}" | tr -d '\r' \
    | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' | sed '/^$/d' > "${out}"
if ! grep -q 'BEGIN ENCRYPTED SIGSTORE PRIVATE KEY' "${out}" ||
   ! grep -q 'END ENCRYPTED SIGSTORE PRIVATE KEY' "${out}"; then
    rm -f "${out}"
    echo "COSIGN_PRIVATE_KEY is not a whole cosign key: it must run" >&2
    echo "from the BEGIN line to the END line, exactly as in cosign.key." >&2
    exit 1
fi
