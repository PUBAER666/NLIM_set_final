"""Configurable PYNQ AXI-Lite/ap_ctrl_hs validation; stdlib-only mock mode.

No application model, fixed board address, DMA, or external-memory transactions.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import html
import importlib
import json
import math
import os
from pathlib import Path
import platform
import re
import struct
import sys
import time


class ValidationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ValidationError(message)


def integer(value):
    require(not isinstance(value, bool), 'Boolean is not an integer register value')
    if isinstance(value, str):
        return int(value, 16 if value.lower().startswith('0x') else 10)
    require(isinstance(value, int), 'Expected integer or hexadecimal string')
    return value


def offset(value):
    result = integer(value)
    require(0 <= result < 2**32 and result % 4 == 0, 'Register offset must be aligned to 4 bytes')
    return result


def identifier(value):
    require(isinstance(value, str) and re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', value), 'Invalid register/name identifier')
    return value


def encode(value, dtype):
    require(isinstance(value, (int, float)) and not isinstance(value, bool), 'Numeric scalar required')
    if dtype in ('uint32', 'int32'):
        require(isinstance(value, int), 'Integer scalar required; implicit truncation is forbidden')
        lo, hi = (0, 2**32 - 1) if dtype == 'uint32' else (-2**31, 2**31 - 1)
        require(lo <= value <= hi, 'Integer outside declared dtype range')
        return value & 0xffffffff
    require(dtype == 'float32' and math.isfinite(value), 'Finite float32 value required')
    try:
        return struct.unpack('<I', struct.pack('<f', value))[0]
    except (OverflowError, struct.error) as exc:
        raise ValidationError('Value overflows float32') from exc


def decode(bits, dtype):
    bits = int(bits) & 0xffffffff
    if dtype == 'uint32':
        return bits
    if dtype == 'int32':
        return bits if bits < 2**31 else bits - 2**32
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def load_json(path):
    def reject_constant(value):
        raise ValidationError('Non-finite JSON literal: ' + value)
    return json.loads(Path(path).read_text(encoding='utf-8-sig'), parse_constant=reject_constant)


def contract_check(c):
    require(c.get('schema_version') == 1, 'Unsupported contract schema')
    require(c.get('interface') == 'axi_lite_ap_ctrl_hs', 'Only scalar AXI-Lite ap_ctrl_hs is supported')
    require(isinstance(c.get('overlay'), dict) and isinstance(c['overlay'].get('bitfile'), str), 'overlay.bitfile required')
    require(isinstance(c.get('ip'), str) and c['ip'], 'IP instance path required')
    for part in c['ip'].split('/'):
        identifier(part)
    require(isinstance(c.get('control'), dict), 'Control description required')
    identifier(c['control']['register'])
    used = {offset(c['control']['offset'])}
    names = set()
    registers = {c['control']['register']}
    for direction in ('inputs', 'outputs'):
        require(isinstance(c.get(direction), list) and c[direction], direction + ' must be a nonempty list')
        for r in c[direction]:
            name, reg, addr = identifier(r['name']), identifier(r['register']), offset(r['offset'])
            require(name not in names and reg not in registers and addr not in used, 'Duplicate name, register, or offset')
            names.add(name); registers.add(reg); used.add(addr)
            require(r['dtype'] in ('uint32', 'int32', 'float32'), 'Unsupported dtype')
            for key in ('atol', 'rtol'):
                tol = r.get(key, 0)
                require(isinstance(tol, (int, float)) and not isinstance(tol, bool) and math.isfinite(tol) and tol >= 0, 'Invalid comparison tolerance')
                require(r['dtype'] == 'float32' or tol == 0, 'Integer outputs require exact comparison')
    # ap_ctrl_hs is deliberately one fixed protocol, not arbitrary control masks.
    timeout = c.get('timeout_s', 2.0)
    poll = c.get('poll_interval_s', 0.001)
    require(isinstance(timeout, (int, float)) and not isinstance(timeout, bool) and math.isfinite(timeout) and 0 < timeout <= 60, 'timeout_s must be in (0, 60]')
    require(isinstance(poll, (int, float)) and not isinstance(poll, bool) and math.isfinite(poll) and 0.0001 <= poll <= timeout, 'Invalid polling interval')
    for key in ('bit_sha256', 'hwh_sha256'):
        digest = c['overlay'].get(key)
        require(digest is None or (isinstance(digest, str) and re.fullmatch(r'[0-9a-fA-F]{64}', digest)), 'Invalid SHA256')
    return c


def cases_check(c, suite):
    require(suite.get('schema_version') == 1 and isinstance(suite.get('cases'), list) and suite['cases'], 'Nonempty cases required')
    seen = set()
    for case in suite['cases']:
        require(isinstance(case.get('id'), str) and case['id'] and case['id'] not in seen, 'Case IDs must be unique nonempty strings')
        seen.add(case['id'])
        for key, direction in [('inputs', 'inputs'), ('expected', 'outputs')]:
            require(isinstance(case.get(key), dict), 'Missing case ' + key)
            require(set(case[key]) == {r['name'] for r in c[direction]}, 'Case names do not match contract: ' + case['id'])
            for r in c[direction]:
                encode(case[key][r['name']], r['dtype'])
    return suite['cases']


def metadata_check(c, meta):
    require(c['ip'] in meta, 'IP not found in Overlay.ip_dict: ' + c['ip'])
    ip = meta[c['ip']]
    require(isinstance(ip.get('registers'), dict), 'HWH register metadata missing')
    if c.get('vlnv'):
        require(ip.get('type') == c['vlnv'], 'IP VLNV mismatch')
    regs = ip['registers']
    for group in ('control', 'inputs', 'outputs'):
        for r in ([c['control']] if group == 'control' else c[group]):
            require(r['register'] in regs, 'Missing HWH register: ' + r['register'])
            actual = regs[r['register']]
            require(integer(actual['address_offset']) == offset(r['offset']), 'HWH register offset mismatch: ' + r['register'])
            require(integer(actual['size']) == 32, 'Only 32-bit scalar HWH registers are supported')
            require(offset(r['offset']) + 4 <= integer(ip['addr_range']), 'Register outside IP address range')
            access = actual.get('access')
            allowed = ('read-write',) if group == 'control' else ('write-only', 'read-write') if group == 'inputs' else ('read-only', 'read-write')
            require(access in allowed, 'HWH register access mismatch: ' + r['register'])
    fields = regs[c['control']['register']].get('fields', {})
    for name, bit in [('AP_START', 0), ('AP_DONE', 1), ('AP_IDLE', 2), ('AUTO_RESTART', 7)]:
        require(name in fields and integer(fields[name]['bit_offset']) == bit and integer(fields[name]['bit_width']) == 1, 'HWH does not describe the required ap_ctrl_hs field: ' + name)
    return ip


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class PynqBackend:
    """Metadata preflight occurs before programming. No buffer/DMA allocation."""
    def __init__(self, c, config_dir, module=None):
        bit = (Path(config_dir) / c['overlay']['bitfile']).resolve()
        hwh = bit.with_suffix('.hwh')
        require(bit.suffix == '.bit' and bit.is_file() and hwh.is_file(), 'Matching .bit and same-stem .hwh files are required')
        self.artifacts = {'bitfile': str(bit), 'bit_sha256': digest(bit), 'hwh_sha256': digest(hwh)}
        for key in ('bit_sha256', 'hwh_sha256'):
            if c['overlay'].get(key):
                require(self.artifacts[key] == c['overlay'][key].lower(), 'Artifact hash mismatch: ' + key)
        self.module = module if module is not None else importlib.import_module('pynq')
        self.overlay = self.module.Overlay(str(bit), download=False)
        self.meta = self.overlay.ip_dict
        self.c = c
        self.ip = None
        self.programmed = False

    def prepare(self, download):
        metadata_check(self.c, self.meta)
        if download:
            self.overlay.download()
            self.programmed = True
        require(self.overlay.is_loaded(), 'Overlay is not loaded; use --download only with the correct board-specific bitstream')
        metadata_check(self.c, self.overlay.ip_dict)
        obj = self.overlay
        for part in self.c['ip'].split('/'):
            obj = getattr(obj, part)
        self.ip = obj
        # Construct the map, but never stringify/read all registers: AP_DONE is clear-on-read.
        self.register_map = obj.register_map
        for r in self.c['outputs']:
            require(hasattr(type(self.register_map), r['register']), 'Output register missing from register_map: ' + r['register'])

    def read(self, addr):
        return int(self.ip.read(addr))

    def write(self, addr, value):
        self.ip.write(addr, value)

    def output(self, r):
        return int(getattr(self.register_map, r['register']))

    def close(self):
        # Keep the loaded overlay resident; no active external-memory transactions exist.
        pass


class MockBackend:
    """Independent arithmetic models, never generated from expected outputs."""
    def __init__(self, c):
        self.c = c
        self.regs = {}
        self.done = False
        self.artifacts = {}
        self.programmed = False
        self.mock = c.get('mock', {})
        require(self.mock.get('operation') in ('u32_add', 'f32_multiply'), 'Mock operation must be u32_add or f32_multiply')
        require(len(c['inputs']) == 2 and len(c['outputs']) == 1, 'Arithmetic mock needs two inputs and one output')
        dtype = 'uint32' if self.mock['operation'] == 'u32_add' else 'float32'
        require(all(r['dtype'] == dtype for r in c['inputs'] + c['outputs']), 'Mock dtype mismatch')
        regs = {c['control']['register']: {'address_offset': offset(c['control']['offset']), 'size': 32, 'access': 'read-write', 'fields': {k: {'bit_offset': b, 'bit_width': 1} for k, b in [('AP_START', 0), ('AP_DONE', 1), ('AP_IDLE', 2), ('AUTO_RESTART', 7)]}}}
        for group in ('inputs', 'outputs'):
            for r in c[group]:
                regs[r['register']] = {'address_offset': offset(r['offset']), 'size': 32, 'access': 'write-only' if group == 'inputs' else 'read-only'}
        self.meta = {c['ip']: {'phys_addr': integer(self.mock.get('base_address', '0x40000000')), 'addr_range': 4096, 'type': c.get('vlnv', 'mock:mock:arithmetic:1.0'), 'registers': regs}}

    def prepare(self, download):
        require(not download, '--download is invalid in mock mode')
        metadata_check(self.c, self.meta)

    def read(self, addr):
        if addr == offset(self.c['control']['offset']):
            result = 4 | (2 if self.done else 0)
            self.done = False
            return result
        return self.regs.get(addr, 0)

    def write(self, addr, value):
        self.regs[addr] = value
        if addr == offset(self.c['control']['offset']) and value == 1:
            a, b = [decode(self.regs[offset(r['offset'])], r['dtype']) for r in self.c['inputs']]
            value = (a + b) & 0xffffffff if self.mock['operation'] == 'u32_add' else a * b
            r = self.c['outputs'][0]
            self.regs[offset(r['offset'])] = encode(value, r['dtype'])
            self.done = True

    def output(self, r):
        return self.read(offset(r['offset']))

    def close(self):
        pass


def poll(backend, c, mask, phase):
    deadline = time.monotonic() + c.get('timeout_s', 2.0)
    while True:
        status = backend.read(offset(c['control']['offset']))
        require(status & 128 == 0, 'AUTO_RESTART is active; stop the owning application first')
        if status & mask:
            return status
        if time.monotonic() >= deadline:
            raise ValidationError(f'{phase} timeout; last_control=0x{status:08x}; no automatic retry')
        time.sleep(c.get('poll_interval_s', 0.001))


def compare(actual, expected, r):
    if r['dtype'] != 'float32':
        return {'passed': actual == expected, 'actual': actual, 'expected': expected, 'absolute_error': abs(actual - expected), 'mode': 'exact_integer'}
    finite = math.isfinite(actual) and math.isfinite(expected)
    error = abs(actual - expected) if finite else None
    limit = r.get('atol', 0) + r.get('rtol', 0) * abs(expected)
    return {'passed': finite and error <= limit, 'actual': actual if finite else str(actual), 'expected': expected, 'absolute_error': error, 'allowed_error': limit, 'mode': 'atol_plus_rtol_times_expected'}


def transaction(backend, c, case):
    poll(backend, c, 4, 'Pre-start idle')  # clears any previous AP_DONE before issuing a new start
    start = time.perf_counter_ns()
    for r in c['inputs']:
        backend.write(offset(r['offset']), encode(case['inputs'][r['name']], r['dtype']))
    backend.write(offset(c['control']['offset']), 1)  # AP_START, AUTO_RESTART disabled
    poll(backend, c, 2, 'Completion')
    outputs = {r['name']: compare(decode(backend.output(r), r['dtype']), case['expected'][r['name']], r) for r in c['outputs']}
    elapsed = (time.perf_counter_ns() - start) / 1e6
    return {'id': case['id'], 'passed': all(x['passed'] for x in outputs.values()), 'outputs': outputs, 'host_observed_transaction_ms': elapsed}


def clean_metadata(meta):
    return {name: {k: data[k] for k in ('phys_addr', 'addr_range', 'type', 'registers') if k in data} for name, data in meta.items()}


def save_report(report, folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    badge = 'PYNQ hardware run' if report['backend'] == 'pynq' else 'MOCK ONLY — no PYNQ hardware'
    body = html.escape(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False))
    (folder / 'report.html').write_text('<!doctype html><meta charset="utf-8"><title>Overlay validation</title><style>body{font-family:system-ui;max-width:1000px;margin:40px auto;padding:16px}h2{background:#eef3fa;padding:15px}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style><h1>Overlay validation</h1><h2>' + badge + '</h2><p>Result: ' + report['status'] + '</p><pre>' + body + '</pre>', encoding='utf-8')


def execute(config, suite_path, mode, download, board_id, report_dir, inspect=False, factory=None):
    require(not (Path(report_dir) / 'report.json').exists(), 'Report directory already contains a run; use a new directory')
    report = {'schema_version': 1, 'utc': datetime.now(timezone.utc).isoformat(), 'backend': mode, 'operation': 'inspect' if inspect else 'run', 'status': 'ERROR', 'hardware_executed': False, 'hardware_validation_passed': False, 'board_label': board_id, 'host': {'python': platform.python_version(), 'system': platform.system(), 'machine': platform.machine()}, 'cases': [], 'timing_scope': 'Host wall time includes register I/O and polling; not accelerator cycles or speedup.'}
    backend = None
    try:
        require(mode in ('mock', 'pynq'), 'Invalid backend')
        require(not inspect or not download, 'Inspect never programs the PL')
        require(mode != 'mock' or not download, 'Mock mode cannot download a bitstream')
        report['contract_sha256'] = digest(config)
        c = contract_check(load_json(config))
        report['contract_name'] = c.get('name', '')
        cases = [] if inspect else cases_check(c, load_json(suite_path))
        if not inspect:
            report['cases_sha256'] = digest(suite_path)
        require(mode != 'pynq' or inspect or bool(board_id), '--board-id is required for a hardware run')
        backend = factory(c) if factory else (MockBackend(c) if mode == 'mock' else PynqBackend(c, Path(config).resolve().parent))
        report['artifacts'] = backend.artifacts
        report['ip_metadata'] = clean_metadata(backend.meta)
        selected = metadata_check(c, backend.meta)
        report['contract_ip'] = clean_metadata({c['ip']: selected})[c['ip']]
        if mode == 'pynq':
            report['pynq_version'] = getattr(backend.module, '__version__', 'unknown')
            model = Path('/sys/firmware/devicetree/base/model')
            report['device_tree_model'] = model.read_text().rstrip('\x00\n') if model.exists() else None
            report['board_environment'] = os.environ.get('BOARD')
        if inspect:
            report['status'] = 'INSPECTED'
        else:
            backend.prepare(download)
            report['overlay_programmed'] = backend.programmed
            for case in cases:
                report['active_case'] = case['id']
                report['hardware_executed'] = mode == 'pynq'
                result = transaction(backend, c, case)
                report['cases'].append(result)
                if not result['passed']:
                    break
            report['status'] = 'PASS' if len(report['cases']) == len(cases) and all(x['passed'] for x in report['cases']) else 'FAIL'
            report['hardware_validation_passed'] = mode == 'pynq' and report['status'] == 'PASS'
    except Exception as exc:
        report['error'] = {'type': type(exc).__name__, 'message': str(exc)}
    finally:
        if backend:
            report['overlay_programmed'] = backend.programmed
            backend.close()
        save_report(report, report_dir)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('operation', choices=['run', 'inspect'])
    p.add_argument('--contract', type=Path, required=True)
    p.add_argument('--cases', type=Path)
    p.add_argument('--backend', choices=['mock', 'pynq'], default='mock')
    p.add_argument('--download', action='store_true', help='Explicitly program the configured board-specific bitstream')
    p.add_argument('--board-id', help='Operator-provided board label; recorded separately from detected platform')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    if a.operation == 'run' and a.cases is None:
        p.error('--cases is required for run')
    try:
        r = execute(a.contract, a.cases, a.backend, a.download, a.board_id, a.out, inspect=a.operation == 'inspect')
    except (ValidationError, OSError) as exc:
        p.exit(2, str(exc) + '\n')
    print(json.dumps({'status': r['status'], 'backend': r['backend'], 'hardware_validation_passed': r['hardware_validation_passed'], 'report': str(a.out / 'report.json')}))
    return 0 if r['status'] in ('PASS', 'INSPECTED') else 1


if __name__ == '__main__':
    sys.exit(main())
