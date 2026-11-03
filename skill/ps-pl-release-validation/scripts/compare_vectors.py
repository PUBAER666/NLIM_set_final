"""Compare matching dictionaries of flat numeric arrays with abs+relative tolerance."""
import argparse,json,math

def compare(expected,actual,atol=1e-4,rtol=4e-5):
    if not math.isfinite(atol) or not math.isfinite(rtol) or min(atol,rtol)<0:raise ValueError('Invalid tolerance')
    errors=[];maximum=0.0
    if set(expected)!=set(actual):return {'passed':False,'errors':['output keys differ'],'max_abs':None}
    for key,ref in expected.items():
        got=actual[key]
        if not isinstance(ref,list) or not isinstance(got,list) or len(ref)!=len(got):errors.append(key+': shape mismatch');continue
        for i,(x,y) in enumerate(zip(ref,got)):
            if type(x) not in (int,float) or type(y) not in (int,float) or not math.isfinite(x) or not math.isfinite(y):errors.append(f'{key}[{i}]: nonfinite/nonnumeric');continue
            error=abs(y-x);maximum=max(maximum,error)
            if error>atol+rtol*abs(x):errors.append(f'{key}[{i}]: outside tolerance')
    return {'passed':not errors,'errors':errors,'max_abs':maximum,'atol':atol,'rtol':rtol}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('expected');p.add_argument('actual');p.add_argument('--atol',type=float,default=1e-4);p.add_argument('--rtol',type=float,default=4e-5);a=p.parse_args()
    with open(a.expected,encoding='utf-8-sig') as f:expected=json.load(f)
    with open(a.actual,encoding='utf-8-sig') as f:actual=json.load(f)
    result=compare(expected,actual,a.atol,a.rtol);print(json.dumps(result,indent=2));raise SystemExit(not result['passed'])
