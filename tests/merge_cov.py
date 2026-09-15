#!/usr/bin/env python3
"""Union line coverage across test binaries (static-lib multi-link safe)."""
from __future__ import print_function
import argparse, os, re, subprocess, sys

def export_lcov(llvm_cov, binary, profdata, ignore):
    cmd = [
        llvm_cov, 'export', binary,
        '-instr-profile=' + profdata,
        '-format=lcov',
        "--ignore-filename-regex=" + ignore,
    ]
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        sys.stderr.write('export failed for %s: %s\n' % (binary, e.stderr[:200]))
        return ''
    if isinstance(out, bytes):
        out = out.decode('utf-8', 'replace')
    return out

def parse_lcov(text):
    """Return dict path -> {line: hits} and set of found lines."""
    files = {}
    cur = None
    for line in text.splitlines():
        if line.startswith('SF:'):
            cur = line[3:].strip()
            files.setdefault(cur, {})
        elif line.startswith('DA:') and cur is not None:
            # DA:<line>,<hits>
            parts = line[3:].split(',')
            if len(parts) >= 2:
                ln = int(parts[0])
                hits = int(parts[1])
                files[cur][ln] = files[cur].get(ln, 0) + hits
        elif line.startswith('end_of_record'):
            cur = None
    return files

def merge(a, b):
    for path, lines in b.items():
        dst = a.setdefault(path, {})
        for ln, hits in lines.items():
            dst[ln] = dst.get(ln, 0) + hits
    return a

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--llvm-cov', required=True)
    ap.add_argument('--profdata', required=True)
    ap.add_argument('--ignore', required=True)
    ap.add_argument('--tests', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--threshold', type=float, default=65)
    args = ap.parse_args()

    ignore_re = re.compile(args.ignore)
    merged = {}
    for name in args.tests.split():
        path = './' + name if not name.startswith('./') else name
        if not os.path.exists(path):
            print('missing', path, file=sys.stderr)
            continue
        text = export_lcov(args.llvm_cov, path, args.profdata, args.ignore)
        if not text:
            continue
        parsed = parse_lcov(text)
        # filter paths
        filtered = {}
        for p, lines in parsed.items():
            # normalize and ignore
            if ignore_re.search(p):
                continue
            filtered[p] = lines
        merge(merged, filtered)

    rows = []
    total_lines = 0
    total_miss = 0
    for path in sorted(merged.keys()):
        lines = merged[path]
        if not lines:
            continue
        n = len(lines)
        miss = sum(1 for h in lines.values() if h == 0)
        cov = 100.0 * (n - miss) / n if n else 0.0
        rows.append((path, n, miss, cov))
        total_lines += n
        total_miss += miss

    total_cov = 100.0 * (total_lines - total_miss) / total_lines if total_lines else 0.0
    lines_out = []
    lines_out.append('%-40s %8s %8s %8s' % ('Filename', 'Lines', 'Miss', 'Cover'))
    lines_out.append('-' * 72)
    for path, n, miss, cov in rows:
        # short path
        sp = path
        for prefix in ('/Volumes/my/github/sthread/',):
            if sp.startswith(prefix):
                sp = sp[len(prefix):]
        lines_out.append('%-40s %8d %8d %7.2f%%' % (sp[-40:], n, miss, cov))
    lines_out.append('-' * 72)
    lines_out.append('%-40s %8d %8d %7.2f%%' % ('TOTAL', total_lines, total_miss, total_cov))
    lines_out.append('LINE_COVER=%.2f THRESHOLD=%s' % (total_cov, args.threshold))
    text = '\n'.join(lines_out) + '\n'
    sys.stdout.write(text)
    with open(args.out, 'w') as f:
        f.write(text)
    if total_cov + 1e-9 < args.threshold:
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
