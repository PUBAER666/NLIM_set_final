"""Behavioral tests: arithmetic mocks and injected PYNQ API doubles, no board."""
import argparse
import copy
import io
import json
from pathlib import Path
import tempfile
import types
import unittest

import validate_overlay as v

ROOT = Path(__file__).resolve().parents[1]


def fake_module(c, loaded=False):
    engine = v.MockBackend(c)
    engine.writes = []
    original = engine.write
    def write(addr, value):
        engine.writes.append((addr, value))
        return original(addr, value)
    engine.write = write
    props = {r['register']: property(lambda self, row=r: engine.output(row)) for r in c['outputs']}
    reg_map = type('FakeRegisterMap', (), props)()
    ip = types.SimpleNamespace(read=engine.read, write=engine.write, register_map=reg_map)
    class Overlay:
        def __init__(self, path, download=False):
            if download:
                raise AssertionError('Constructor must not program the PL')
            self.ip_dict = engine.meta
            # Real PYNQ descriptions contain unserializable device/driver values.
            self.ip_dict[c['ip']]['driver'] = object()
            self.ip_dict[c['ip']]['device'] = object()
            self.loaded = loaded
            self.downloads = 0
            owner = self
            parts = c['ip'].split('/')
            for part in parts[:-1]:
                setattr(owner, part, types.SimpleNamespace())
                owner = getattr(owner, part)
            setattr(owner, parts[-1], ip)
        def is_loaded(self):
            return self.loaded
        def download(self):
            self.downloads += 1
            self.loaded = True
    return types.SimpleNamespace(Overlay=Overlay, __version__='test_double'), engine


class ValidatorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.c = v.load_json(ROOT/'assets/add_contract.json')
        self.suite = v.load_json(ROOT/'assets/add_cases.json')
    def tearDown(self):
        self.temp.cleanup()
    def paths(self):
        contract = self.root/'contract.json';cases = self.root/'cases.json'
        contract.write_text(json.dumps(self.c), encoding='utf-8')
        cases.write_text(json.dumps(self.suite), encoding='utf-8')
        return contract, cases
    def run_mock(self, factory=None):
        contract, cases = self.paths()
        return v.execute(contract, cases, 'mock', False, None, self.root/'report', factory=factory)
    def adapter(self, loaded=False):
        bit = self.root/self.c['overlay']['bitfile'];bit.parent.mkdir(parents=True, exist_ok=True)
        bit.write_bytes(b'test fixture, not a bitstream');bit.with_suffix('.hwh').write_text('test fixture')
        module, engine = fake_module(self.c, loaded=loaded)
        return v.PynqBackend(self.c, self.root, module=module), engine
    def test_unsigned_arithmetic_and_wraparound(self):
        report = self.run_mock()
        self.assertEqual(report['status'], 'PASS')
        self.assertEqual(len(report['cases']), 5)
        self.assertFalse(report['hardware_validation_passed'])
        self.assertIn('MOCK ONLY', (self.root/'report/report.html').read_text(encoding='utf-8'))
    def test_float_arithmetic_and_different_hierarchy(self):
        self.c = v.load_json(ROOT/'assets/multiply_contract.json')
        self.suite = v.load_json(ROOT/'assets/multiply_cases.json')
        self.assertEqual(self.run_mock()['status'], 'PASS')
    def test_wrong_golden_fails_independently(self):
        self.suite['cases'][1]['expected']['sum'] = 580
        report = self.run_mock()
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['cases'][1]['outputs']['sum']['actual'], 579)
        self.assertEqual(len(report['cases']), 2)
    def test_integer_comparison_is_exact_at_high_bit(self):
        r = self.c['outputs'][0]
        self.assertFalse(v.compare(4294967294, 4294967295, r)['passed'])
    def test_fractional_integer_input_rejected_before_backend(self):
        self.suite['cases'][0]['inputs']['a'] = 1.5
        report = self.run_mock(factory=lambda _: self.fail('backend must not be created'))
        self.assertEqual(report['status'], 'ERROR')
    def test_unknown_case_key_rejected(self):
        self.suite['cases'][0]['inputs']['extra'] = 0
        self.assertEqual(self.run_mock()['status'], 'ERROR')
    def test_duplicate_case_id_rejected(self):
        self.suite['cases'][1]['id'] = self.suite['cases'][0]['id']
        self.assertEqual(self.run_mock()['status'], 'ERROR')
    def test_invalid_register_alignment(self):
        self.c['inputs'][0]['offset'] = '0x11'
        with self.assertRaises(v.ValidationError):v.contract_check(self.c)
    def test_register_collision(self):
        self.c['outputs'][0]['offset'] = '0x10'
        with self.assertRaises(v.ValidationError):v.contract_check(self.c)
    def test_integer_overflow_and_bool_rejected(self):
        for x in (2**32, -1, True):
            with self.assertRaises(v.ValidationError):v.encode(x, 'uint32')
    def test_signed_encoding(self):
        self.assertEqual(v.encode(-1, 'int32'), 0xffffffff)
        self.assertEqual(v.decode(0x80000000, 'int32'), -2147483648)
    def test_float_tolerance_and_nonfinite(self):
        r = {'dtype': 'float32', 'atol': 1e-6, 'rtol': 1e-5}
        self.assertTrue(v.compare(1.000001, 1.0, r)['passed'])
        self.assertFalse(v.compare(1.1, 1.0, r)['passed'])
        self.assertFalse(v.compare(float('nan'), 1.0, r)['passed'])
        with self.assertRaises(v.ValidationError):v.encode(float('inf'), 'float32')
    def test_metadata_mismatch_prevents_download(self):
        adapter, engine = self.adapter()
        engine.meta[self.c['ip']]['registers']['a']['address_offset'] = 0x14
        with self.assertRaises(v.ValidationError):adapter.prepare(True)
        self.assertEqual(adapter.overlay.downloads, 0)
        self.assertEqual(engine.writes, [])
    def test_missing_ip_rejected(self):
        with self.assertRaises(v.ValidationError):v.metadata_check(self.c, {})
    def test_wrong_access_rejected(self):
        b = v.MockBackend(self.c)
        b.meta[self.c['ip']]['registers']['a']['access'] = 'read-only'
        with self.assertRaises(v.ValidationError):v.metadata_check(self.c, b.meta)
    def test_wrong_control_protocol_rejected(self):
        b = v.MockBackend(self.c)
        del b.meta[self.c['ip']]['registers']['CTRL']['fields']['AP_DONE']
        with self.assertRaises(v.ValidationError):v.metadata_check(self.c, b.meta)
    def test_no_download_without_explicit_flag(self):
        adapter, engine = self.adapter()
        with self.assertRaises(v.ValidationError):adapter.prepare(False)
        self.assertEqual(adapter.overlay.downloads, 0)
        self.assertEqual(engine.writes, [])
    def test_real_adapter_api_double_and_register_map(self):
        self.c = v.load_json(ROOT/'assets/multiply_contract.json')
        self.suite = v.load_json(ROOT/'assets/multiply_cases.json')
        adapter, engine = self.adapter()
        adapter.prepare(True)
        self.assertEqual(adapter.overlay.downloads, 1)
        self.assertTrue(v.transaction(adapter, self.c, self.suite['cases'][1])['passed'])
        self.assertEqual(engine.writes[-1], (0, 1))
        json.dumps(v.clean_metadata(adapter.meta))
    def test_artifact_hash_mismatch(self):
        self.adapter()
        self.c['overlay']['bit_sha256'] = '0'*64
        module, _ = fake_module(self.c)
        with self.assertRaises(v.ValidationError):v.PynqBackend(self.c, self.root, module=module)
    def test_missing_hwh(self):
        bit = self.root/self.c['overlay']['bitfile'];bit.parent.mkdir(parents=True);bit.write_bytes(b'x')
        module, _ = fake_module(self.c)
        with self.assertRaises(v.ValidationError):v.PynqBackend(self.c, self.root, module=module)
    def test_idle_timeout_has_no_writes(self):
        self.c.update(timeout_s=0.003, poll_interval_s=0.001)
        b = v.MockBackend(self.c);b.read=lambda _:0
        writes=[];b.write=lambda *args:writes.append(args)
        with self.assertRaisesRegex(v.ValidationError, 'Pre-start idle timeout'):
            v.transaction(b, self.c, self.suite['cases'][0])
        self.assertEqual(writes, [])
    def test_completion_timeout_does_not_accept_stale_done(self):
        self.c.update(timeout_s=0.003, poll_interval_s=0.001)
        b = v.MockBackend(self.c)
        writes=[];b.write=lambda *args:writes.append(args)
        statuses=iter([6])
        b.read=lambda _: next(statuses, 0)
        report=self.run_mock(factory=lambda _: b)
        self.assertEqual(report['status'], 'ERROR')
        self.assertIn('Completion timeout', report['error']['message'])
        self.assertEqual(len(writes), 3)
        self.assertEqual(report['cases'], [])
    def test_auto_restart_rejected(self):
        b=v.MockBackend(self.c);b.read=lambda _:132
        b.write=lambda *_:self.fail('must not overwrite a running application')
        with self.assertRaisesRegex(v.ValidationError, 'AUTO_RESTART'):
            v.transaction(b, self.c, self.suite['cases'][0])
    def test_inspect_does_not_run_or_program(self):
        contract, cases=self.paths()
        b=v.MockBackend(self.c);b.prepare=lambda *_:self.fail('inspect must not prepare')
        b.write=lambda *_:self.fail('inspect must not write')
        report=v.execute(contract, cases, 'mock', False, None, self.root/'report', inspect=True, factory=lambda _:b)
        self.assertEqual(report['status'], 'INSPECTED')
        self.assertFalse(report['hardware_executed'])
    def test_hardware_run_requires_board_label_before_load(self):
        contract, cases=self.paths()
        report=v.execute(contract, cases, 'pynq', False, None, self.root/'report', factory=lambda _:self.fail('must not load'))
        self.assertEqual(report['status'], 'ERROR')
    def test_report_cannot_be_silently_overwritten(self):
        self.run_mock()
        with self.assertRaises(v.ValidationError):self.run_mock()
    def test_negative_tolerance_rejected(self):
        self.c['outputs'][0]['atol']=-1
        with self.assertRaises(v.ValidationError):v.contract_check(self.c)


if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--report',type=Path);a=p.parse_args()
    stream=io.StringIO()
    result=unittest.TextTestRunner(stream=stream,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ValidatorTests))
    print(stream.getvalue())
    summary={'passed':result.wasSuccessful(),'tests_run':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'evidence_kind':'host_unit_tests_with_pynq_api_doubles','pynq_hardware_tested':False,'cross_board_hardware_verified':False}
    if a.report:
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
