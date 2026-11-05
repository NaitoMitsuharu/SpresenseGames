"""Render the title and Light Box off-screen using UE's screenshot API."""
import argparse
import subprocess
import time
from pathlib import Path


def capture(engine, project, map_name, flag, filename, extra=(), resolution=(1600, 900)):
    target = project.parent / "Saved" / "AutoShots" / filename
    target.parent.mkdir(parents=True, exist_ok=True)
    started = time.time()
    log_path = project.parent / "Saved" / "Logs" / ("Review_" + Path(filename).stem + ".log")
    with open(log_path.with_suffix(".stdout.log"), "wb") as log:
        process = subprocess.Popen([
            str(engine), str(project), "/Game/Maps/" + map_name,
            "-game", "-RenderOffscreen", f"-ResX={resolution[0]}", f"-ResY={resolution[1]}", "-ForceRes",
            "-unattended", "-nosplash", "-nosound", "-" + flag,
            "-abslog=" + str(log_path), *extra,
        ], stdout=log, stderr=subprocess.STDOUT)
        try:
            while time.time() - started < 180:
                if target.exists() and target.stat().st_mtime >= started and target.stat().st_size > 1000:
                    time.sleep(1)
                    print(str(target), flush=True)
                    return
                if process.poll() is not None:
                    raise RuntimeError(f"UE exited with {process.returncode}; see {log_path}")
                time.sleep(.5)
            raise TimeoutError(f"Screenshot did not arrive; see {log_path}")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path, default=Path("D:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--panels-only", action="store_true")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1] / "SpresenseMaze.uproject"
    if not args.panels_only:
        capture(args.engine, project, "Title_Select", "TitleAutoShot", "00_title.png")
        capture(args.engine, project, "LightBox_World", "LightBoxAutoShot", "03_lightbox.png")
    capture(args.engine, project, "LightBox_World", "LightBoxAutoShot", "03_lightbox_words.png", ("-LightBoxReviewPanel=words",))
    capture(args.engine, project, "LightBox_World", "LightBoxAutoShot", "03_lightbox_settings.png", ("-LightBoxReviewPanel=settings",))
