#!/usr/bin/env python3
"""Generate the server operator table and preset manifests from the schema."""
import json
import pathlib
import sys


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    schema_path = root / "schema" / "mlu_api_schema.json"
    generated = root / "build" / "generated"
    modules = root / "modules"
    generated.mkdir(parents=True, exist_ok=True)
    modules.mkdir(parents=True, exist_ok=True)
    schema = json.loads(schema_path.read_text())
    entries = schema["operator_registry"]
    lines = [
        "#ifndef MLU_REMOTE_GENERATED_OPERATOR_REGISTRY_H",
        "#define MLU_REMOTE_GENERATED_OPERATOR_REGISTRY_H",
        "/* Generated from schema/mlu_api_schema.json; do not edit. */",
        "#define MLU_REMOTE_OPERATOR_REGISTRY(X) \\",
    ]
    ordered = sorted(entries.items(), key=lambda item: int(item[0]))
    for index, (operator_id, descriptor) in enumerate(ordered):
        suffix = " " + "\\" if index + 1 < len(ordered) else ""
        lines.append(
            f"  X({int(operator_id)}, \"{descriptor['name']}\", "
            f"\"{descriptor['kernel']}\", {int(descriptor['inputs'])}, "
            f"{int(descriptor['outputs'])}, {int(descriptor['rank'])}, "
            f"{int(descriptor['scalar_bytes'])}){suffix}"
        )
        manifest = modules / f"{descriptor['name']}.manifest"
        manifest.write_text(
            "\n".join(
                [
                    f"name={descriptor['name']}",
                    "arch=mtp_372",
                    f"symbol={descriptor['kernel']}",
                    f"inputs={int(descriptor['inputs'])}",
                    f"outputs={int(descriptor['outputs'])}",
                    f"rank={int(descriptor['rank'])}",
                    f"datatype={descriptor['datatype']}",
                    f"scalar_bytes={int(descriptor['scalar_bytes'])}",
                    "abi=mlu_remote_operator_v1",
                    "",
                ]
            )
        )
    lines.append("#endif")
    (generated / "operator_registry.h").write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
