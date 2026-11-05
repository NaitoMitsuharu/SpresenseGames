"""Measure the point renderer on a real Android device without contacting the AI provider.
Leaves the installed APK in place and restores the original scene/history via review-only mode.
"""
import argparse,hashlib,json,shlex,subprocess,time
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).parent/'PointCloud'))
from fixtures import scene

ROOT=Path(__file__).resolve().parents[1]
BASE='/sdcard/Android/data/com.lati.spresensemaze/files/UnrealGame/SpresenseMaze/SpresenseMaze/Saved'
def adb(*args,check=True):return subprocess.run(['adb',*map(str,args)],capture_output=True,check=check)
def main():
    p=argparse.ArgumentParser();p.add_argument('--counts',default='10000,65536,100000,300000,1000000');p.add_argument('--out',type=Path,default=ROOT/'Saved/PointSculpture/phone');a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True);results=[]
    adb('pull',BASE+'/LightBox/Journal/journal.json',a.out/'journal-before.json',check=False)
    adb('shell','mkdir','-p',BASE+'/PointCloudBench')
    try:
        for count in map(int,a.counts.split(',')):
            if not 256<=count<=1000000:raise ValueError('Point count outside the agreed budget')
            local=a.out/f'{count}.scene.json';local.write_text(json.dumps(scene(count=count),ensure_ascii=False),encoding='utf8')
            target=BASE+f'/PointCloudBench/{count}.json';report=BASE+f'/PointCloudBench/{count}.report.json'
            adb('push',local,target);adb('shell','rm','-f',report);adb('shell','am','force-stop','com.lati.spresensemaze')
            cmd=f'/Game/Maps/LightBox_World -PointCloudReview -PointCloudPhase=1 -LightBoxScene={target} -PointCloudReport={report}'
            adb('shell','am','start','-n','com.lati.spresensemaze/com.epicgames.unreal.GameActivity','--es','cmdline',shlex.quote(cmd))
            start=time.monotonic();path=a.out/f'{count}.report.json'
            while time.monotonic()-start<100:
                pulled=adb('pull',report,path,check=False)
                if pulled.returncode==0:break
                time.sleep(1)
            else:raise TimeoutError(f'No benchmark result for {count} points')
            row=json.loads(path.read_text(encoding='utf-8-sig'));row['requested']=count
            if row['instances']!=count:raise RuntimeError('Requested points were not all instantiated')
            adb('shell','screencap','-p',BASE+'/PointCloudBench/screen.png');adb('pull',BASE+'/PointCloudBench/screen.png',a.out/f'{count}.png')
            results.append(row);(a.out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf8')
            print(f'{count:,} points: {row["fps"]:.1f} fps / p95 {row["frameMsP95"]:.1f} ms',flush=True)
    finally:
        adb('shell','am','force-stop','com.lati.spresensemaze')
        adb('pull',BASE+'/LightBox/Journal/journal.json',a.out/'journal-after.json',check=False)
        before=a.out/'journal-before.json';after=a.out/'journal-after.json'
        if before.exists() and after.exists():
            if before.read_bytes()!=after.read_bytes():raise RuntimeError('Review mode changed the user journal')
            print('User journal unchanged',flush=True)
        adb('shell','am','start','-n','com.lati.spresensemaze/com.epicgames.unreal.GameActivity','--es','cmdline','/Game/Maps/LightBox_World')

if __name__=='__main__':main()
