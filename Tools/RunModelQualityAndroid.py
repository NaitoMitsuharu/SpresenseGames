"""Run an explicitly requested, paid in-app comparison and retain actual motion clips.

Uses the installed Development APK's LightBoxBench path, including its production
prompt, validation, repair, renderer, and on-device credentials. No key is read.
"""
import argparse
import json
import shlex
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = '/sdcard/Android/data/com.lati.spresensegames/files/UnrealGame/SpresenseMaze/SpresenseMaze/Saved'
PACKAGE = 'com.lati.spresensegames'


def adb(*args, check=True):
    return subprocess.run(['adb', *map(str, args)], capture_output=True, check=check)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--monitor-only', action='store_true', help='Attach to a running comparison without sending it again')
    args = parser.parse_args()
    if not args.live:
        parser.error('--live is required: these model requests consume credits')
    out = args.directory.resolve()
    spec = json.loads((out / 'bench.json').read_text(encoding='utf-8'))
    remote = BASE + '/QualityReview/' + out.name
    if not args.monitor_only:
        if adb('shell', 'ls', remote + '/bench.json', check=False).returncode == 0:
            raise RuntimeError('Run already exists: use --monitor-only or a new directory name; do not resend paid requests')
        adb('shell', 'mkdir', '-p', remote)
        adb('push', out / 'bench.json', remote + '/bench.json')
        adb('shell', 'am', 'force-stop', PACKAGE)
        command = '/Game/Maps/LightBox_World -LightBoxNoSensors -LightBoxBench=' + remote
        adb('shell', 'am', 'start', '-n', PACKAGE + '/com.epicgames.unreal.GameActivity',
            '--es', 'cmdline', shlex.quote(command))
    started = time.monotonic()
    seen, clips = set(), set()
    recording = None
    while time.monotonic() - started < 3600:
        names = set(adb('shell', 'ls', remote).stdout.decode().split())
        if recording and recording.poll() is not None:
            recording = None
        for name in sorted(names):
            if re.fullmatch(r'\d{2}_\d{2}\.json', name) and name not in seen:
                adb('pull', remote + '/' + name, out / name)
                try:
                    row = json.loads((out / name).read_text(encoding='utf-8-sig'))
                except json.JSONDecodeError:
                    continue
                seen.add(name)
                print(json.dumps({k: row.get(k) for k in ['model', 'prompt', 'applied', 'status', 'seconds']},
                                 ensure_ascii=False), flush=True)
            # A monitor can be reattached late. Never label the current scene as
            # an earlier result; completed shot sequences are replayed offline.
            if (name.endswith('_t0.png') and name not in clips and recording is None
                    and name[:-7] + '_t3.png' not in names):
                stem = name[:-7]
                recording = subprocess.Popen(['adb', 'shell', 'screenrecord', '--bit-rate', '4000000',
                                              '--time-limit', '6', remote + '/' + stem + '.mp4'],
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                clips.add(name)
        if 'done.json' in names:
            if recording:
                recording.wait(timeout=12)
            adb('pull', remote + '/.', out)
            expected = len(spec['models']) * len(spec['prompts'])
            if len(seen) != expected:
                raise RuntimeError(f'Expected {expected} results, received {len(seen)}')
            print(f'Finished {len(seen)} cases with {len(clips)} motion clips: {out}', flush=True)
            return
        time.sleep(1.2)
    raise TimeoutError('Comparison did not finish within one hour; inspect the device before retrying')


if __name__ == '__main__':
    main()
