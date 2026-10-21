# Reads a vcix_timeline file and prints one line of what it holds, for tests/timeline/run.sh to compare.
# Usage: check.py json|header|issues|order|trace|ports|live <timeline> [stats.txt or gem5 log]
import json
import re
import sys


# The file's lines, parsed; a line that is not JSON is None.
def load(path):
    out = []
    with open(path) as f:
        for text in f.read().splitlines():
            try:
                out.append(json.loads(text))
            except ValueError:
                out.append(None)
    return out


# Each unit of gem5's stats.txt with its primary port and admitted, and each encoding's commit count.
def stats(path):
    units, committed = {}, {}
    for text in open(path):
        m = re.match(r"\S*\.vcix\.([^.\s]+)\.admitted\s+(\d+)\s+# \S+ admitted at port (\S+)", text)
        if m:
            units[m.group(1)] = (m.group(3), int(m.group(2)))
        m = re.match(r"\S*\.vcix\.committed\.(\S+)\s+(\d+)", text)
        if m:
            committed[m.group(1)] = int(m.group(2))
    return units, committed


# The header's units, as {unit: [port names]}, whichever form the model writes.
def header_units(head):
    if "units" in head:
        return {u["name"]: [p["name"] for p in u["ports"]] for u in head["units"]}
    units = {}
    for p in head["ports"]:
        units.setdefault(p["unit"], []).append(p["name"])
    return units


def check_json(lines):
    return "%d of %d lines JSON" % (sum(x is not None for x in lines), len(lines))


def check_header(lines, stats_path):
    head = lines[0]
    units = header_units(head)
    said = "model %s; %d units, %d encodings" % (head["model"], len(units), len(head["encodings"]))
    if not stats_path:
        return said
    named, committed = stats(stats_path)
    differ = []
    if sorted(units) != sorted(named):
        differ.append("units %s vs %s" % (sorted(units), sorted(named)))
    for unit, (port, _) in sorted(named.items()):
        if port not in units.get(unit, []):
            differ.append("no port %s.%s" % (unit, port))
    if [e.replace(" ", "_") for e in head["encodings"]] != list(committed):
        differ.append("encodings %s vs %s" % (head["encodings"], list(committed)))
    return said + (", as stats.txt names them" if not differ else "; stats.txt differs: " + "; ".join(differ))


# The issue events left once each squash drops the ids it names, in file order, and how many it dropped.
def kept_issues(lines):
    kept, dropped = [], 0
    for x in lines[1:]:
        if x.get("e") == "issue":
            kept.append(x)
        elif x.get("e") == "squash":
            dropped += sum(i["id"] >= x["from"] for i in kept)
            kept = [i for i in kept if i["id"] < x["from"]]
    return kept, dropped


def check_issues(lines, stats_path):
    kept, dropped = kept_issues(lines)
    ids = [i["id"] for i in kept]
    commits = [x["id"] for x in lines[1:] if x.get("e") == "commit"]
    said = "%d kept, committed %s" % (len(kept), "in issue order" if commits == ids else "as %s, issued %s" % (commits, ids))
    if not stats_path:
        return said
    _, committed = stats(stats_path)
    counts = {}
    for i in kept:
        counts[i["insn"].replace(" ", "_")] = counts.get(i["insn"].replace(" ", "_"), 0) + 1
    differ = [name for name, n in committed.items() if counts.get(name, 0) != n] + [n for n in counts if n not in committed]
    return said + ("; per encoding as stats.txt" if not differ else "; stats.txt differs at " + ",".join(differ))


def check_order(lines):
    return ",".join(i["insn"] for i in kept_issues(lines)[0])


def squashed(lines):
    return str(kept_issues(lines)[1])


