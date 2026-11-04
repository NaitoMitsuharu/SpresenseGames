"""Create the flight game's assets that cannot be authored from C++.

Run headless from the project root:
  UnrealEditor-Cmd.exe SpresenseMaze.uproject -run=pythonscript -script=Tools/CreateFlightAssets.py -unattended

Creates
  /Game/Flight/M_FlightSurface   lit opaque: BaseColor x VertexColor, Metallic, Roughness, Specular, Emissive
  /Game/Flight/M_FlightAdditive  unlit additive glow: Color
  /Game/Maps/Title_Select        empty level (TitleGameMode via GameModeMapPrefixes)
  /Game/Maps/Flight_Course       empty level (FlightGameMode via GameModeMapPrefixes)
Everything visible in those levels is spawned in C++.
"""
import unreal
import runpy
from pathlib import Path

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary


def fresh(path, name, cls, factory):
    full = f"{path}/{name}"
    if assets.does_asset_exist(full):
        assets.delete_asset(full)
    return tools.create_asset(name, path, cls, factory)


def param(material, cls, name, value, x, y):
    node = mel.create_material_expression(material, cls, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def surface():
    m = fresh("/Game/Flight", "M_FlightSurface", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_instanced_static_meshes", True)
    base = param(m, unreal.MaterialExpressionVectorParameter, "BaseColor", unreal.LinearColor(1, 1, 1, 1), -700, -200)
    vcol = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -700, 0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, -150)
    mel.connect_material_expressions(base, "", mul, "A")
    mel.connect_material_expressions(vcol, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for i, (name, value, prop) in enumerate([
            ("Metallic", 0.0, unreal.MaterialProperty.MP_METALLIC),
            ("Roughness", 0.5, unreal.MaterialProperty.MP_ROUGHNESS),
            ("Specular", 0.5, unreal.MaterialProperty.MP_SPECULAR)]):
        node = param(m, unreal.MaterialExpressionScalarParameter, name, value, -350, 100 + i * 120)
        mel.connect_material_property(node, "", prop)
    emissive = param(m, unreal.MaterialExpressionVectorParameter, "Emissive", unreal.LinearColor(0, 0, 0, 1), -350, 500)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    assets.save_loaded_asset(m)


def additive():
    m = fresh("/Game/Flight", "M_FlightAdditive", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    color = param(m, unreal.MaterialExpressionVectorParameter, "Color", unreal.LinearColor(1, 1, 1, 1), -400, 0)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    assets.save_loaded_asset(m)


def level(name):
    factory = unreal.WorldFactory()
    world = fresh("/Game/Maps", name, unreal.World, factory)
    assets.save_loaded_asset(world)


surface()
additive()
runpy.run_path(str(Path(__file__).with_name("CreateFlightScenery.py")))
level("Title_Select")
level("Flight_Course")
unreal.log("CreateFlightAssets: done")
