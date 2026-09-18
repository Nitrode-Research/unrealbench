#!/bin/bash
set -euo pipefail
mkdir -p /logs/verifier
if test -f /project/answer.txt && cmp -s /project/answer.txt <(printf '42\n'); then
    echo 1 > /logs/verifier/reward.txt
else
    echo 0 > /logs/verifier/reward.txt
fi
