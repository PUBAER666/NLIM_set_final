import hashlib,tempfile
from pathlib import Path
from compare_vectors import compare
from verify_delivery import verify

assert compare({'x':[1,0]},{'x':[1.00001,0]})['passed']
assert not compare({'x':[1]},{'x':[2]})['passed']
assert not compare({'x':[1]},{'x':[]})['passed']
assert not compare({'x':[1]},{'x':[float('nan')]})['passed']
assert not compare({'x':[1]},{'y':[1]})['passed']
with tempfile.TemporaryDirectory() as d:
    root=Path(d);(root/'a.txt').write_bytes(b'abc');rows=[{'path':'a.txt','sha256':hashlib.sha256(b'abc').hexdigest()}]
    assert not verify(root,rows)
    (root/'a.txt').write_bytes(b'changed');assert verify(root,rows)
    assert verify(root,[{'path':'../escape','sha256':'0'*64}])
print('PASS numeric mismatch, tolerance, shape, NaN, hashes, tampering and path containment')
