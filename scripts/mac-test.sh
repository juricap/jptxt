#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=./build/jptxt
echo "=== selftest ==="
"$BIN" --selftest
echo "=== bench sample ==="
"$BIN" --bench testdata/sample.cpp
echo "=== bench /bin/ls (binary file, not running ls) ==="
"$BIN" --bench /bin/ls
python3 - <<'PY'
open("/tmp/jptxt-20m.txt","wb").write((b"The quick brown fox 0123456789 abcdefghijklmnopqrstuvwxyz\n")*350000)
open("/tmp/jptxt-120m.txt","wb").write((b"The quick brown fox 0123456789 abcdefghijklmnopqrstuvwxyz\n")*2100000)
import os
print("20m_bytes", os.path.getsize("/tmp/jptxt-20m.txt"))
print("120m_bytes", os.path.getsize("/tmp/jptxt-120m.txt"))
PY
echo "=== bench 20MB ==="
"$BIN" --bench /tmp/jptxt-20m.txt
echo "=== bench 120MB ==="
"$BIN" --bench /tmp/jptxt-120m.txt
rm -f /tmp/jptxt-20m.txt /tmp/jptxt-120m.txt
echo ALL_OK
