from pathlib import Path
import ctypes as C,json,os
import numpy as np
import pandas as pd
R=Path(__file__).resolve().parents[2];P=R/'data/golden'

if os.name=='nt' and os.environ.get('NILM_GCC_BIN'):os.add_dll_directory(os.environ['NILM_GCC_BIN'])
lib=C.CDLL(str(R/'sim/results/integrated.dll'))
dp=np.ctypeslib.ndpointer(np.float64,flags='C_CONTIGUOUS');fp=np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS')
lib.run_physical.argtypes=[dp,dp,dp,fp,dp];lib.register_integration.argtypes=[dp,fp,C.c_uint]
vectors=json.loads((P/'golden_vectors.json').read_text())
def run(vector):
    wave=pd.read_csv(P/vector['physical_csv']);features=np.zeros(5);embedding=np.zeros(64,dtype=np.float32);result=np.zeros(6)
    assert lib.run_physical(wave.Voltage_V.to_numpy().copy(),wave.Current_A.to_numpy().copy(),features,embedding,result)==0
    np.testing.assert_allclose(features,vector['features5'],rtol=1e-10,atol=1e-9)
    np.testing.assert_allclose(embedding,vector['embedding64'],rtol=4e-5,atol=2e-4)
    return result
lib.init_integration();results=[]
for v in vectors:
    result=run(v);assert result[0]==(0 if v['expected']['identification_status']=='known' else 1)
    results.append(dict(name=v['name'],state=int(result[0]),label=int(result[1]),score=result[2],probability=result[3]))
scope=vectors[1]
assert lib.register_integration(np.asarray(scope['standardized5']),np.asarray(scope['embedding64'],dtype=np.float32),1)==1
after=run(scope);assert after[0]==0 and after[1]==14 and after[4]==1
known=run(vectors[0]);assert known[0]==0 and known[1]==8
report=dict(passed=True,physical_pipeline=results,registered_scope_known=True,base_known_protected=True,hardware_connected=False)
(R/'sim/results/integrated_numeric.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
