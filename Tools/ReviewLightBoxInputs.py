"""Live Android input review through real touch, IME and speaker-to-microphone audio.
Requires the test-only AndroidInputReview helper and a 2340x1080 landscape device.
Uses the app's saved authentication, never reads a gateway key. AI calls require --live.
"""
import argparse, json, shlex, subprocess, time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
DEVICE_JOURNAL='/sdcard/Android/data/com.lati.spresensemaze/files/UnrealGame/SpresenseMaze/SpresenseMaze/Saved/LightBox/Journal/journal.json'
CASES=[
    ('voice01','voice','金色の光で輪を作って、ゆっくり回して'),
    ('text01','text','青い海の中を、256匹の魚が3つの群れで泳ぐ景色にして'),
    ('text02','text','魚の数と動きを変えずに、魚だけを金色にして'),
    ('voice02','voice','花のある小さな森にして'),
    ('voice03','voice','目玉焼きを作って'),
    ('text03','text','夕焼けの空を、小鳥が羽ばたきながら飛ぶ景色にして'),
]

def adb(*args):
    return subprocess.run(['adb',*map(str,args)],capture_output=True,check=True)
def tap(x,y): adb('shell','input','tap',x,y)
def journal(out):
    adb('pull',DEVICE_JOURNAL,out/'journal.json')
    return json.loads((out/'journal.json').read_text(encoding='utf-8-sig'))
def shot(out,name):
    adb('shell','screencap','-p','/sdcard/lightbox-review.png')
    adb('pull','/sdcard/lightbox-review.png',out/name)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--live',action='store_true')
    parser.add_argument('--ids',default=','.join(c[0] for c in CASES))
    parser.add_argument('--existing-draft',action='store_true',help='First selected case is already in the text field')
    parser.add_argument('--out',type=Path,default=ROOT/'Saved/InputReview')
    args=parser.parse_args()
    if not args.live: parser.error('--live is required; these tests consume model credits and play audio')
    args.out.mkdir(parents=True,exist_ok=True)
    results_file=args.out/'results.json'
    results=json.loads(results_file.read_text(encoding='utf8')) if results_file.exists() else []
    cases=[c for c in CASES if c[0] in args.ids.split(',')]
    for index,(ident,mode,words) in enumerate(cases):
        previous=journal(args.out);old_ids={r['id'] for r in previous['records']}
        shot(args.out,ident+'-before.png')
        if not (index==0 and args.existing_draft):
            command=['shell','am','start','-n','com.lati.lightbox.inputreview/.AudioPromptActivity','--es','text',shlex.quote(words)]
            if mode=='text': command+=['--ez','clipboard','true']
            adb(*command);time.sleep(1.8)
            if mode=='voice':
                tap(774,1010);time.sleep(12)
            else:
                # Use Gboard's visible clipboard suggestion; KEYCODE_PASTE isn't routed to UE's Android input bar.
                tap(1120,1005);time.sleep(.8);tap(1190,592);time.sleep(.3);adb('shell','input','keyevent',4);time.sleep(.4)
        shot(args.out,ident+'-draft.png')
        draft_records=journal(args.out)['records']
        unsolicited=any(r['id'] not in old_ids for r in draft_records)
        if not unsolicited: tap(1563,1010)
        print(f'{ident}: submitted via {mode}; request before confirmation={unsolicited}',flush=True)
        record=None;started=time.monotonic()
        while time.monotonic()-started<150:
            data=journal(args.out)
            fresh=[r for r in data['records'] if r['id'] not in old_ids]
            if fresh:
                record=fresh[-1]
                if record['result']!='応答待ち' and (record['credits']>=0 or '取り消し' in record['result']): break
            time.sleep(2)
        time.sleep(2);shot(args.out,ident+'-after.png');time.sleep(2);shot(args.out,ident+'-motion.png')
        row={'case':ident,'mode':mode,'requested':words,'sentBeforeConfirmation':unsolicited}
        if record:
            row.update({k:record[k] for k in ('id','words','result','credits','seconds','repair')})
            row['scene']=json.loads(record['after']) if record['after'] else None
            row['before']=json.loads(record['before']) if record['before'] else None
        else: row['result']='No request arrived; inspect draft screenshot'
        results=[r for r in results if r['case']!=ident]+[row]
        results_file.write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
        print(f'{ident}: {row["result"]}, credits={row.get("credits")}, recognized={row.get("words")}',flush=True)
        if row['result']!='反映済み': raise RuntimeError('Input review stopped for inspection; no further requests sent')
    adb('shell','rm','-f','/sdcard/lightbox-review.png')

if __name__=='__main__': main()
