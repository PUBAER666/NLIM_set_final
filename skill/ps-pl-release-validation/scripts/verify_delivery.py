"""Check a trusted JSON list [{path,sha256,...}] against a package directory."""
from pathlib import Path
import argparse, hashlib, json

def verify(root, rows):
    root=Path(root).resolve();errors=[];seen=set()
    for row in rows:
        relative=Path(row['path']);target=(root/relative).resolve()
        if relative.is_absolute() or not target.is_relative_to(root):
            errors.append('unsafe path: '+str(relative));continue
        key=str(relative).casefold()
        if key in seen:errors.append('duplicate path: '+str(relative))
        seen.add(key)
        if not target.is_file():errors.append('missing: '+str(relative));continue
        digest=hashlib.sha256(target.read_bytes()).hexdigest()
        if digest!=row['sha256'].lower():errors.append('changed: '+str(relative))
    return errors

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--manifest',default='checksums.json');a=p.parse_args()
    errors=verify(a.root,json.loads((a.root/a.manifest).read_text(encoding='utf-8-sig')))
    print(json.dumps({'passed':not errors,'errors':errors},ensure_ascii=False,indent=2))
    raise SystemExit(bool(errors))