# Each issue as the model's own trace gives it: the tpu models' name and cycles since the last issue still in
# flight or committed, or print_args's cycle and latency; the timeline's issues, every one, give the same.
def check_trace(lines, log):
    text = open(log).read()
    tpu = re.findall(r"^\[tpu\] issue (\S+): (?:the first|(\d+) cycles after the last issue)", text, re.M)
    if tpu:
        want = [(name, int(gap) if gap else None) for name, gap in tpu]
        got, live = [], []
        for x in lines[1:]:
            if x.get("e") == "squash":
                live = [i for i in live if i["id"] < x["from"]]
            elif x.get("e") == "issue":
                got.append((x["insn"], x["c"] - live[-1]["c"] if live else None))
                live.append(x)
        what = "names and gaps"
    else:
        want = [(int(c), int(r) - int(c)) for c, r in re.findall(r"^\[issue  \] .* cycle=(\d+) ready=(\d+)$", text, re.M)]
        got = [(x["c"], x["latency"]) for x in lines[1:] if x.get("e") == "issue"]
        what = "cycles and latencies"
    if got == want:
        return "%d issues, %s as the model's trace" % (len(got), what)
    first = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
    return "%d issues, %d traced; issue %d: %s, trace %s" % (
        len(got), len(want), first, got[first] if first < len(got) else None, want[first] if first < len(want) else None)


# The state at the end of each cycle, a squash's replayed line for a cycle replacing the first.
def resolved(lines):
    at = {}
    for x in lines[1:]:
        if "e" not in x:
            at[x["c"]] = x
    return [at[c] for c in sorted(at)]


def admitted(state):
    if "units" in state:
        return [n for u in state["units"] for n in u["admitted"]]
    return state["admitted"]


def check_ports(lines, stats_path):
    head = lines[0]
    names = [(u, p) for u, ports in header_units(head).items() for p in ports]
    if "ports" in head:
        names = [(p["unit"], p["name"]) for p in head["ports"]]
    states = resolved(lines)
    last, total, negative = [0] * len(names), [0] * len(names), 0
    for s in states:
        now = admitted(s)
        for k, n in enumerate(now):
            total[k] += n - last[k]
            negative += n < last[k]
        last = now
    said = "%d cycles of use below 0, %s" % (
        negative, "each port's use sums to its final admitted" if total == last else "use %s, final %s" % (total, last))
    if not stats_path:
        return said
    final = dict(zip(names, last))
    primary = sorted(stats(stats_path)[0].items())
    return said + "; primary " + " ".join("%s.%s=%s" % (u, p, final.get((u, p))) for u, (p, _) in primary)


# In file order: each id in a state line's ready or waiting is issued, not squashed, not committed; each in its queue
# commands is issued and not squashed, committed or not, as done_when_enqueued's command outlives its instruction.
def check_live(lines):
    issued, live, count, wrong = set(), set(), 0, []
    for k, x in enumerate(lines[1:], 2):
        e = x.get("e")
        if e == "issue":
            issued.add(x["id"]), live.add(x["id"])
        elif e == "commit":
            live.discard(x["id"])
        elif e == "squash":
            issued = {i for i in issued if i < x["from"]}
            live = {i for i in live if i < x["from"]}
        elif e is None:
            count += 1
            waiting, commands = list(x["ready"]), []
            for u in x["units"]:
                for q in u["queues"]:
                    commands += [c[0] for c in q["commands"]]
                    waiting += q["waiting"]
            wrong += ["line %d id %d" % (k, i) for i in waiting if i not in live]
            wrong += ["line %d command %d" % (k, i) for i in commands if i not in issued]
    said = "%d state lines" % count
    return said + (", each id named is in flight or its command queued" if not wrong else ", not in flight: " + " ".join(wrong[:5]))


def main():
    what, path = sys.argv[1], sys.argv[2]
    other = sys.argv[3] if len(sys.argv) > 3 else None
    lines = load(path)
    if what == "json":
        print(check_json(lines))
        return
    if None in lines or not lines:
        print("not JSON")
        return
    print({"header": lambda: check_header(lines, other), "issues": lambda: check_issues(lines, other),
           "order": lambda: check_order(lines), "squashed": lambda: squashed(lines),
           "trace": lambda: check_trace(lines, other), "ports": lambda: check_ports(lines, other),
           "live": lambda: check_live(lines)}[what]())


main()
