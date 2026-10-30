#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
import json
from pathlib import Path
import re
import sys

out = Path(sys.argv[1])
status = int(sys.argv[2])
log = (out / 'boot.log').read_text(errors='replace')
required = ['PVM_HOST_UMODE_LINUX', 'Run /init as init process',
            'PVM host registered (CSR trap mode,', 'INIT-TESTS-ALL-PASS',
            'INIT-ALL-PASS', 'PVM_HOST_GUEST_SHUTDOWN']
missing = [s for s in required if s not in log]
failures = [s for s in log.splitlines() if re.search(
    r': FAIL|INIT-FAILED|INIT-TESTS-FAIL|PVM_HOST_FAIL|Kernel panic|Oops \[', s)]
match = re.search(r'PVM_HOST_STATS exits=([0-9a-f]+) faults=([0-9a-f]+) '
                  r'rebuilds=([0-9a-f]+) user_entries=([0-9a-f]+) '
                  r'timer_irqs=([0-9a-f]+) registered=([0-9a-f]+)', log)
stats = dict(zip(['exits', 'faults', 'rebuilds', 'user_entries', 'timer_irqs',
                  'registered'], [int(x, 16) for x in match.groups()])) if match else {}
ok = status == 0 and not missing and not failures and all(
    stats.get(k, 0) > 0 for k in ['exits', 'user_entries', 'timer_irqs', 'registered'])
result = dict(pass_=ok, status=status, missing=missing, failures=failures,
              checks_passed=len(re.findall(r': PASS', log)), stats=stats,
              scope='single-hart M-mode monitor; physical U-mode guest; not Linux/KVM host')
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
sys.exit(0 if ok else 1)
