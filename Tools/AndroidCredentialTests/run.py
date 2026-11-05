"""Build a small, isolated instrumentation APK and test the actual Android Keystore."""
import os
from pathlib import Path
import subprocess
import zipfile

root = Path(__file__).resolve().parents[2]
source = Path(__file__).parent
out = root / 'Saved/UiRefresh/CredentialTests'
classes = out / 'classes'
dex = out / 'dex'
classes.mkdir(parents=True, exist_ok=True)
dex.mkdir(exist_ok=True)
sdk = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'
build = sdk / 'build-tools/35.0.1'
android = sdk / 'platforms/android-35/android.jar'
jdk = Path('C:/Program Files/Android/Android Studio/jbr/bin')

def run(*args):
    subprocess.run([str(x) for x in args], check=True)

run(jdk / 'javac.exe', '-source', '17', '-target', '17', '-cp', android, '-d', classes,
    root / 'Source/SpresenseMaze/Android/LightBoxCredentials.java', source / 'CredentialsInstrumentation.java')
run(build / 'd8.bat', '--lib', android, '--classpath', classes, '--output', dex,
    classes / 'com/lati/lightbox/CredentialsInstrumentation.class')
apk = out / 'checks.apk'
run(build / 'aapt2.exe', 'link', '-I', android, '--manifest', source / 'AndroidManifest.xml', '-o', apk)
with zipfile.ZipFile(apk, 'a') as package:
    package.write(dex / 'classes.dex', 'classes.dex')
aligned = out / 'checks-aligned.apk'
run(build / 'zipalign.exe', '-f', '4', apk, aligned)
run(build / 'apksigner.bat', 'sign', '--ks', Path.home() / '.android/debug.keystore',
    '--ks-pass', 'pass:android', '--key-pass', 'pass:android', aligned)
run('adb', 'install', '-r', '-t', aligned)
completed = subprocess.run(['adb', 'shell', 'am', 'instrument', '-w',
    'com.lati.lightbox.test/com.lati.lightbox.CredentialsInstrumentation'], capture_output=True, text=True, check=True)
print(completed.stdout)
(out / 'result.txt').write_text(completed.stdout, encoding='utf8')
if 'Credential checks passed: 13' not in completed.stdout:
    raise SystemExit('Android credential verification failed')
