"""Offline worker handshake, all retained classes, and generic workshop HID routing."""
from __future__ import annotations
import argparse
import json
import mmap
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--engine', type=Path, default=ROOT)
    p.add_argument('--python', type=Path, default=Path(sys.executable))
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    definitions = {c['class']: c for c in json.loads((ROOT/'native/resources/characters.json').read_text(encoding='utf-8'))['characters']}
    def party(names):
        return [dict(index=i, canonical=definitions[n]['canonical'], **{'class': n},
                    element=definitions[n]['element'], role=['MainDps','SubDps','Healer'][definitions[n]['role']],
                    buff=definitions[n]['buff_time']) for i,n in enumerate(names)]
    result = dict(loaded=[], requests=0, input_events=[], hardware_input_sent=False)
    mapping = 'Local\\OKWW-workshop-test-'+uuid.uuid4().hex
    memory = mmap.mmap(-1,64*1024*1024,tagname=mapping)
    frame = bytes(1920*1080*3)
    memory[:len(frame)] = frame
    with tempfile.TemporaryDirectory(prefix='okww-workshop-test-') as temporary:
        data = Path(temporary)
        shutil.copy2(a.data/'dualpc.json',data/'dualpc.json')
        shutil.copytree(a.data/'configs-dualpc',data/'configs-dualpc')
        env=dict(os.environ,PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1')
        log = a.report.with_suffix('.log').open('w',encoding='utf-8')
        proc = subprocess.Popen([str(a.python),str(a.engine/'native/bridge/workshop_worker.py'),
                    '--root',str(a.engine),'--data',str(data),'--mapping',mapping],
                    stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,encoding='utf-8',env=env)
        inbox = queue.Queue()
        def read():
            for line in proc.stdout: inbox.put(line)
            inbox.put(None)
        threading.Thread(target=read,daemon=True).start()
        keys=set(); buttons=0
        def box(): return dict(connected=True,keyboard=True,keys=sorted(keys),modifiers=0,buttons=buttons,absolute=False)
        def emit(value): proc.stdin.write(json.dumps(value)+'\n'); proc.stdin.flush()
        def receive():
            nonlocal buttons
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                line=inbox.get(timeout=max(.01,deadline-time.monotonic()))
                if line is None: raise RuntimeError('Worker exited; see '+str(a.report.with_suffix('.log')))
                q=json.loads(line)
                if q.get('event')!='rpc': return q
                request=q['request'];op=request['op'];result['requests']+=1
                if op=='frame': reply=dict(available=True,size=len(frame),width=1920,height=1080,stride=5760,format='RGB3',sequence=1)
                elif op in {'state','connect','heartbeat','probe'}: reply=dict(capture_connected=True,armed=True,box=box(),actual={})
                elif op=='release': keys.clear();buttons=0;reply={}
                elif op=='key':
                    (keys.add if request['down'] else keys.discard)(request['code']);reply=box();result['input_events'].append(request)
                elif op=='button':
                    bit={'left':1,'right':2,'middle':4}[request['name']]
                    buttons=buttons|bit if request['down'] else buttons&~bit;reply=box();result['input_events'].append(request)
                elif op in {'move','absolute','wheel'}: reply=box();result['input_events'].append(request)
                else: raise AssertionError('Unexpected RPC '+op)
                emit(dict(ok=True,data=reply))
            raise TimeoutError('Worker response timed out')
        def command(q):
            a.report.with_suffix('.progress.json').write_text(json.dumps(q, ensure_ascii=False), encoding='utf-8')
            emit(q);reply=receive()
            if reply.get('event')!='reply' or not reply.get('ok'): raise AssertionError(reply)
            return reply['data']
        try:
            assert receive()==dict(event='ready')
            teams=sorted((data/'configs-dualpc/custom_teams').iterdir())
            for folder in teams:
                if not folder.is_dir():continue
                names=folder.name.split('__')
                reply=command(dict(op='load',party=party(names)))
                assert len(reply['chars'])==3
                command(dict(op='reset'))
                result['loaded'].append(names)
            # An ordinary newly imported Python class, absent from every native
            # personal DLL, must execute through the same bridge without compiling.
            names=['Baizhi','Mortefi','Verina']
            folder=data/'configs-dualpc/custom_teams'/'__'.join(names);folder.mkdir()
            for name in names:
                code=f'from src.char.{name} import {name} as Native\nclass {name}(Native):\n    def do_perform(self):\n        self.task.send_key("e", down_time=.02)\n        self.task.mouse_down()\n        self.task.mouse_up()\n'
                (folder/(name+'.py')).write_text(code,encoding='utf-8')
                (folder/(name+'.import.py')).write_text(code,encoding='utf-8')
            (folder/'team.json').write_text(json.dumps(dict(team=', '.join(names),code_mode='import')),encoding='utf-8')
            command(dict(op='load',party=party(names)))
            command(dict(op='reset'))
            reply=command(dict(op='perform',index=0,current=0,use_liberation=True))
            assert not reply.get('interrupted')
            assert any(e['op']=='key' and e['code']==8 and e['down'] for e in result['input_events'])
            assert any(e['op']=='button' and e['down'] for e in result['input_events'])
            assert not keys and buttons==0
            result.update(ok=True,new_python_team_executed=True,all_input_released=True)
        finally:
            proc.kill();proc.wait(timeout=5);log.close();memory.close()
    a.report.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(ok=True,loaded_teams=len(result['loaded']),new_python_team_executed=True,hardware_input_sent=False)))


if __name__=='__main__':main()
