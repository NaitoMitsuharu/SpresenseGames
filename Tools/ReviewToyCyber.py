"""Capture actual UE gameplay/UI, without writing synthetic player records."""
import argparse
import shutil
import subprocess
from pathlib import Path
from ReviewLightBox import capture

parser = argparse.ArgumentParser()
parser.add_argument('--width', type=int, default=1600)
parser.add_argument('--height', type=int, default=900)
parser.add_argument('--flight-only', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
engine = Path('D:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe')
project = root / 'SpresenseMaze.uproject'
out = root / 'Saved/ToyCyberReview' / f'{args.width}x{args.height}'
out.mkdir(parents=True, exist_ok=True)
if not args.flight_only:
    for state in ['home', 'playing', 'map', 'pause', 'tutorial', 'clear', 'celebration', 'ranking']:
        filename = 'ui_maze_' + state + '.png'
        capture(engine, project, 'MainMaze', 'MazeUIReview=' + state, filename,
                ('-MazeForceTouch',) if state == 'tutorial' else (), (args.width, args.height))
        shutil.copy2(root / 'Saved/AutoShots' / filename, out / filename)
    capture(engine, project, 'MainMaze', 'MazeUIReview=playing', 'ui_maze_playing.png',
            ('-MazeStage=4',), (args.width, args.height))
    shutil.copy2(root / 'Saved/AutoShots/ui_maze_playing.png', out / 'maze_stage4.png')
log = out / 'flight.log'
with open(out / 'flight.stdout.log', 'wb') as stdout:
    result = subprocess.run([str(engine), str(project), '/Game/Maps/Flight_Course',
            '-game', '-RenderOffscreen', '-unattended', '-nosplash', '-nosound',
            '-FlightHudReview', '-HudWide', f'-ResX={args.width}', f'-ResY={args.height}',
            '-ForceRes', '-abslog=' + str(log)], stdout=stdout, stderr=subprocess.STDOUT,
            timeout=240)
    if result.returncode:
        raise RuntimeError(f'Flight review failed: {log}')
for source in (root / 'Saved/ModernFlightHUD/Wide').glob('*.png'):
    shutil.copy2(source, out / ('flight_' + source.name))
text = log.read_text(encoding='utf-8', errors='replace')
assert 'FlightHudReview: dodge, cooldown, protection, bounds and pause checks completed' in text
assert 'Ensure condition failed' not in text
print('Reviewed actual UE screens and flight interaction invariants:', out, flush=True)
