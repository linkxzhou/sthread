#!/usr/bin/env python3
"""Turn a bench_curve.sh CSV into SVG line charts and a markdown report.

Dev tool only: Python 3 standard library. Not linked into libmthread.
"""

import argparse
import csv
import io
import math
import os
import statistics
import sys


def load_csv(path):
    meta = {}
    lines = []
    with io.open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#"):
                body = line[1:].strip()
                if "=" in body:
                    key, val = body.split("=", 1)
                    meta[key.strip()] = val.strip()
                continue
            if line.strip():
                lines.append(line)
    if not lines:
        raise SystemExit("empty curve csv: %s" % path)
    reader = csv.DictReader(io.StringIO("".join(lines)))
    rows = []
    for row in reader:
        rows.append(row)
    return meta, rows


def num(text):
    if text is None:
        return None
    text = str(text).strip()
    if text == "":
        return None
    return float(text)


def is_clean(row):
    if str(row.get("exit_code", "")).strip() != "0":
        return False
    if str(row.get("server_alive", "")).strip() != "1":
        return False
    fail = num(row.get("fail"))
    ok = num(row.get("ok"))
    qps = num(row.get("qps"))
    if fail is None or ok is None or qps is None:
        return False
    if fail != 0:
        return False
    if row.get("proto") == "http":
        if num(row.get("p50_ms")) is None or num(row.get("p99_ms")) is None:
            return False
        if num(row.get("elapsed_ms")) is None:
            return False
    if row.get("proto") == "dns":
        pending = num(row.get("pending"))
        if pending is None or pending != 0:
            return False
    return True


def group_rows(rows, proto):
    grouped = {}
    for row in rows:
        if row.get("proto") != proto:
            continue
        conc = int(row["conc"])
        grouped.setdefault(conc, []).append(row)
    return grouped


def median_of(rows, key):
    vals = []
    for row in rows:
        v = num(row.get(key))
        if v is not None:
            vals.append(v)
    if not vals:
        return None
    return statistics.median(vals)


def fmt_num(v, digits=2):
    if v is None:
        return ""
    if digits == 0 or abs(v - round(v)) < 1e-9:
        return str(int(round(v)))
    return ("%." + str(digits) + "f") % v


def fmt_qps(v):
    if v is None:
        return ""
    return "%.2f" % v


def fmt_tick(v):
    if abs(v) >= 1000:
        k = v / 1000.0
        if abs(k - round(k)) < 1e-6:
            return "%dk" % int(round(k))
        return "%.1fk" % k
    if abs(v - round(v)) < 1e-6:
        return str(int(round(v)))
    return "%.1f" % v


def esc(text):
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def nice_max(value):
    if value <= 0:
        return 1.0
    exp = 0
    base = 1.0
    while base * 10 <= value:
        base *= 10
        exp += 1
    frac = value / base
    if frac <= 1:
        step = 1
    elif frac <= 2:
        step = 2
    elif frac <= 2.5:
        step = 2.5
    elif frac <= 5:
        step = 5
    else:
        step = 10
    return step * base


def y_ticks(ymax, count=5):
    ticks = []
    i = 0
    while i <= count:
        ticks.append(ymax * i / float(count))
        i += 1
    return ticks


