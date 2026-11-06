"""Audit particle-only scenery/animation on Android, with no AI calls or user data changes."""
import argparse,json,shlex,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from ReviewPointCloudAndroid import adb,BASE,ROOT
sys.path.insert(0,str(Path(__file__).parent/'PointCloud'))
from fixtures import scene

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=ROOT/'Saved/ParticlesOnly/phone');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    record=json.loads((ROOT/'Docs/LightBoxCameraReview-2026-11-05.json').read_text(encoding='utf8'))['liveTextCases']
    legacy=json.loads(json.dumps(record[1]['scene']));gull=json.loads(json.dumps(legacy));fireflies=record[0]['scene']
    gull['pointCloud'].update(enabled=True,count=65536,floor=False);fireflies['pointCloud'].update(enabled=True,count=65536,floor=False)
    million=json.loads(json.dumps(gull));million['pointCloud']['count']=1000000
    cases=[('legacy-gulls',legacy),('gulls-65536',gull),('fireflies-65536',fireflies),('gulls-1000000',million),('eggplant-1000000',scene(count=1000000))]
    adb('pull',BASE+'/LightBox/Journal/journal.json',a.out/'journal-before.json');results=[]
    adb('shell','mkdir','-p',BASE+'/ParticleScenesReview')
    try:
        for name,value in cases:
            local=a.out/(name+'.json');local.write_text(json.dumps(value,ensure_ascii=False),encoding='utf8')
            target=BASE+'/ParticleScenesReview/'+name+'.json';report=target+'.report.json'
            adb('push',local,target);adb('shell','rm','-f',report);adb('shell','am','force-stop','com.lati.spresensemaze')
            # No frozen phase: measure continuous deformation as well as assembly.
            cmd=f'/Game/Maps/LightBox_World -PointCloudReview -LightBoxScene={target} -PointCloudReport={report}'
            adb('shell','am','start','-n','com.lati.spresensemaze/com.epicgames.unreal.GameActivity','--es','cmdline',shlex.quote(cmd))
            start=time.monotonic();path=a.out/(name+'.report.json')
            while time.monotonic()-start<110:
                if adb('pull',report,path,check=False).returncode==0:break
                time.sleep(1)
            else:raise TimeoutError('Missing particle scene report: '+name)
            row=json.loads(path.read_text(encoding='utf-8-sig'));row['case']=name
            assert row['particleOnly'] and row['solidSceneComponents']==0,row
            expected=max(131072,value['pointCloud']['count']) if name=='legacy-gulls' else value['pointCloud']['count']
            assert row['instances']==row['points']==expected,row
            if name.startswith(('gulls','fireflies')):assert row['animatedTarget'] and row['controlTravel']>1,row
            adb('shell','screencap','-p',BASE+'/ParticleScenesReview/screen.png');adb('pull',BASE+'/ParticleScenesReview/screen.png',a.out/(name+'.png'))
            results.append(row);(a.out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf8')
            print(f'{name}: {row["fps"]:.1f}fps / solid components {row["solidSceneComponents"]} / control travel {row["controlTravel"]:.1f}cm',flush=True)
    finally:
        adb('shell','am','force-stop','com.lati.spresensemaze')
        adb('pull',BASE+'/LightBox/Journal/journal.json',a.out/'journal-after.json')
        adb('shell','am','start','-n','com.lati.spresensemaze/com.epicgames.unreal.GameActivity','--es','cmdline','/Game/Maps/LightBox_World')
        assert (a.out/'journal-before.json').read_bytes()==(a.out/'journal-after.json').read_bytes(),'Review changed the user journal'
        print('User journal unchanged; restored normal app',flush=True)

if __name__=='__main__':main()
