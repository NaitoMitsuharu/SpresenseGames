"""Real-device screenshots, pause/resume touch smoke test and flight recording.

Requires the current Development APK. Does not complete maze runs, save synthetic
scores, enter LightBox or issue AI requests. Leaves the normal game selector open.
"""
import json
import shlex
import subprocess
import time
from pathlib import Path
from PIL import Image
from ReviewPointCloudAndroid import adb, BASE, ROOT

OUT = ROOT / 'Saved/ToyCyberReview/Android'
PACKAGE = 'com.lati.spresensegames'
ACTIVITY = PACKAGE + '/com.epicgames.unreal.GameActivity'
OUT.mkdir(parents=True, exist_ok=True)


def launch(command):
    adb('shell', 'am', 'force-stop', PACKAGE)
    adb('shell', 'am', 'start', '-n', ACTIVITY, '--es', 'cmdline', shlex.quote(command))


def screen(name):
    remote = BASE + '/ToyCyberScreen.png'
    adb('shell', 'screencap', '-p', remote)
    adb('pull', remote, OUT / (name + '.png'))
    print(name, flush=True)


adb('pull', BASE + '/SaveGames', OUT / 'savegames-before', check=False)
adb('pull', BASE + '/LightBox/Journal/journal.json', OUT / 'journal-before.json', check=False)
try:
    launch('/Game/Maps/MainMaze -MazeUIReview=playing')
    time.sleep(9)
    screen('maze-playing')
    width, height = Image.open(OUT / 'maze-playing.png').size
    scale = max(.6, min(1.6, height / 1080))
    left = max(20, min(190, width * .08))
    adb('shell', 'input', 'tap', int(left + 442 * scale), int(65 * scale))
    time.sleep(1)
    screen('maze-paused-touch')
    adb('shell', 'input', 'tap', width // 2, int(height * .5 - 50 * scale))
    time.sleep(1)
    screen('maze-resumed-touch')
    adb('shell', 'input', 'tap', int(width - 118 * scale), int(65 * scale))
    time.sleep(1)
    screen('maze-map-touch')
    launch('/Game/Maps/MainMaze -MazeUIReview=playing -MazeStage=4')
    time.sleep(7)
    screen('maze-stage4')
    launch('/Game/Maps/MainMaze -MazeUIReview=clear')
    time.sleep(7)
    screen('maze-clear-layout')
    adb('logcat', '-c')
    # Stream to a file: other connected tools may clear the shared logcat buffer.
    live_log = open(OUT / 'flight-live.log', 'wb')
    collector = subprocess.Popen(['adb', 'logcat', '-v', 'threadtime', 'UE:D', '*:S'],
                                 stdout=live_log, stderr=subprocess.STDOUT)
    recording = subprocess.Popen(['adb', 'shell', 'screenrecord', '--time-limit', '24',
                                  '/sdcard/ToyCyberFlight.mp4'], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
    launch('/Game/Maps/Flight_Course -FlightHudReview -HudWide')
    recording.wait(timeout=35)
    collector.terminate()
    collector.wait(timeout=5)
    live_log.close()
    adb('pull', '/sdcard/ToyCyberFlight.mp4', OUT / 'flight.mp4')
    adb('pull', BASE + '/ModernFlightHUD/Wide', OUT / 'flight', check=False)
    log = (OUT / 'flight-live.log').read_text(encoding='utf-8', errors='replace')
    (OUT / 'device.log').write_text(log, encoding='utf-8')
    result = {'flightChecks': 'FlightHudReview: dodge, cooldown, protection, bounds and pause checks completed' in log,
              'ensureFailure': 'Ensure condition failed' in log,
              'screens': sorted(p.name for p in OUT.glob('*.png'))}
    (OUT / 'review.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result), flush=True)
finally:
    adb('pull', BASE + '/LightBox/Journal/journal.json', OUT / 'journal-after.json', check=False)
    before, after = OUT / 'journal-before.json', OUT / 'journal-after.json'
    if before.exists() and after.exists():
        assert before.read_bytes() == after.read_bytes(), 'AI journal changed during game review'
    adb('pull', BASE + '/SaveGames', OUT / 'savegames-after', check=False)
    launch('/Game/Maps/Title_Select')
