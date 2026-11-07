"""Summarise an in-app model comparison (-LightBoxBench=<dir>): credits from token usage, montages per prompt.
Prices: GitHub Copilot per-token rates (USD per 1M tokens), 1 AI credit = $0.01. Cached input billed at the cache-read rate."""
import argparse,glob,json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
PRICES={ # input, cached input, output
 'default':(0.10,0.01,0.50),'github-copilot/gpt-6-luna':(0.10,0.01,0.50),
 'github-copilot/gpt-6.1-terra':(1.00,0.10,5.00),'github-copilot/gpt-6.1-sol':(2.00,0.20,10.00),
 'github-copilot/claude-sonnet-5.5':(2.00,0.20,10.00),'github-copilot/claude-opus-5.5':(4.00,0.40,20.00)}
SHORT={'default':'luna','github-copilot/gpt-6.1-terra':'terra','github-copilot/gpt-6.1-sol':'sol','github-copilot/claude-sonnet-5.5':'sonnet','github-copilot/claude-opus-5.5':'opus'}
TABLE=json.load(open(Path(__file__).parent/'OpenClaw/lightbox-pricing.json',encoding='utf-8'))
def credits(model,usage,history):
    """Exact when the observer saw the run (backend model + cache read/write); otherwise the cached
    context (total - prompt - completion) is priced as cache reads, which can under-count cache writes."""
    total=0;backends=set()
    for u in usage:
        h=history.get(u.get('requestId','').lower())
        if h and h.get('input',-1)>=0 and h['model'] in TABLE['models']:
            rate=TABLE['models'][h['model']];ctx=h['input']+h['cacheRead']+h['cacheWrite'];t=rate['long'] if ctx>rate['threshold'] else rate['default']
            total+=sum(h[k]*t[k] for k in('input','output','cacheRead','cacheWrite'))/1e6/TABLE['usdPerCredit'];backends.add(h['model']);continue
        pi,pc,po=PRICES[model];cached=max(0,u.get('total_tokens',0)-u.get('prompt_tokens',0)-u.get('completion_tokens',0))
        total+=(u.get('prompt_tokens',0)*pi+cached*pc+u.get('completion_tokens',0)*po)/1e6/0.01;backends.add('?'+SHORT[model])
    return total,sorted(backends)
def main():
    a=argparse.ArgumentParser();a.add_argument('dir',type=Path);d=a.parse_args().dir
    spec=json.load(open(d/'bench.json',encoding='utf-8'));rows=[]
    history={r['id'].lower():r for r in json.load(open(d/'history.json',encoding='utf-8-sig'))['records']} if (d/'history.json').exists() else {}
    font=ImageFont.truetype('C:/Windows/Fonts/meiryo.ttc',22)
    for p,prompt in enumerate(spec['prompts']):
        sheet=Image.new('RGB',(4*300,len(spec['models'])*250+40),(12,12,16));dr=ImageDraw.Draw(sheet);dr.text((8,6),prompt,font=font,fill=(240,240,240))
        for m,model in enumerate(spec['models']):
            stem=f'{m:02d}_{p:02d}';f=d/(stem+'.json')
            r=json.load(open(f,encoding='utf-8')) if f.exists() else {'applied':False,'status':'missing','usage':[],'seconds':0}
            scene=json.loads(r['scene']) if r.get('scene') else {}
            cr,backend=credits(model,r['usage'],history)
            row={'model':SHORT[model],'prompt':prompt,'applied':r['applied'],'status':r['status'],'seconds':round(r['seconds'],1),'credits':round(cr,3),'backend':backend,
                 'attempts':len(r['usage']),'tokensIn':sum(u.get('prompt_tokens',0) for u in r['usage']),'tokensOut':sum(u.get('completion_tokens',0) for u in r['usage']),
                 'reported':sorted({u.get('reportedModel','?') for u in r['usage']}),'objects':len(scene.get('objects',[])),'shapes':sorted({o['shape'] for o in scene.get('objects',[])}),
                 'storyFrames':len(scene.get('storyboard',{}).get('frames',[])),'note':scene.get('note','')}
            rows.append(row)
            for t in range(4):
                shot=d/f'{stem}_t{t}.png'
                if shot.exists():
                    im=Image.open(shot).convert('RGB');w,h=im.size;im=im.crop((int(w*.17),int(h*.1),int(w*.83),int(h*.9))).resize((300,210))
                    sheet.paste(im,(t*300,40+m*250))
            dr.text((8,40+m*250+212),f"{SHORT[model]}  {'OK' if r['applied'] else 'NG'}  {row['credits']:.2f}cr  {row['seconds']:.0f}s  story{row['storyFrames']}",font=font,fill=(255,220,120))
        sheet.save(d/f'prompt{p:02d}.png')
    json.dump(rows,open(d/'summary.json','w',encoding='utf-8'),ensure_ascii=False,indent=1)
    for model in spec['models']:
        rs=[r for r in rows if r['model']==SHORT[model]]
        print(sorted({b for r in rs for b in r['backend']}),end=' ')
        print(f"{SHORT[model]:7s} ok {sum(r['applied'] for r in rs)}/{len(rs)}  credits {sum(r['credits'] for r in rs):7.2f}  mean {sum(r['credits'] for r in rs)/len(rs):6.3f}  sec {sum(r['seconds'] for r in rs)/len(rs):5.1f}  repairs {sum(r['attempts']>1 for r in rs)}  story {sum(r['storyFrames']>0 for r in rs)}")
if __name__=='__main__':main()
