"""Generate Host-local error APIs from the reviewed native SDK catalogue."""
import json

def generate_errors(root, schema):
    catalog = json.loads((root / 'schema/error_catalog.json').read_text())
    entries = catalog['entries']
    values = [r['value'] for r in entries]
    declared = {r['value'] for rows in catalog['declarations'].values() for r in rows}
    if values != sorted(set(values)) or set(values) != declared:
        raise ValueError('error catalogue must cover the declared enum union exactly')
    files = {}
    notice = '/* Generated from reviewed schema/error_catalog.json; do not edit. */'
    for library, prefix, filename in [('CNRT', 'runtime', 'local_cnrt_errors.inc'),
                                      ('CNDrv', 'driver', 'local_cndrv_errors.inc')]:
        lines = [notice]
        for row in catalog['declarations'][library]:
            lines.append(f'_Static_assert({row["name"]} == {row["value"]}, "SDK error enum drift");')
        for api in schema['apis']:
            spec = api.get('local_error')
            if api['library'] != library or not spec:
                continue
            if api['implementation'] != 'generated_local' or api.get('wrapper'):
                raise ValueError('local catalogue API cannot also have an RPC wrapper')
            field = spec['field']
            if field not in ('name', 'description'):
                raise ValueError('invalid local error field')
            key = prefix + '_' + field
            if library == 'CNRT':
                lines += [f'const char *{api["name"]}(cnrtRet_t error) {{', '  switch ((uint32_t)error) {']
            else:
                lines += [f'CNresult {api["name"]}(CNresult error, const char **pStr) {{',
                          '  if (!pStr) return CN_ERROR_INVALID_VALUE;', '  switch ((uint32_t)error) {']
            for row in entries:
                text = json.dumps(row[key], ensure_ascii=True)
                action = f'return {text};' if library == 'CNRT' else f'*pStr={text}; return CN_SUCCESS;'
                lines += [f'    case {row["value"]}: {action}']
            text = json.dumps(catalog['fallback'][key], ensure_ascii=True)
            if library == 'CNRT':
                high = json.dumps(catalog['runtime_fallback'][key], ensure_ascii=True)
                text = f'((int32_t)error <= {catalog["runtime_driver_fallback_max"]} ? {text} : {high})'
            action = f'return {text};' if library == 'CNRT' else f'*pStr={text}; return CN_SUCCESS;'
            lines += [f'    default: {action}', '  }', '}']
        files[filename] = lines
    # Probe inputs come from enum declarations, never from the output strings.
    probe = [notice, 'static const int sdk_error_codes[] = {',
             *[f'  {value},' for value in values],
             *[f'  ({row["value"]}LL),' for row in catalog['unknown_samples']], '};']
    files['sdk_error_codes.inc'] = probe
    return files