def render_svg(path, title, subtitle, footer, xlabel, ylabel, series, log_x):
    width = 860
    height = 520
    ml, mr, mt, mb = 84, 36, 86, 78
    plot_w = width - ml - mr
    plot_h = height - mt - mb
    xs = []
    ys = []
    for serie in series:
        for x, y in serie["points"]:
            xs.append(x)
            ys.append(y)
    if not xs:
        raise SystemExit("no points for %s" % path)
    xmin = min(xs)
    xmax = max(xs)
    if xmin <= 0:
        raise SystemExit("concurrency must be positive for log x-axis")
    if xmax == xmin:
        xmax = xmin * 10
    ymax_data = max(ys) if ys else 1
    # Headroom so point labels sit inside the frame.
    ymax = nice_max(ymax_data * 1.18)
    if ymax <= ymax_data:
        ymax = nice_max(ymax_data * 1.25)

    def map_x(x):
        if log_x:
            span = math.log10(xmax) - math.log10(xmin)
            if span == 0:
                span = 1
            return ml + (math.log10(x) - math.log10(xmin)) / span * plot_w
        span = float(xmax - xmin) or 1
        return ml + (x - xmin) / span * plot_w

    def map_y(y):
        return mt + plot_h - (y / ymax) * plot_h

    parts = []
    parts.append('<?xml version="1.0" encoding="UTF-8"?>')
    parts.append(
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d" role="img">' % (width, height, width, height)
    )
    parts.append("<title>%s</title>" % esc(title))
    parts.append(
        '<rect x="0" y="0" width="%d" height="%d" fill="#ffffff"/>' % (width, height)
    )
    parts.append(
        '<text x="%d" y="28" font-family="Helvetica, Arial, sans-serif" '
        'font-size="18" font-weight="700" fill="#1a1a1a">%s</text>'
        % (ml, esc(title))
    )
    parts.append(
        '<text x="%d" y="50" font-family="Helvetica, Arial, sans-serif" '
        'font-size="13" fill="#333333">%s</text>' % (ml, esc(subtitle))
    )

    for tick in y_ticks(ymax, 5):
        y = map_y(tick)
        parts.append(
            '<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e6e6e6" '
            'stroke-width="1"/>' % (ml, y, ml + plot_w, y)
        )
        parts.append(
            '<text x="%d" y="%.1f" text-anchor="end" '
            'font-family="Helvetica, Arial, sans-serif" font-size="12" '
            'fill="#333333">%s</text>'
            % (ml - 8, y + 4, esc(fmt_tick(tick)))
        )

    parts.append(
        '<rect x="%d" y="%d" width="%d" height="%d" fill="none" '
        'stroke="#222222" stroke-width="1.25"/>' % (ml, mt, plot_w, plot_h)
    )

    # x labels at the measured concurrency values
    seen = []
    for serie in series:
        for x, _y in serie["points"]:
            if x not in seen:
                seen.append(x)
    seen.sort()
    for x in seen:
        px = map_x(x)
        parts.append(
            '<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="#222222" '
            'stroke-width="1"/>' % (px, mt + plot_h, px, mt + plot_h + 5)
        )
        parts.append(
            '<text x="%.1f" y="%d" text-anchor="middle" '
            'font-family="Helvetica, Arial, sans-serif" font-size="12" '
            'fill="#1a1a1a">%s</text>' % (px, mt + plot_h + 22, esc(fmt_num(x, 0)))
        )

    parts.append(
        '<text x="%d" y="%d" text-anchor="middle" '
        'font-family="Helvetica, Arial, sans-serif" font-size="13" '
        'fill="#1a1a1a">%s</text>'
        % ((ml + ml + plot_w) / 2, height - 36, esc(xlabel))
    )
    y_label_y = mt + plot_h / 2.0
    parts.append(
        '<text x="22" y="%.1f" text-anchor="middle" transform="rotate(-90 22 %.1f)" '
        'font-family="Helvetica, Arial, sans-serif" font-size="13" '
        'fill="#1a1a1a">%s</text>' % (y_label_y, y_label_y, esc(ylabel))
    )

    legend_x = ml + plot_w - 150
    legend_y = 18
    if len(series) > 1:
        i = 0
        for serie in series:
            lx = legend_x
            ly = legend_y + i * 16
            parts.append(
                '<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" '
                'stroke-width="2.5"/>' % (lx, ly, lx + 22, ly, serie["color"])
            )
            parts.append(
                '<circle cx="%d" cy="%d" r="3.5" fill="%s"/>'
                % (lx + 11, ly, serie["color"])
            )
            parts.append(
                '<text x="%d" y="%d" font-family="Helvetica, Arial, sans-serif" '
                'font-size="12" fill="#1a1a1a">%s</text>'
                % (lx + 28, ly + 4, esc(serie["name"]))
            )
            i += 1

    for serie in series:
        coords = []
        for x, y in serie["points"]:
            coords.append("%.1f,%.1f" % (map_x(x), map_y(y)))
        parts.append(
            '<polyline fill="none" stroke="%s" stroke-width="2.25" '
            'stroke-linejoin="round" stroke-linecap="round" points="%s"/>'
            % (serie["color"], " ".join(coords))
        )
        for x, y in serie["points"]:
            px = map_x(x)
            py = map_y(y)
            parts.append(
                '<circle cx="%.1f" cy="%.1f" r="4" fill="#ffffff" stroke="%s" '
                'stroke-width="2"/>' % (px, py, serie["color"])
            )
            if not serie.get("annotate", True):
                continue
            label = serie.get("label")
            if label:
                text = label(x, y)
            else:
                text = fmt_num(y, 0)
            ly = py + serie.get("label_dy", -12)
            if ly < mt + 14:
                ly = py + 14
            if ly > mt + plot_h - 2:
                ly = py - 12
            anchor = "middle"
            lx = px
            if px < ml + 36:
                anchor = "start"
                lx = px + 8
            elif px > ml + plot_w - 36:
                anchor = "end"
                lx = px - 8
            parts.append(
                '<text x="%.1f" y="%.1f" text-anchor="%s" '
                'font-family="Helvetica, Arial, sans-serif" font-size="11" '
                'fill="%s">%s</text>' % (lx, ly, anchor, serie["color"], esc(text))
            )

    parts.append(
        '<text x="%d" y="%d" font-family="Helvetica, Arial, sans-serif" '
        'font-size="12" fill="#333333">%s</text>' % (ml, height - 14, esc(footer))
    )
    parts.append("</svg>")
    parts.append("")

    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with io.open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(parts))


