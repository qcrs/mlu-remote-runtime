#!/usr/bin/env python3
"""Select layered regressions from changed paths."""
import argparse
import json
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("base", nargs="?", help="base git ref")
parser.add_argument("--paths", action="store_true", help="read changed paths from stdin")
args = parser.parse_args()
if args.paths:
    paths = [line.strip() for line in sys.stdin if line.strip()]
elif args.base:
    paths = subprocess.check_output(["git", "diff", "--name-only", f"{args.base}...HEAD"], text=True).splitlines()
else:
    parser.error("provide BASE or --paths")

families = set()
full = False
for path in paths:
    if path.startswith(("schema/", "compat/")):
        families.add("schema")
    if any(word in path for word in ("memory", "memcpy")):
        families.add("memory")
    if any(word in path for word in ("queue", "notifier")):
        families.add("queue_notifier")
    if any(word in path for word in ("bang", "module", "kernel")):
        families.add("module_kernel")
    if any(word in path for word in ("cndrv", "driver_queries", "native_driver")):
        families.add("device_context")
    if any(word in path for word in ("cnrt", "api_queries", "native_api_families")):
        families.add("device_context")
    if "host" in path and ("memory" in path or "completion" in path):
        families.add("host_memory")
    if any(word in path for word in ("server_session", "object_registry", "resource_limits", "remote_protocol", "transport")):
        full = True
    if any(word in path for word in ("bang_registration", "bang_metadata", "module_registry", "kernel_registry", "host_completion", "runtime_context", "mlu_backend")):
        full = True
if not families and paths:
    families.add("device_context")
levels = ["L0"] + (["L1", "L2"] if families else []) + (["L3"] if full else [])
print(json.dumps({"changed_paths": paths, "levels": levels, "affected_families": sorted(families),
                  "selected_suites": ["build", "schema_codegen_contracts"] + (["family_probe", "core_remote_smoke"] if families else []) + (["full_regression"] if full else [])}, indent=2))
