"""Display-only bridge: board JSON and user commands. No raw stream or model code."""
import argparse
import ipaddress
import json
import secrets
import socket
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import webbrowser
import threading
import csv
import sys
from datetime import datetime
LINK_LOCK=threading.Lock()

def exchange(host,port,command=None):
    with socket.create_connection((host,port),timeout=2) as s:
        s.settimeout(2)
        deadline=time.monotonic()+2
        if command is not None:s.sendall((command+'\n').encode('ascii'))
        data=bytearray()
        while b'\n' not in data:
            remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError('开发板响应超时')
            s.settimeout(remaining)
            chunk=s.recv(2048)
            if not chunk:raise ValueError('开发板未返回完整结果')
            data.extend(chunk)
            if len(data)>65536:raise ValueError('结果超过协议长度')
        result=json.loads(data.split(b'\n')[0].decode('utf-8'))
        if not isinstance(result,dict):raise ValueError('板端协议格式错误')
        return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--ip',default='192.168.1.10');parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--no-browser',action='store_true');parser.add_argument('--recordings',type=Path)
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args();ipaddress.IPv4Address(args.ip)
    if args.check:
        try:
            value=exchange(args.ip,9001)
            if (value.get('firmware')!='NLIM_set_fianl' or value.get('firmware_revision')!='NLIM_set_fianl'):raise ValueError('请使用本包 SD_CARD 中的 BOOT.BIN，并重新断电上电')
            if not value.get('pl_inference_available') or not value.get('inference',{}).get('selftest_passed'):raise ValueError('板端 CNN 自检未通过，请检查配套 BOOT 与串口日志')
            print('连接成功：'+value['firmware_revision']+'；PL 推理自检通过；SD：'+value.get('dorm',{}).get('store','未知'))
            return
        except (OSError,ValueError) as e:
            print('检查未通过：'+str(e));raise SystemExit(2)
    base=Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parent.parent
    recording=(args.recordings or base/'recordings')/(datetime.now().strftime('%Y%m%d_%H%M%S')+'_'+secrets.token_hex(3))
    recording.mkdir(parents=True,exist_ok=False)
    log_lock=threading.Lock();seen=set();last_uptime=0;boot=0;last_serial=None
    def record(value):
        nonlocal boot,last_uptime,last_serial
        with log_lock:
            uptime=value.get('uptime_ms',0)
            serial=value.get('boot_serial')
            if (serial is not None and serial!=last_serial) or uptime<last_uptime:boot+=1
            last_serial=serial
            last_uptime=uptime
            timestamp=datetime.now().isoformat(timespec='seconds')
            with (recording/'snapshots.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps({'received_at':timestamp,'board':value},ensure_ascii=False,allow_nan=False)+'\n')
            path=recording/'events.csv';new=not path.exists()
            with path.open('a',encoding='utf-8-sig' if new else 'utf-8',newline='') as f:
                w=csv.writer(f)
                if new:w.writerow(['received_at','boot','record_id','uptime_ms','kind','name','power_w','event_id'])
                for e in reversed(value.get('dorm',{}).get('events',[])):
                    key=(boot,e['id'])
                    if key not in seen:
                        seen.add(key);w.writerow([timestamp,boot,e['id'],e['ms'],e['kind'],e['name'],e['power_w'],e['event']])
    token=secrets.token_hex(24);origin=f'127.0.0.1:{args.port}'
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def send(self,status,data,kind='application/json; charset=utf-8'):
            if isinstance(data,dict):data=json.dumps(data,ensure_ascii=False,allow_nan=False).encode('utf-8')
            self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)))
            self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(data)
        def allowed(self):return self.headers.get('Host')==origin
        def do_GET(self):
            if not self.allowed():return self.send(403,{'error':'HOST'})
            if self.path=='/':
                page=Path(__file__).with_name('dorm_demo.html').read_text(encoding='utf-8').replace('__TOKEN__',token)
                return self.send(200,page.encode('utf-8'),'text/html; charset=utf-8')
            if self.path!='/snapshot':return self.send(404,{'error':'NOT_FOUND'})
            if self.headers.get('X-Nilm-Token')!=token:return self.send(403,{'error':'TOKEN'})
            try:
                with LINK_LOCK:value=exchange(args.ip,9001)
                if (value.get('firmware')!='NLIM_set_fianl' or value.get('firmware_revision')!='NLIM_set_fianl') or 'board_compute' not in value:
                    raise ValueError('固件不支持板端计算协议；请勿配用旧演示版 BOOT')
                record(value)
                self.send(200,value)
            except (OSError,ValueError) as e:self.send(502,{'error':str(e)})
        def do_POST(self):
            if not self.allowed() or self.headers.get('X-Nilm-Token')!=token:return self.send(403,{'error':'TOKEN_OR_HOST'})
            if self.path!='/command':return self.send(404,{'error':'NOT_FOUND'})
            try:
                length=int(self.headers.get('Content-Length','0'))
                if not 0<length<=1024:raise ValueError('请求长度无效')
                body=json.loads(self.rfile.read(length))
                if body.get('action')=='factor':
                    factor=float(body['value'])
                    if not 0<=factor<=5:raise ValueError('碳因子范围为 0～5 kgCO₂/kWh')
                    command=f'FACTOR {factor:.12g}'
                elif body.get('action')=='learn':
                    label=body['name'].strip();raw=label.encode('utf-8');event=int(body['event'])
                    if not 1<=len(raw)<=63 or any(ord(c)<32 or c in '"\\' for c in label) or not 0<event<=0xffffffff:raise ValueError('名称或事件编号无效')
                    command=f'ENROLL {event} {raw.hex()}'
                elif body.get('action') in ('delete_profile','clear_profiles'):
                    if body.get('confirmed') is not True:raise ValueError('请先确认删除登记')
                    revision=body['revision']
                    if type(revision) is not int or not 0<=revision<=0xffffffff:raise ValueError('登记版本无效，请刷新后重试')
                    if body['action']=='delete_profile':
                        label=body['name'].strip();raw=label.encode('utf-8')
                        if not 1<=len(raw)<=63 or any(ord(c)<32 or c in '\\"' for c in label):raise ValueError('名称无效')
                        command=f'REGDELETE {revision} {raw.hex()}'
                    else:command=f'REGCLEAR {revision} CONFIRM'
                elif body.get('action')=='config':
                    limit=float(body['limit_w']);confirm=float(body['confirm_s']);ratio=float(body['clear_ratio']);scale=float(body['support_scale'])
                    if not(0<=limit<=20000 and .04<=confirm<=30 and .5<=ratio<=.99 and scale==1):raise ValueError('设置超出范围')
                    if not isinstance(body['unknown_enabled'],bool):raise ValueError('未知检测开关无效')
                    command=f"CONFIG {limit:.12g} {confirm:.12g} {ratio:.12g} {int(body['unknown_enabled'])} {scale:.12g}"
                elif body.get('action')=='rule':
                    label=body['name'].strip();raw=label.encode('utf-8')
                    if not 1<=len(raw)<=63 or any(ord(c)<32 or c in '\\"' for c in label):raise ValueError('名称无效')
                    if not isinstance(body['add'],bool):raise ValueError('名单操作无效')
                    command=f"RULE {int(body['add'])} {raw.hex()}"
                elif body.get('action')=='ack':
                    ident=int(body['id'])
                    if not 0<ident<=0xffffffff:raise ValueError('记录编号无效')
                    command=f'ACK {ident}'
                elif body.get('action')=='learning':command='LEARNING' 
                else:raise ValueError('不支持的指令')
                with LINK_LOCK:result=exchange(args.ip,9002,command)
                with log_lock:
                    with (recording/'commands.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps({'received_at':datetime.now().isoformat(timespec='seconds'),'request':body,'board_reply':result},ensure_ascii=False,allow_nan=False)+'\n')
                self.send(200,result)
            except (OSError,ValueError,KeyError,TypeError) as e:self.send(400,{'error':str(e)})
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    print(f'Recordings: {recording}')
    print(f'展示页面：http://{origin}；计算均由开发板完成。Ctrl+C 关闭展示，不停止开发板。')
    if not args.no_browser:webbrowser.open(f'http://{origin}')
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()
if __name__=='__main__':main()
