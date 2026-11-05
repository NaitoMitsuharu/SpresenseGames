"""Create the empty Light Box map and its sole particle material; no solid scenery assets."""
from pathlib import Path
import runpy
import unreal
assets=unreal.EditorAssetLibrary
if not assets.does_asset_exist('/Game/Maps/LightBox_World'):
    world=unreal.AssetToolsHelpers.get_asset_tools().create_asset('LightBox_World','/Game/Maps',unreal.World,unreal.WorldFactory())
    assets.save_loaded_asset(world)
runpy.run_path(str(Path(unreal.Paths.project_dir())/'Tools/CreatePointSculptureAssets.py'))
