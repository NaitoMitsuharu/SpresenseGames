"""Create the toy maze's lit, cookable enamel and fine-grain wood materials.

Run with UnrealEditor-Cmd -run=pythonscript -script=Tools/CreateToyMaterials.py.
Only these two materials are updated; maps and player data are untouched.
"""
import unreal

assets = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def make(name, wood=False):
    path = '/Game/Materials/' + name
    material = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if material:
        mel.delete_all_material_expressions(material)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('used_with_instanced_static_meshes', True)

    def param(cls, name, value):
        node = mel.create_material_expression(material, cls)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', value)
        return node

    color = param(unreal.MaterialExpressionVectorParameter, 'BaseColor', unreal.LinearColor(.5, .7, .5, 1))
    output = color
    if wood:
        # World-space grain continues across merged floor and frame instances.
        pos = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition)
        scale = mel.create_material_expression(material, unreal.MaterialExpressionMultiply)
        grain_scale = param(unreal.MaterialExpressionVectorParameter, 'GrainScale', unreal.LinearColor(.012, .28, .012, 1))
        mel.connect_material_expressions(pos, '', scale, 'A')
        mel.connect_material_expressions(grain_scale, '', scale, 'B')
        noise = mel.create_material_expression(material, unreal.MaterialExpressionNoise)
        noise.set_editor_property('levels', 2)
        mel.connect_material_expressions(scale, '', noise, 'Position')
        amplitude = mel.create_material_expression(material, unreal.MaterialExpressionMultiply)
        amplitude.set_editor_property('const_b', .12)
        mel.connect_material_expressions(noise, '', amplitude, 'A')
        bias = mel.create_material_expression(material, unreal.MaterialExpressionAdd)
        bias.set_editor_property('const_b', .88)
        mel.connect_material_expressions(amplitude, '', bias, 'A')
        output = mel.create_material_expression(material, unreal.MaterialExpressionMultiply)
        mel.connect_material_expressions(color, '', output, 'A')
        mel.connect_material_expressions(bias, '', output, 'B')
    mel.connect_material_property(output, '', unreal.MaterialProperty.MP_BASE_COLOR)
    for name, value, prop in [('Roughness', .25, unreal.MaterialProperty.MP_ROUGHNESS),
                              ('Metallic', 0., unreal.MaterialProperty.MP_METALLIC),
                              ('Specular', .5, unreal.MaterialProperty.MP_SPECULAR)]:
        node = param(unreal.MaterialExpressionScalarParameter, name, value)
        mel.connect_material_property(node, '', prop)
    mel.layout_material_expressions(material)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)


make('M_ToyEnamel')
make('M_ToyWood', wood=True)
unreal.log('ToyMaterials: enamel and wood saved')
