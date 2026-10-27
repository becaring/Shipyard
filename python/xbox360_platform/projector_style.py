"""Line-length check for a consumer's maintained projector scripts.

    python -m xbox360_platform.projector_style <folder> [--pattern GLOB] [--limit N]
"""

import argparse
from pathlib import Path
import sys

LIMIT = 140


def check(paths, limit=LIMIT):
    """'name:line: N characters' for every line longer than limit."""
    failures = []
    for path in paths:
        for number, line in enumerate(Path(path).read_text(encoding='utf-8-sig').splitlines(), 1):
            if len(line) > limit:
                failures.append('%s:%d: %d characters (limit %d)' % (Path(path).name, number, len(line), limit))
    return failures


def main(folder, pattern='prepare*.py', limit=LIMIT):
    files = sorted(Path(folder).glob(pattern))
    if not files:
        raise SystemExit('No projectors found')
    failures = check(files, limit)
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print('Checked %d projectors: no lines exceed %d characters' % (len(files), limit))
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--pattern', default='prepare*.py')
    parser.add_argument('--limit', type=int, default=LIMIT)
    args = parser.parse_args()
    raise SystemExit(main(args.folder, args.pattern, args.limit))
