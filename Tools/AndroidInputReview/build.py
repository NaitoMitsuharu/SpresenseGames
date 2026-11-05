"""Install the test-only audio helper; does not modify the game's APK."""
import os, subprocess, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
source=Path(__file__).parent
out=root/'Saved/InputReview/helper'
(out/'classes').mkdir(parents=True,exist_ok=True)
(out/'dex').mkdir(exist_ok=True)
sdk=Path(os.environ['LOCALAPPDATA'])/'Android/Sdk'
build=sdk/'build-tools/35.0.1'
android=sdk/'platforms/android-35/android.jar'
jdk=Path('C:/Program Files/Android/Android Studio/jbr/bin')
def run(*args): subprocess.run([str(a) for a in args],check=True)
run(jdk/'javac.exe','-source','17','-target','17','-cp',android,'-d',out/'classes',*list(source.glob('*.java')))
run(build/'d8.bat','--lib',android,'--output',out/'dex',*list((out/'classes').rglob('*.class')))
run(build/'aapt2.exe','link','-I',android,'--manifest',source/'AndroidManifest.xml','-o',out/'helper.apk')
with zipfile.ZipFile(out/'helper.apk','a') as z:z.write(out/'dex/classes.dex','classes.dex')
run(build/'zipalign.exe','-f','4',out/'helper.apk',out/'helper-aligned.apk')
run(build/'apksigner.bat','sign','--ks',Path.home()/'.android/debug.keystore','--ks-pass','pass:android','--key-pass','pass:android',out/'helper-aligned.apk')
run('adb','install','-r','-t',out/'helper-aligned.apk')