def md_cell(text):
    return str(text).replace("|", "\\|")


def write_report(path, meta, rows, embed_qps, embed_latency):
    http = group_rows(rows, "http")
    dns = group_rows(rows, "dns")

    def m(key, default="unknown"):
        return meta.get(key, default)

    lines = []
    lines.append("# HTTP concurrency curve (frozen)")
    lines.append("")
    lines.append(
        "> Frozen from a real `make bench-curve` run. **Do not edit numbers by hand.** "
        "Re-run `make bench-curve` for a timestamped `reports/curve-*` (gitignored)."
    )
    lines.append("")
    lines.append("## Environment")
    lines.append("")
    lines.append("| Item | Value |")
    lines.append("| --- | --- |")
    rows_env = [
        ("Date (UTC)", m("date")),
        ("OS", "%s %s (%s)" % (m("os"), m("kernel"), m("arch"))),
        ("CPU", "%s (nproc=%s)" % (m("cpu"), m("nproc"))),
        ("Memory", m("mem_total")),
        ("nofile", m("nofile")),
        ("Compiler", "%s (`%s`)" % (m("compiler"), m("dumpmachine"))),
        ("Commit", "`%s`" % m("commit")),
        ("Build", m("build")),
        ("TRACE", m("trace")),
        ("HTTP server", "`%s`" % m("http_bin")),
        ("HTTP client", "`%s`" % m("http_client")),
        ("URL", m("http_url")),
        ("Repeats", m("repeats")),
        ("Requests", "n = max(%s, concurrency * %s)" % (m("min_n"), m("per_coro"))),
        (
            "Client timeout",
            "%s ms per request; wall timeout %s s"
            % (m("client_timeout_ms"), m("wall_timeout_s")),
        ),
    ]
    for key, val in rows_env:
        lines.append("| %s | %s |" % (md_cell(key), md_cell(val)))
    lines.append("")
    lines.append("`ldd` http server: `%s`" % m("ldd_http", "n/a"))
    lines.append("")
    lines.append("`ldd` http client: `%s`" % m("ldd_client", "n/a"))
    lines.append("")
    lines.append("## Command")
    lines.append("")
    lines.append("Each repeat starts a fresh `st_httpserver` (stderr discarded), then:")
    lines.append("")
    lines.append("```bash")
    lines.append(
        "app/st_httpclient/st_httpclient -q -c <conc> -n <n> -t %s %s"
        % (m("client_timeout_ms"), m("http_url"))
    )
    lines.append("```")
    lines.append("")
    lines.append(
        "`st_wrk` is not the load generator: `-d` is a duration label and each "
        "worker exits after one batch, so it does not hold a sustained rate."
    )
    lines.append("")
    lines.append("## HTTP results")
    lines.append("")
    lines.append(
        "QPS, p50, and p99 are the median of the repeats. "
        "Errors are the sum of `fail` across repeats. "
        "A point is **reliable** only when every repeat exits 0, the server stays up, and `fail=0`. "
        "Unreliable points are omitted from the charts."
    )
    lines.append("")
    if embed_qps:
        lines.append("![QPS vs concurrency](%s)" % embed_qps)
        lines.append("")
    if embed_latency:
        lines.append("![latency vs concurrency](%s)" % embed_latency)
        lines.append("")
    lines.append(
        "| conc | n | repeats | QPS median | QPS min | QPS max | p50 ms | p99 ms | elapsed ms | errors | reliable |"
    )
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")

    chart_qps = []
    chart_p50 = []
    chart_p99 = []
    unreliable = []
    spread = []
    for conc in sorted(http):
        samples = http[conc]
        clean = [row for row in samples if is_clean(row)]
        fails = 0
        for row in samples:
            fv = num(row.get("fail"))
            if fv is None:
                fails += 1
            else:
                fails += int(fv)
        qps_vals = [num(row.get("qps")) for row in samples if num(row.get("qps")) is not None]
        qmin = min(qps_vals) if qps_vals else None
        qmax = max(qps_vals) if qps_vals else None
        reliable = len(clean) == len(samples) and len(samples) > 0
        n_show = samples[0].get("n", "")
        if reliable:
            qps_m = median_of(clean, "qps")
            p50_m = median_of(clean, "p50_ms")
            p99_m = median_of(clean, "p99_ms")
            el_m = median_of(clean, "elapsed_ms")
            chart_qps.append((conc, qps_m))
            chart_p50.append((conc, p50_m))
            chart_p99.append((conc, p99_m))
            if qmin is not None and qmax is not None and qmin > 0 and (qmax / qmin) >= 1.25:
                spread.append((conc, qps_m, qmin, qmax))
        else:
            qps_m = median_of(samples, "qps")
            p50_m = median_of(samples, "p50_ms")
            p99_m = median_of(samples, "p99_ms")
            el_m = median_of(samples, "elapsed_ms")
            unreliable.append(conc)
        lines.append(
            "| %s | %s | %d | %s | %s | %s | %s | %s | %s | %s | %s |"
            % (
                conc,
                n_show,
                len(samples),
                fmt_qps(qps_m),
                fmt_qps(qmin),
                fmt_qps(qmax),
                fmt_num(p50_m, 0),
                fmt_num(p99_m, 0),
                fmt_num(el_m, 0),
                fails,
                "yes" if reliable else "no",
            )
        )
    lines.append("")
    if spread:
        lines.append(
            "Shared-VM spread (still reliable, chart uses the median): "
            + "; ".join(
                "c=%s median %s min %s max %s"
                % (c, fmt_qps(med), fmt_qps(lo), fmt_qps(hi))
                for c, med, lo, hi in spread
            )
            + "."
        )
        lines.append("")
    lines.append("### Every repeat")
    lines.append("")
    lines.append(
        "| proto | conc | repeat | n | ok | fail | qps | elapsed_ms | p50_ms | p99_ms | exit | server_alive | pending |"
    )
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for row in rows:
        lines.append(
            "| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |"
            % (
                md_cell(row.get("proto", "")),
                md_cell(row.get("conc", "")),
                md_cell(row.get("repeat", "")),
                md_cell(row.get("n", "")),
                md_cell(row.get("ok", "")),
                md_cell(row.get("fail", "")),
                md_cell(row.get("qps", "")),
                md_cell(row.get("elapsed_ms", "")),
                md_cell(row.get("p50_ms", "")),
                md_cell(row.get("p99_ms", "")),
                md_cell(row.get("exit_code", "")),
                md_cell(row.get("server_alive", "")),
                md_cell(row.get("pending", "")),
            )
        )
    lines.append("")
    if unreliable:
        lines.append(
            "Unreliable HTTP concurrency (not drawn): %s."
            % ", ".join(str(c) for c in unreliable)
        )
        lines.append("")

    lines.append("## DNS")
    lines.append("")
    if not dns:
        lines.append("DNS probe did not run, or the server failed its health-check.")
        lines.append("")
    else:
        lines.append(
            "Not plotted. `st_dns` still needs `-n == -c` (one query per coroutine). "
            "Each point is a single burst, and `elapsed_ms` is a 1 ms clock, so the "
            "QPS figure is not a sustained rate. A check with `-n > -c` fails the extra queries "
            "(same-process sequential UDP); this probe does not do that."
        )
        lines.append("")
        lines.append(
            "| conc | n | repeats | QPS median | elapsed ms median | errors | pending max | reliable |"
        )
        lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
        for conc in sorted(dns):
            samples = dns[conc]
            clean = [row for row in samples if is_clean(row)]
            fails = 0
            pending_max = 0
            for row in samples:
                fv = num(row.get("fail"))
                if fv is None:
                    fails += 1
                else:
                    fails += int(fv)
                pv = num(row.get("pending"))
                if pv is not None and pv > pending_max:
                    pending_max = pv
            reliable = len(clean) == len(samples) and len(samples) > 0
            lines.append(
                "| %s | %s | %d | %s | %s | %s | %s | %s |"
                % (
                    conc,
                    samples[0].get("n", ""),
                    len(samples),
                    fmt_qps(median_of(samples, "qps")),
                    fmt_num(median_of(samples, "elapsed_ms"), 0),
                    fails,
                    fmt_num(pending_max, 0),
                    "yes" if reliable else "no",
                )
            )
        lines.append("")

    lines.append("## Caveats")
    lines.append("")
    lines.append(
        "- Loopback on a shared VM. Numbers are not comparable across machines or to a quiet bare-metal host."
    )
    lines.append(
        "- One OS thread runs the server event loop. The client is a second process, also one event-loop thread."
    )
    lines.append(
        "- HTTP connections are short. `st_httpserver` always sends `Connection: close`. "
        "The sweep does not pass `-k`. Against this server, `-k` reconnects; it is not pool reuse "
        "(keepalive pool reuse is still not implemented)."
    )
    lines.append(
        "- The server process is restarted before every HTTP repeat. Finished coroutines are not reclaimed "
        "(`StThread` pool TODO), so a long-lived server accumulates stacks and later points would measure that leak."
    )
    lines.append(
        "- Latency comes from `st_httpclient`'s millisecond clock (`p50_ms` / `p99_ms`). "
        "Sub-millisecond requests show up as 0."
    )
    lines.append(
        "- Server stderr is discarded (`/dev/null`). Close still logs `del event failed` inside the process; "
        "those lines are not kept, and they are not request failures."
    )
    lines.append(
        "- `listen` backlog is 128. A point with `fail>0` or a dead server stops the HTTP sweep at that concurrency."
    )
    lines.append(
        "- DNS rows above are bursts of one query per coroutine, not the curve."
    )
    lines.append("")
    lines.append("## Regenerating")
    lines.append("")
    lines.append("```bash")
    lines.append("make bench-curve")
    lines.append("# smaller smoke of the script only:")
    lines.append(
        "# BENCH_CURVE_CONCS='1 10' BENCH_CURVE_REPEATS=1 BENCH_CURVE_MIN_N=200 make bench-curve"
    )
    lines.append("```")
    lines.append("")

    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with io.open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    return chart_qps, chart_p50, chart_p99


