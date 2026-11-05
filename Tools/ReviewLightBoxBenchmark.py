# Local benchmark review; no inference.
import argparse,collections,csv,html,json,pathlib
from PIL import Image,ImageDraw,ImageFont
ROOT=pathlib.Path(__file__).resolve().parents[1]
def read(p,default=None):
 try:return json.loads(p.read_text(encoding='utf-8-sig'))
 except (OSError,ValueError):return default

def main():
 p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);p.add_argument('--round',default='baseline');a=p.parse_args();root=a.directory.resolve();folder=root/a.round
 cases=read(ROOT/'Tools/Benchmarks/lightbox-100.json')['cases'];notes=read(root/'visual-reviews.json',{});rows=[];failures=collections.Counter();known=unknown=calls=repairs=accepted=checks=passed=0
 for c in cases:
  result=read(folder/f'{c["id"]:03d}.result.json')
  if not result:continue
  calls+=len(result['attempts']);repairs+=max(0,len(result['attempts'])-1);accepted+=result['accepted'];known+=result.get('creditsKnown',0);unknown+=result.get('creditsUnknown',0)
  audit=result.get('audit',{});failed=[x['check'] for x in audit.get('checks',[]) if not x['pass']];failures.update(failed)
  if not result['accepted']:failures['schema_or_intent_rejection']+=1
  checks+=audit.get('checksTotal',0);passed+=audit.get('checksPassed',0)
  rows.append({**c,'accepted':result['accepted'],'attempts':len(result['attempts']),'credits':result.get('creditsKnown',0),'unknown':result.get('creditsUnknown',0),'seconds':sum(x.get('seconds',0) for x in result['attempts']),'failedChecks':failed,'metrics':audit.get('metrics',{}),'tag':result.get('acceptedAttempt'),'visual':notes.get(a.round,{}).get(str(c['id']))})
 summary={'round':a.round,'completed':len(rows),'accepted':accepted,'calls':calls,'repairs':repairs,'creditsKnown':known,'creditsUnknown':unknown,'checksPassed':passed,'checksTotal':checks,'failedChecks':dict(failures),'rows':rows}
 (folder/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
 with open(folder/'summary.csv','w',encoding='utf-8-sig',newline='') as f:
  w=csv.writer(f);keys=['id','category','words','accepted','attempts','credits','unknown','seconds','failedChecks','visual'];w.writerow(keys)
  for r in rows:w.writerow([json.dumps(r[k],ensure_ascii=False) if k in ['failedChecks','visual'] else r[k] for k in keys])
 font=ImageFont.truetype('C:/Windows/Fonts/meiryo.ttc',17);small=ImageFont.truetype('C:/Windows/Fonts/meiryo.ttc',13);sheets=folder/'contacts';sheets.mkdir(exist_ok=True);byid={r['id']:r for r in rows}
 for page in range(10):
  im=Image.new('RGB',(1600,1800),'#0a1520');d=ImageDraw.Draw(im)
  for index,c in enumerate(cases[page*10:(page+1)*10]):
   x=(index%2)*800;y=(index//2)*360;r=byid.get(c['id']);d.text((x+10,y+5),f'{c["id"]:03d} {c["words"][:36]}',font=font,fill='#d7eae8')
   if not r:continue
   tag=r.get('tag')
   for t in [0,2]:
    f=folder/f'{tag}_t{t}.png'
    if f.exists():
     pic=Image.open(f).convert('RGB').resize((780,439)).crop((115,62,735,360)).resize((385,185));im.paste(pic,(x+6+(t//2)*393,y+38))
   d.text((x+10,y+230),('受理' if r['accepted'] else '検証で拒否')+f' / {r["attempts"]}呼出 / 推定{r["credits"]:.4f}cr',font=small,fill='#8fd7cb')
   d.text((x+10,y+255),'要確認: '+','.join(r['failedChecks']),font=small,fill='#f3c284')
   if r.get('visual'):
    text=r['visual'].get('comment','')
    for j in range(0,len(text),49):d.text((x+10,y+279+(j//49)*22),text[j:j+49],font=small,fill='#c7d0d8')
  im.save(sheets/f'{page+1:02d}.jpg',quality=92)
 payload=json.dumps(summary,ensure_ascii=False).replace('</','<\\/')
 page=r'''<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ひかりの箱・100指示レビュー</title><style>body{margin:0;background:#09141e;color:#dce9ef;font:16px/1.8 system-ui,sans-serif}main{max-width:1500px;margin:40px auto;padding:20px}h1{font-size:36px}p{color:#b7cbd5}button,select,input{font:inherit;background:#203c47;color:#edf8f7;border:1px solid #446775;border-radius:8px;padding:7px;margin:5px}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(500px,1fr));gap:18px}article{padding:18px;background:#132936;border-radius:14px;border:1px solid #2b4854}img{width:100%;border-radius:8px}a{color:#89ddd2}pre{white-space:pre-wrap;font-size:13px}.bad{color:#f4b578}.ok{color:#86dfbd}.note{background:#283d3d;padding:12px;border-radius:10px}@media(max-width:600px){section{display:block}article{margin:15px 0}}</style><main><h1>100の指示で、景色を検証する</h1><p>実際のOpenClaw＋Copilotの返答をアプリと同じパーサー・描画で実行。形式と要求チェック、画像レビューを区別しています。画像はWindows UEで中立姿勢・同じ時刻に再生した結果です。</p><div id="summary"></div><p class="note">クレジットは元のusageと公表単価からの推定。確定請求額・残高ではありません。未取得は0消費と扱いません。形式の受理は、見た目や意味の成功を保証しません。コントラストなどは画像での確認が必要です。</p><label>分類<select id="cat"><option value="">すべて</option></select></label><label><input type="checkbox" id="bad">要確認のみ</label><input id="search" placeholder="指示を検索"><section id="cards"></section><script>const data=PAYLOAD;document.getElementById('summary').innerHTML=`<strong>${data.round}：${data.completed}/100件 / 形式受理 ${data.accepted}件 / ${data.calls}呼出（修正${data.repairs}回） / 推定 ${data.creditsKnown.toFixed(4)}cr / 利用量未取得 ${data.creditsUnknown}件</strong><p><a href="summary.csv">CSV</a> · <a href="summary.json">全記録JSON</a></p>`;const cat=document.getElementById('cat');for(const c of [...new Set(data.rows.map(x=>x.category))])cat.add(new Option(c,c));function render(){const query=document.getElementById('search').value;document.getElementById('cards').innerHTML=data.rows.filter(x=>(!cat.value||x.category===cat.value)&&(!document.getElementById('bad').checked||!x.accepted||x.failedChecks.length||x.visual?.rating<3)&&(!query||x.words.includes(query))).map(x=>`<article><div>${String(x.id).padStart(3,'0')} · ${x.category}</div><h3>${x.words.replaceAll('<','&lt;')}</h3><div class="${x.accepted?'ok':'bad'}">${x.accepted?'形式受理':'検証で拒否'} / 推定${x.credits.toFixed(4)}cr / ${x.seconds.toFixed(1)}秒</div>${x.tag?`<img id="p${x.id}" src="${x.tag}_t1.png"><button onclick="document.getElementById('p${x.id}').src='${x.tag}_t0.png'">0.2秒</button><button onclick="document.getElementById('p${x.id}').src='${x.tag}_t1.png'">2.5秒</button><button onclick="document.getElementById('p${x.id}').src='${x.tag}_t2.png'">6秒</button>`:'<p>不正な返答のため、旧景色への置き換えで成功扱いにしていません。</p>'}<p class="bad">${x.failedChecks.length?'要求の要確認：'+x.failedChecks.join(', '):''}</p>${x.visual?`<p class="note">画像レビュー ${x.visual.rating}/5：${x.visual.comment}</p>`:'<p>個別の画像レビューは未記入。</p>'}<details><summary>計測値</summary><pre>${JSON.stringify(x.metrics,null,2)}</pre></details></article>`).join('')}for(const e of ['cat','bad','search'])document.getElementById(e).addEventListener('input',render);render();</script></main></html>'''.replace('PAYLOAD',payload)
 (folder/'index.html').write_text(page,encoding='utf-8');print({k:v for k,v in summary.items() if k!='rows'})
if __name__=='__main__':main()
