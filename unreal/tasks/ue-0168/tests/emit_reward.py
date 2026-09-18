"""Emit a Harbor reward only from a completed, valid native verifier report."""
import json
import math
from pathlib import Path
import sys


def checked_reward(report, returncode):
    reward = report.get('reward')
    if (returncode not in (0, 1) or report.get('reward_valid') is not True
            or report.get('diagnostic_only') or report.get('infrastructure_errors')
            or isinstance(reward, bool) or not isinstance(reward, (int, float))
            or not math.isfinite(reward) or not 0 <= reward <= 1):
        raise ValueError('Native verification is incomplete or invalid; no reward emitted')
    return float(reward)


def main():
    report_path, reward_path = map(Path, sys.argv[1:3])
    reward_path.unlink(missing_ok=True)
    try:
        reward = checked_reward(json.loads(report_path.read_text(encoding='utf-8')), int(sys.argv[3]))
    except (OSError, ValueError) as error:
        print(f'VERDICT: {error}', file=sys.stderr)
        return 2
    reward_path.write_text(f'{reward:.6f}\n', encoding='utf-8')
    print(f'VERDICT: complete native verification; reward {reward:.6f}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