def subtitle_of(meta):
    return "median of %s · %s %s · nproc=%s · commit %s · TRACE=%s" % (
        meta.get("repeats", "?"),
        meta.get("os", "?"),
        meta.get("arch", "?"),
        meta.get("nproc", "?"),
        meta.get("commit", "?"),
        meta.get("trace", "?"),
    )


def footer_of(meta):
    cpu = meta.get("cpu", "unknown")
    if len(cpu) > 48:
        cpu = cpu[:45] + "..."
    return "loopback · 1 server thread · Connection: close · %s" % cpu


def main(argv):
    parser = argparse.ArgumentParser(description="Plot bench_curve CSV to SVG")
    parser.add_argument("csv_path")
    parser.add_argument("--qps", required=True, help="QPS SVG output path")
    parser.add_argument("--latency", required=True, help="latency SVG output path")
    parser.add_argument("--md", required=True, help="markdown report path")
    parser.add_argument("--embed-qps", default="", help="markdown image path for QPS")
    parser.add_argument(
        "--embed-latency", default="", help="markdown image path for latency"
    )
    args = parser.parse_args(argv)
    meta, rows = load_csv(args.csv_path)
    qps, p50, p99 = write_report(
        args.md, meta, rows, args.embed_qps, args.embed_latency
    )
    sub = subtitle_of(meta)
    foot = footer_of(meta)
    foot_latency = foot + " · 1 ms clock"
    if not qps:
        print("no reliable HTTP points; charts not written", file=sys.stderr)
        return 1
    render_svg(
        args.qps,
        "HTTP short-conn throughput vs concurrency",
        sub,
        foot,
        "concurrency (log scale)",
        "requests / s",
        [
            {
                "name": "QPS",
                "color": "#0b5cab",
                "points": qps,
            }
        ],
        True,
    )
    render_svg(
        args.latency,
        "HTTP short-conn latency vs concurrency",
        sub,
        foot_latency,
        "concurrency (log scale)",
        "latency (ms)",
        [
            {
                "name": "p50",
                "color": "#0b5cab",
                "points": p50,
                "annotate": False,
            },
            {
                "name": "p99",
                "color": "#d35400",
                "points": p99,
                "label_dy": -12,
            },
        ],
        True,
    )
    print("charts: %s %s" % (args.qps, args.latency))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
