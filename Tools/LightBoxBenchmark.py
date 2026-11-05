"""Resumable live-generation benchmark. Secrets stay in process memory, never in artifacts.
The UE batch reviewer supplies the exact production parser/intent validation via audit files.
"""
import argparse, concurrent.futures, json, pathlib, threading, time, urllib.request, uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = threading.Lock()

def read(p):
    return json.loads(pathlib.Path(p).read_text(encoding='utf-8-sig'))

def save(p, data):
    p=pathlib.Path(p); p.parent.mkdir(parents=True,exist_ok=True)
    tmp=p.with_suffix(p.suffix+'.tmp');tmp.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8');tmp.replace(p)

def get(url):
    return json.load(urllib.request.urlopen(url,timeout=12))

def main():
    a=argparse.ArgumentParser();a.add_argument('--out',type=pathlib.Path,required=True);a.add_argument('--system',type=pathlib.Path,required=True)
    a.add_argument('--workers',type=int,default=2);a.add_argument('--ids',default='');a.add_argument('--round',default='baseline');a.add_argument('--timeout',type=float,default=160)
    args=a.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    cases=read(ROOT/'Tools/Benchmarks/lightbox-100.json')['cases'];wanted={int(x) for x in args.ids.split(',') if x} if args.ids else None
    cases=[c for c in cases if wanted is None or c['id'] in wanted]
    prefix=args.system.read_text(encoding='utf-8-sig').split('Current scene:')[0]+'Current scene:'
    base=read(out.parent/'base-scene.json');key=get('http://127.0.0.1:64215/key')['token']
    before=get('http://127.0.0.1:64215/history')
    if not (out/'usage-before.json').exists():save(out/'usage-before.json',before)
    progress={'completed':0,'accepted':0,'errors':0}
    def run(case):
        cid=case['id'];stem=f'{cid:03d}';final=out/f'{stem}.result.json'
        if final.exists():
            with LOCK:progress['completed']+=1;progress['accepted']+=bool(read(final).get('accepted'))
            return
        current=base
        if case.get('baseCase'):
            # Each follow-up uses that round's actual result, or the frozen baseline on targeted reruns.
            source=(out if args.round=='baseline' else out.parent/'baseline')/f'{case["baseCase"]:03d}.scene.json'
            fallback=out.parent/'baseline'/source.name
            deadline=time.monotonic()+600
            while not source.exists() and not fallback.exists() and time.monotonic()<deadline:
                failed=out/f'{case["baseCase"]:03d}.result.json'
                if failed.exists():break
                time.sleep(.5)
            if source.exists() or fallback.exists():current=read(source if source.exists() else fallback)
        result={'id':cid,'round':args.round,'category':case['category'],'words':case['words'],'checks':case['checks'],'baseCase':case.get('baseCase'),'accepted':False,'attempts':[]}
        save(out/f'{stem}.base.json',current)
        messages=[{'role':'system','content':prefix+json.dumps(current,ensure_ascii=False,separators=(',',':'))},{'role':'user','content':case['words']}]
        for attempt in range(2):
            tag=f'{stem}_a{attempt}';attempt_file=out/f'{tag}.attempt.json'
            if attempt_file.exists():
                meta=read(attempt_file)
            else:
                rid=str(uuid.uuid4());meta={'id':rid,'caseId':cid,'attempt':attempt,'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'words':case['words']}
                save(out/f'{tag}.started.json',meta)
                started=time.monotonic()
                body={'model':'openclaw/default','stream':False,'max_completion_tokens':8192,'tool_choice':'none','messages':messages}
                req=urllib.request.Request('http://127.0.0.1:64216/v1/chat/completions',data=json.dumps(body,ensure_ascii=False).encode(),headers={'Content-Type':'application/json','Authorization':'Bearer '+key,'x-openclaw-session-key':'agent:main:lightbox:'+rid})
                try:
                    data=json.load(urllib.request.urlopen(req,timeout=args.timeout));save(out/f'{tag}.completion.json',data)
                    meta['seconds']=time.monotonic()-started;meta['httpStatus']=200
                except Exception as e:
                    meta.update(seconds=time.monotonic()-started,httpStatus=getattr(e,'code',None),error=f'{type(e).__name__}: {str(e)[:180]}')
                save(attempt_file,meta)
            result['attempts'].append(meta)
            if meta.get('error'):result['error']=meta['error'];break
            request={'id':tag,'caseId':cid,'words':case['words'],'completion':str(out/f'{tag}.completion.json'),'base':str(out/f'{stem}.base.json'),'checks':case['checks']}
            save(out/f'{tag}.request.json',request)
            deadline=time.monotonic()+420;audit=out/f'{tag}.audit.json'
            while not audit.exists() and time.monotonic()<deadline:time.sleep(.25)
            if not audit.exists():result['error']='UE audit not available; resume after reviewer starts';break
            review=read(audit);result['audit']=review
            if review.get('accepted'):
                save(out/f'{stem}.scene.json',read(out/f'{tag}.scene.json'));result['accepted']=True;result['acceptedAttempt']=tag;break
            if attempt==0:
                data=read(out/f'{tag}.completion.json');content=data.get('choices',[{}])[0].get('message',{}).get('content','')
                messages += [{'role':'assistant','content':content[:65536]},{'role':'user','content':'The renderer rejected the previous scene: '+review.get('diagnostic','Invalid scene')+' Correct it using ONLY listed fields and enum values. Preserve the requested composition. Output complete compact JSON only, preferably <=16 object groups. No tools or prose.'}]
        # A missing provider metric remains unknown, never silently zero.
        history=get('http://127.0.0.1:64215/history')['records'];byid={x['id']:x for x in history}
        for m in result['attempts']:
            met=byid.get(m['id']);m['meter']=met
            if met:save(out/f'{stem}_a{m["attempt"]}.meter.json',met)
        result['creditsKnown']=sum(m['meter']['credits'] for m in result['attempts'] if m.get('meter') and m['meter'].get('credits') is not None)
        result['creditsUnknown']=sum(not m.get('meter') or m['meter'].get('credits') is None for m in result['attempts'])
        save(final,result)
        with LOCK:
            progress['completed']+=1;progress['accepted']+=result['accepted'];progress['errors']+=bool(result.get('error'))
            save(out/'progress.json',progress)
            print(f'{args.round} {progress["completed"]}/{len(cases)} case={cid:03d} accepted={result["accepted"]} calls={len(result["attempts"])} known={result["creditsKnown"]:.4f}cr unknown={result["creditsUnknown"]}',flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures=[pool.submit(run,c) for c in cases]
        for f in concurrent.futures.as_completed(futures):
            try:f.result()
            except Exception as e:print('Worker error:',type(e).__name__,str(e)[:160],flush=True)
    save(out/'generation.done.json',progress);save(out/'usage-after.json',get('http://127.0.0.1:64215/history'))
    key=None

if __name__=='__main__':main()
