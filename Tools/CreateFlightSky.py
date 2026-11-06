"""Copy UE's mobile-compatible atmosphere dome into the cooked Flight folder.

Run with UnrealEditor-Cmd -run=pythonscript -script=Tools/CreateFlightSky.py.
This only creates the two sky assets; it does not rebuild maps or other materials.
"""
import unreal

assets = unreal.EditorAssetLibrary
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Engine/EngineSky"], True)
sources = {
    "/Game/Flight/M_FlightSky": "/Engine/EngineSky/SkyAtmosphere/SkyAtmosphere_MaterialSkyDome",
    "/Game/Flight/SM_FlightSkyDome": "/Engine/EngineSky/SM_SkySphere",
}
for target, source in sources.items():
    if not assets.does_asset_exist(target):
        assert assets.duplicate_asset(source, target), f"Could not copy {source}"

material = assets.load_asset("/Game/Flight/M_FlightSky")
assert material.get_editor_property("is_sky"), "The dome material must be tagged IsSky"
assert material.get_editor_property("shading_model") == unreal.MaterialShadingModel.MSM_UNLIT
mesh = assets.load_asset("/Game/Flight/SM_FlightSkyDome")
mesh.set_material(0, material)
assets.save_loaded_asset(material)
assets.save_loaded_asset(mesh)
unreal.log("FlightSky: saved mobile IsSky atmosphere dome and material")
