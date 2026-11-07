"""Capture the experience menus/HUDs at phone and desktop landscape sizes."""
import argparse
import shutil
from pathlib import Path
from ReviewLightBox import capture

parser = argparse.ArgumentParser()
parser.add_argument('--width', type=int, default=1600)
parser.add_argument('--height', type=int, default=900)
parser.add_argument('--quick', action='store_true')
parser.add_argument('--only', nargs='+', help='Capture selected filename stems, e.g. 00_title 03_lightbox')
args = parser.parse_args()
project = Path(__file__).resolve().parents[1] / 'SpresenseMaze.uproject'
engine = Path('D:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe')
resolution = (args.width, args.height)
jobs = [
    ('Title_Select', 'TitleAutoShot', '00_title.png', ()),
    ('MainMaze', 'MazeUIReview=home', 'ui_maze_home.png', ()),
    ('MainMaze', 'MazeUIReview=playing', 'ui_maze_playing.png', ()),
    ('LightBox_World', 'LightBoxAutoShot', '03_lightbox.png', ()),
]
if not args.quick:
    jobs += [
        ('MainMaze', 'MazeUIReview=tutorial', 'ui_maze_tutorial.png', ('-MazeForceTouch',)),
        ('MainMaze', 'MazeUIReview=clear', 'ui_maze_clear.png', ()),
        ('MainMaze', 'MazeUIReview=ranking', 'ui_maze_ranking.png', ()),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_menu.png', ('-LightBoxReviewPanel=menu',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_examples.png', ('-LightBoxReviewPanel=examples',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_words.png', ('-LightBoxReviewPanel=words',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_keyboard.png', ('-LightBoxReviewPanel=keyboard',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_sensors.png', ('-LightBoxReviewPanel=sensors',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_sensor_breath.png', ('-LightBoxReviewSensor=breath',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_sensor_clap.png', ('-LightBoxReviewSensor=clap',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_sensor_dark.png', ('-LightBoxReviewSensor=dark',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_sensor_bright.png', ('-LightBoxReviewSensor=bright',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_sensor_warmth.png', ('-LightBoxReviewSensor=warmth',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_sensor_hand.png', ('-LightBoxReviewSensor=hand',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_sensor_magnet.png', ('-LightBoxReviewSensor=magnet',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_settings.png', ('-LightBoxReviewPanel=settings',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_auth.png', ('-LightBoxReviewPanel=auth',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_budget.png', ('-LightBoxReviewPanel=budget',)),
        ('LightBox_World', 'LightBoxAutoShot', '03_lightbox_journal.png', ('-LightBoxReviewPanel=journal',)),
    ]
if args.only:
    jobs = [job for job in jobs if Path(job[2]).stem in args.only]
    if not jobs:
        parser.error('No matching screen names')
out = project.parent / 'Saved/UiRefresh' / f'{args.width}x{args.height}'
out.mkdir(parents=True, exist_ok=True)
for map_name, flag, filename, extra in jobs:
    capture(engine, project, map_name, flag, filename, extra, resolution)
    shutil.copy2(project.parent / 'Saved/AutoShots' / filename, out / filename)
print(f'UI review captures: {out}', flush=True)
