"""Replay saved comparison scenes on Android without an AI request.

Uses the review-only renderer, records twelve seconds, and samples at deliberately
uneven intervals so a periodic action cannot alias into identical still frames.
"""
import argparse
import json
import shlex
import subprocess
import time
from pathlib import Path
from PIL import Image, ImageDraw

from RunModelQualityAndroid import adb, BASE, PACKAGE


def main():
    p = argparse.ArgumentParser()
    p.add_argument('directory', type=Path)
    p.add_argument('stems', nargs='+')
    a = p.parse_args()
    out = a.directory.resolve() / 'replay'
    out.mkdir(exist_ok=True)
    remote = BASE + '/QualityReviewReplay'
    adb('shell', 'mkdir', '-p', remote)
    for stem in a.stems:
        row = json.loads((a.directory / (stem + '.json')).read_text(encoding='utf-8-sig'))
        if not row['applied']:
            print(stem, 'not applied', flush=True)
            continue
        scene = out / (stem + '.json')
        scene.write_text(row['scene'], encoding='utf-8')
        adb('push', scene, remote + '/scene.json')
        adb('shell', 'am', 'force-stop', PACKAGE)
        report = remote + '/' + stem + '.report.json'
        adb('shell', 'rm', '-f', report)
        command = ('/Game/Maps/LightBox_World -PointCloudReview -LightBoxNoSensors '
                   '-LightBoxScene=' + remote + '/scene.json -PointCloudReport=' + report)
        adb('shell', 'am', 'start', '-n', PACKAGE + '/com.epicgames.unreal.GameActivity',
            '--es', 'cmdline', shlex.quote(command))
        time.sleep(13)
        adb('shell', 'screenrecord', '--bit-rate', '5000000', '--time-limit', '12', remote + '/motion.mp4')
        clip = out / (stem + '.mp4')
        adb('pull', remote + '/motion.mp4', clip)
        adb('pull', report, out / (stem + '.report.json'), check=False)
        sheet = Image.new('RGB', (1200, 620), (12, 12, 16))
        draw = ImageDraw.Draw(sheet)
        for i, sec in enumerate([.1, .47, 1.13, 2.31, 7.19, 11.07]):
            frame = out / f'{stem}_{i}.png'
            subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-ss', str(sec), '-i',
                            str(clip), '-frames:v', '1', str(frame)], check=True)
            im = Image.open(frame).convert('RGB')
            w, h = im.size
            im = im.crop((int(w*.17), int(h*.10), int(w*.83), int(h*.90)))
            im.thumbnail((400, 285))
            x, y = (i % 3)*400, (i // 3)*310
            sheet.paste(im, (x+(400-im.width)//2, y))
            draw.text((x+8, y+287), f'{stem} +{sec}s', fill='white')
        sheet.save(out / (stem + '.png'))
        print('Replayed', stem, flush=True)


if __name__ == '__main__':
    main()
