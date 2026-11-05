"""Create only the Light Box map/materials; leave existing games untouched."""
import unreal

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary


def material(name, glow=False):
    path = "/Game/LightBox/" + name
    if assets.does_asset_exist(path):
        return
    m = tools.create_asset(name, "/Game/LightBox", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1) if glow else unreal.LinearColor(.012, .024, .04, 1))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    assets.save_loaded_asset(m)


material("M_LightSurface")
material("M_LightGlow", True)
# Rebuild only our materials so old installations receive the lighting upgrade.
def node(m, cls, **props):
    n = mel.create_material_expression(m, getattr(unreal, "MaterialExpression" + cls))
    for key, value in props.items(): n.set_editor_property(key, value)
    return n


def wire(a, b, slot, output=""):
    ok = mel.connect_material_expressions(a, output, b, slot)
    if not ok and output: ok = mel.connect_material_expressions(a, "", b, slot)
    if not ok and slot == "Input":
        names = mel.get_material_expression_input_names(b)
        for name in ["", *names]:
            if mel.connect_material_expressions(a, "", b, name): ok = True; break
    assert ok, f"Cannot connect {a.get_class().get_name()} to {b.get_class().get_name()}.{slot}"


def scalar(m, name, value):
    return node(m, "ScalarParameter", parameter_name=name, default_value=value)


for name in ("M_LightObject", "M_LightFloor", "M_LightBackdrop"):
    path = "/Game/LightBox/" + name
    m = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, "/Game/LightBox", unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    backdrop = name == "M_LightBackdrop"
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT if backdrop else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("two_sided", name == "M_LightObject")
    m.set_editor_property("used_with_instanced_static_meshes", True)
    color = node(m, "VectorParameter", parameter_name="Color", default_value=unreal.LinearColor(.4,.5,.6,1))
    if backdrop:
        uv = node(m, "TextureCoordinate")
        mask = node(m, "ComponentMask", r=False, g=True, b=False, a=False)
        wire(uv, mask, "Input")
        grad = node(m, "LinearInterpolate", const_a=.35, const_b=1.6)
        wire(mask, grad, "Alpha")
        mult = node(m, "Multiply"); wire(color, mult, "A"); wire(grad, mult, "B")
        mel.connect_material_property(mult, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        base = color
        if name == "M_LightObject":
            vertex = node(m, "VertexColor")
            base = node(m, "Multiply"); wire(color, base, "A", "RGB"); wire(vertex, base, "B", "RGB")
        mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = scalar(m, "Roughness", .48 if name == "M_LightObject" else .7)
        metal = scalar(m, "Metallic", 0)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
        emission = scalar(m, "Emission", 0)
        glow = node(m, "Multiply"); wire(base, glow, "A"); wire(emission, glow, "B")
        mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    assets.save_loaded_asset(m)
if not assets.does_asset_exist("/Game/Maps/LightBox_World"):
    world = tools.create_asset("LightBox_World", "/Game/Maps", unreal.World, unreal.WorldFactory())
    assets.save_loaded_asset(world)
if not assets.does_asset_exist("/Game/LightBox/T_StudioAmbient"):
    source = unreal.load_object(None, "/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")
    assert source, "Studio ambient source missing"
    assert tools.duplicate_asset("T_StudioAmbient", "/Game/LightBox", source)
    assets.save_asset("/Game/LightBox/T_StudioAmbient")
unreal.log("CreateLightBoxAssets: done")
