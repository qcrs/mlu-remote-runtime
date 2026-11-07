#!/usr/bin/env python3
"""Keep the frozen PARTIAL census and reviewed decisions independently checkable."""
import collections
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from api_codegen import validate

batch = ROOT / 'evidence/closure-20261107'
baseline = json.loads((batch / 'baseline.json').read_text())
reviews = json.loads((batch / 'review.json').read_text())
schema = json.loads((ROOT / 'schema/mlu_api_schema.json').read_text())
validate(schema)

def check(condition, message):
    if not condition:
        raise SystemExit(message)

original = json.loads(subprocess.check_output(
    ['git', 'show', baseline['base_head'] + ':schema/mlu_api_schema.json'], cwd=ROOT, text=True))
check(baseline['partial_apis'] == [a for a in original['apis'] if a['status'] == 'PARTIAL_IMPLEMENTED'],
      'frozen baseline differs from source commit')
old = {a['name']: a for a in baseline['partial_apis']}
reviewed = {r['api']: r for r in reviews}
check(len(reviewed) == len(reviews) and old.keys() == reviewed.keys(),
      'each original PARTIAL must have exactly one decision')
apis = {a['name']: a for a in schema['apis']}

def evidence(value):
    check(bool(value) and value != 'not_run', 'missing closure evidence')
    for relative in value.split(';'):
        relative = relative.strip()
        path = ROOT / 'evidence' / relative if '/' in relative else ROOT / 'evidence/transparent-20261102' / relative
        check(path.is_file() and path.stat().st_size > 0, 'missing evidence: ' + str(path))

for name, r in reviewed.items():
    a = apis[name]
    note = a.get('closure_review', {})
    check(r['from'] == old[name]['status'] == note.get('baseline_status'), 'baseline status: ' + name)
    check(note.get('baseline_boundary') == old[name]['boundary'], 'baseline boundary: ' + name)
    check(r['to'] == note.get('audit_status') and r['reason'] == note.get('decision'), 'review drift: ' + name)
    if a['status'] != r['to']:
        resolution = a.get('closure_resolution', {})
        check(bool(resolution.get('summary')), 'changed audit decision needs resolution: ' + name)
        for key in ('native_test', 'remote_test'):
            evidence(resolution.get(key))
for a in schema['apis']:
    if a['status'] == 'IMPLEMENTED':
        check(bool(a.get('boundary')), 'missing implementation boundary: ' + a['name'])
        for key in ('native_test', 'remote_test'):
            evidence(a.get(key))
print('PASS closure audit:', len(reviews), 'original PARTIAL decisions;',
      dict(collections.Counter(r['to'] for r in reviews)))
print('Current statuses:', dict(collections.Counter(a['status'] for a in schema['apis'])))
