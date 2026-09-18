"""Check a Docker host or prove native regrading without AWS or model access."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

try:
    from scripts.handoff_worker import doctor
except ModuleNotFoundError:
    from handoff_worker import doctor

ROOT = Path(__file__).resolve().parents[1]


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=['check','smoke'])
    parser.add_argument('--output',type=Path,help='New output directory for smoke results')
    args=parser.parse_args(argv)
    try:
        host=doctor()
        print(json.dumps(host,indent=2),flush=True)
        if args.operation == 'check': return 0
        output=(args.output or ROOT/'jobs'/('regrade-smoke-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8])).resolve()
        if output.exists():
            raise ValueError(f'Choose a new output directory: {output}')
        env=os.environ.copy()
        env['PATH']=str(Path(sys.executable).parent)+os.pathsep+env.get('PATH','')
        print(f'Smoke results: {output}',flush=True)
        result=subprocess.run(['bash',str(ROOT/'scripts/smoke_regrade_on_host.sh'),str(ROOT/'examples/regrade-smoke'),str(output)],env=env)
        if result.returncode:
            print(f'Smoke failed. Inspect source.log/replay.log in {output}',file=sys.stderr)
        return result.returncode
    except (OSError,RuntimeError,ValueError,subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}',file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
