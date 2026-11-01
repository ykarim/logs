#!/bin/sh
set -eu
# Each guide is an executable transcript. Check meaningful outcomes, not every
# trace line: transport IDs and extra messages are not application guarantees.
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
for source in experiments/*.lab; do
    name=$(basename "$source" .lab)
    ./build/consensus-lab --script "$source" > "$task_tmp/$name"
done
python3 - "$task_tmp" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
for path in root.iterdir():
    errors = [line for line in path.read_text().splitlines() if line.startswith('! ')]
    expected = ['! WAIT: session needs applied >= 1; sync or use a caught-up node.'] if path.name.startswith('02-') else []
    assert errors == expected, (path.name, errors)
checks = {
    '01-first-decision': ['PASS: linearizability', 'result=true'],
    '02-stale-read': ['VIOLATION: linearizability', 'session read n3: checkout=true'],
    '03-preferred-node-failure': ['r1 SUCCESS: ok', 'phase 2'],
    '04-concurrent-cas': ['result=swapped', 'result=not-swapped', 'PASS: linearizability'],
    '05-unknown-outcome': ['PENDING/UNKNOWN', 'checkout=true', 'r1 SUCCESS: ok'],
    '06-conflicting-writes': ['VIOLATION: linearizability', 'n3 up=1 applied=0 events=2 buffered=0 {route=east }'],
    '07-causes-before-effects': ['n2 buffers r2: waiting for cause r1', 'n2 up=1 applied=0 events=2 buffered=0 {destination=east enabled=true }'],
    '08-majority-minority': ['PENDING/UNKNOWN', 'r2 SUCCESS: ok', 'PASS: linearizability'],
}
for name, snippets in checks.items():
    text = (root/name).read_text()
    for snippet in snippets:
        assert snippet in text, (name, snippet)
print('PASS: all 8 experiment transcripts and their expected outcomes')
PY
