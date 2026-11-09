#!/usr/bin/env python3
"""Generate an API behavior-coverage audit from source call sites and the ledger."""
import argparse
import collections
import csv
import io
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLASSES = ("DIRECT_BEHAVIOR_TEST", "WORKLOAD_COVERED", "FAMILY_TEST", "CONTRACT_ONLY")


def code_text(path):
    text = path.read_text(errors="replace")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
    return text


def expand_macros(text):
    definitions = []
    for match in re.finditer(r"^\s*#\s*define\s+(\w+)\s*\(([^)]*)\)\s*(.*?)\s*$", text, re.M):
        name, parameters, body = match.groups()
        definitions.append((name, [item.strip() for item in parameters.split(",") if item.strip()], body))
    for _ in range(8):
        before = text
        for name, parameters, body in definitions:
            if not parameters or "##" not in body:
                continue
            pattern = re.compile(r"\b" + re.escape(name) + r"\(\s*([A-Za-z_]\w*)\s*\)")
            text = pattern.sub(lambda match: body.replace("##", "").replace(parameters[0], match.group(1)), text)
        if text == before:
            break
    text = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', " ", text)
    return text


def evidence_paths(value):
    result = []
    for item in (value or "").split(";"):
        item = item.strip()
        if item:
            result.append(item)
    return result


def resolve_evidence(item):
    return ROOT / "evidence" / item if "/" in item else ROOT / "evidence/transparent-20261102" / item


def successful_workload_runs():
    successful = collections.defaultdict(set)
    for path in sorted((ROOT / "evidence").rglob("results.json")):
        try:
            data = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        entries = data if isinstance(data, list) else data.get("results", []) if isinstance(data, dict) else []
        if not isinstance(entries, list) or (isinstance(data, dict) and data.get("status") not in (None, "PASS")):
            continue
        modes = collections.defaultdict(set)
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            metrics = entry.get("metrics") or {}
            if (entry.get("workload") and entry.get("mode") and entry.get("returncode") == 0
                    and isinstance(metrics, dict) and metrics.get("RESULT") == "PASS"):
                modes[entry["workload"]].add(entry["mode"])
        for workload, observed in modes.items():
            if "native" in observed and observed.intersection({"replacement", "preload"}):
                successful[workload].add(str(path.relative_to(ROOT / "evidence")))
    return successful


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated audit files are stale")
    args = parser.parse_args()

    schema = json.loads((ROOT / "schema/mlu_api_schema.json").read_text())
    with (ROOT / "compat/api-ledger.csv").open(newline="") as stream:
        ledger = {(row["family"], row["api"]): row for row in csv.DictReader(stream)}
    apis = [api for api in schema["apis"] if api["status"] == "IMPLEMENTED"]
    if len(apis) != 283:
        raise SystemExit(f"expected 283 IMPLEMENTED APIs, found {len(apis)}")

    tests = {}
    workloads = {}
    runtime_workloads = successful_workload_runs()
    # Native probes are executable behavior checks too.  Keep them in the
    # direct-test pool so APIs exercised by a separate-process IPC probe are
    # not downgraded to contract-only merely because the probe lives outside
    # tests/.
    for source_dir in ("tests", "probes"):
        for path in sorted((ROOT / source_dir).rglob("*")):
            if path.suffix not in (".c", ".h", ".mlu", ".cpp", ".cc"):
                continue
            relative = str(path.relative_to(ROOT))
            tests[relative] = expand_macros(code_text(path))
    for path in sorted((ROOT / "workloads").rglob("*")):
        if path.suffix not in (".c", ".h", ".mlu", ".cpp", ".cc"):
            continue
        relative = str(path.relative_to(ROOT))
        workloads[relative] = expand_macros(code_text(path))

    rows = []
    family_sources = collections.defaultdict(set)
    test_calls = {}
    workload_calls = {}
    for api in apis:
        name = api["name"]
        pattern = re.compile(r"\b" + re.escape(name) + r"\s*\(")
        test_calls[name] = sorted(path for path, source in tests.items() if pattern.search(source))
        workload_calls[name] = sorted(path for path, source in workloads.items() if pattern.search(source))
        if test_calls[name]:
            family_sources[(api["library"], api["family"])].update(test_calls[name])

    for api in apis:
        name = api["name"]
        family = api["library"]
        verified_workload_calls = [
            path for path in workload_calls[name]
            if pathlib.Path(path).stem in runtime_workloads
        ]
        workload_evidence = sorted({
            evidence
            for path in verified_workload_calls
            for evidence in runtime_workloads[pathlib.Path(path).stem]
        })
        entry = ledger.get((family, name))
        if not entry or entry["status"] != "IMPLEMENTED":
            raise SystemExit("compatibility ledger/schema mismatch for " + name)
        native = evidence_paths(entry["native_test"])
        remote = evidence_paths(entry["remote_test"])
        if not native or not remote:
            raise SystemExit("missing Native/Remote evidence for " + name)
        for item in native + remote:
            if not resolve_evidence(item).is_file():
                raise SystemExit("missing evidence file for " + name + ": " + item)

        if verified_workload_calls:
            coverage = "WORKLOAD_COVERED"
            sites = verified_workload_calls
            reason = "The API has an exact workload call site and a successful Native plus Remote workload run."
        elif test_calls[name]:
            coverage = "DIRECT_BEHAVIOR_TEST"
            sites = test_calls[name]
            reason = "The API has an exact call site in a native behavior test or probe."
        elif family_sources[(family, api["family"])]:
            coverage = "FAMILY_TEST"
            sites = sorted(family_sources[(family, api["family"])])
            reason = "The API has ledger evidence and its family has behavior-test call sites, but this API has no exact source call site."
        else:
            coverage = "CONTRACT_ONLY"
            sites = []
            reason = "No exact test or workload call site was found; keep the existing ledger evidence at contract level."
        if workload_calls[name] and not verified_workload_calls:
            reason += " Workload call sites exist but have no successful Native plus Remote evidence."
        rows.append({
            "family": family,
            "api": name,
            "feature_family": api["family"],
            "status": api["status"],
            "coverage": coverage,
            "call_sites": "; ".join(sites),
            "workload_call_sites": "; ".join(workload_calls[name]),
            "workload_runtime_evidence": "; ".join(workload_evidence),
            "native_evidence": "; ".join(native),
            "remote_evidence": "; ".join(remote),
            "classification_reason": reason,
        })

    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=list(rows[0]), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    counts = collections.Counter(row["coverage"] for row in rows)
    summary = {
        "scope": "All 283 IMPLEMENTED public CNRT/CNDrv APIs in compat/api-ledger.csv",
        "classification_method": "Exact source call sites, including token-pasting family aliases, joined to Native/Remote ledger evidence. WORKLOAD_COVERED additionally requires successful Native and Remote workload entries in one passing results.json; missing runtime evidence is never inferred from compilation or source calls.",
        "successful_workload_runs": {name: sorted(paths) for name, paths in sorted(runtime_workloads.items())},
        "counts": {key: counts.get(key, 0) for key in CLASSES},
        "total": len(rows),
    }
    outputs = {
        ROOT / "compat/behavior-coverage.csv": output.getvalue(),
        ROOT / "compat/behavior-coverage.json": json.dumps(summary, indent=2) + "\n",
    }
    for path, content in outputs.items():
        if args.check:
            if not path.exists() or path.read_text() != content:
                raise SystemExit("out of date: " + str(path.relative_to(ROOT)))
        else:
            path.write_text(content)
    print(json.dumps(summary["counts"], sort_keys=True))


if __name__ == "__main__":
    main()
