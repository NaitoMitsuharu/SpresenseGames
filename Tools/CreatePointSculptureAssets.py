"""Build the precompiled GPU vertex-texture point renderer; no Niagara/editor required at runtime."""
import struct
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir())
assets=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools();mel=unreal.MaterialEditingLibrary
placeholder='/Game/LightBox/T_PointData'
if not assets.does_asset_exist(placeholder):
    file=root/'Saved/point-data.tga';file.write_bytes(struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,2,2,32,0x28)+bytes([0,0,0,255])*4)
    task=unreal.AssetImportTask();task.filename=str(file);task.destination_path='/Game/LightBox';task.destination_name='T_PointData';task.automated=True;task.save=True;tools.import_asset_tasks([task])
texture=assets.load_asset(placeholder);texture.set_editor_property('srgb',False);texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP);assets.save_loaded_asset(texture)
path='/Game/LightBox/M_PointSculpture'
m=assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset('M_PointSculpture','/Game/LightBox',unreal.Material,unreal.MaterialFactoryNew())
mel.delete_all_material_expressions(m);m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED);m.set_editor_property('two_sided',True);m.set_editor_property('used_with_instanced_static_meshes',True);m.set_editor_property('max_world_position_offset_displacement',1000.0);m.set_editor_property('float_precision_mode',unreal.MaterialFloatPrecisionMode.MFPM_FULL)
def node(cls,**props):
    n=mel.create_material_expression(m,getattr(unreal,'MaterialExpression'+cls))
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def data(cls,**props):
    value=getattr(unreal,cls)()
    for k,v in props.items():value.set_editor_property(k,v)
    return value
def wire(a,b,name,output=''):
    ok=mel.connect_material_expressions(a,output,b,name)
    if not ok and name=='Input':ok=mel.connect_material_expressions(a,output,b,'')
    assert ok,(name,output)
inputs={
 'Index':node('PerInstanceCustomData',data_index=0),
 'Eye':node('CameraPositionWS'),
 'Viewport':node('ViewSize'),
 'HalfFov':node('ViewProperty',property=unreal.MaterialExposedViewProperty.MEVP_TAN_HALF_FIELD_OF_VIEW),
 'QuadUV':node('TextureCoordinate'),
 'World':node('WorldPosition',world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS),
 'Right':node('VectorParameter',parameter_name='Right',default_value=unreal.LinearColor(1,0,0,0)),
 'Up':node('VectorParameter',parameter_name='Up',default_value=unreal.LinearColor(0,0,1,0)),
 'AxisX':node('VectorParameter',parameter_name='AxisX',default_value=unreal.LinearColor(1,0,0,0)),
 'AxisY':node('VectorParameter',parameter_name='AxisY',default_value=unreal.LinearColor(0,1,0,0)),
 'AxisZ':node('VectorParameter',parameter_name='AxisZ',default_value=unreal.LinearColor(0,0,1,0)),
 'Origin':node('VectorParameter',parameter_name='Origin',default_value=unreal.LinearColor(0,0,0,0)),
 'ObjectX':node('VectorParameter',parameter_name='ObjectX',default_value=unreal.LinearColor(1,0,0,0)),
 'ObjectY':node('VectorParameter',parameter_name='ObjectY',default_value=unreal.LinearColor(0,1,0,0)),
 'ObjectZ':node('VectorParameter',parameter_name='ObjectZ',default_value=unreal.LinearColor(0,0,1,0)),
 'DataWidth':node('ScalarParameter',parameter_name='DataWidth',default_value=256),
 'Phase':node('ScalarParameter',parameter_name='Phase',default_value=1),
 'ColourPhase':node('ScalarParameter',parameter_name='ColourPhase',default_value=-1),
 'Animated':node('ScalarParameter',parameter_name='Animated',default_value=0),
 'VertexWidth':node('ScalarParameter',parameter_name='VertexWidth',default_value=1),
 'FrameMix':node('ScalarParameter',parameter_name='FrameMix',default_value=1),
 'Path':node('VectorParameter',parameter_name='Path',default_value=unreal.LinearColor(65,1.25,.35,0)),
 'Center':node('VectorParameter',parameter_name='Center',default_value=unreal.LinearColor(0,0,160,0)),
 # Sensor forces (see PointSculpture.hlsl): two travelling waves, breath, magnet and warmth.
 'Wave0':node('VectorParameter',parameter_name='Wave0',default_value=unreal.LinearColor(0,0,0,-100)),
 'Wave1':node('VectorParameter',parameter_name='Wave1',default_value=unreal.LinearColor(0,0,0,-100)),
 'WaveForce':node('VectorParameter',parameter_name='WaveForce',default_value=unreal.LinearColor(0,0,1000,100)),
 'Wind':node('VectorParameter',parameter_name='Wind',default_value=unreal.LinearColor(0,0,0,0)),
 'Pull':node('VectorParameter',parameter_name='Pull',default_value=unreal.LinearColor(0,0,0,0)),
 'Heat':node('VectorParameter',parameter_name='Heat',default_value=unreal.LinearColor(0,0,0,0)),
 'SensorClock':node('ScalarParameter',parameter_name='SensorClock',default_value=0),
}
rgba={'Path','Wave0','Wave1','WaveForce','Wind','Pull','Heat'}
for name in ['FromTex','ToTex','LinksTex','WeightsTex','VertexFrom','VertexTo']:
    inputs[name]=node('TextureObjectParameter',parameter_name=name,texture=texture,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
# Each batch vertex carries its immutable submission index in full-precision UV1.
# The order map changes only submission order; geometry, colour and grain IDs remain intact.
draw_index=node('ComponentMask',r=True,g=False,b=False,a=False)
wire(node('TextureCoordinate',coordinate_index=1),draw_index,'Input')
order=node('TextureObjectParameter',parameter_name='OrderTex',texture=texture,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
index=node('Custom',code='float2 uv=float2(frac((DrawIndex+.5)/DataWidth),(floor(DrawIndex/DataWidth)+.5)/DataWidth);return Texture2DSampleLevel(OrderTex,OrderTexSampler,uv,0).r;',output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
index.set_editor_property('inputs',[data('CustomInput',input_name=n) for n in ['DrawIndex','DataWidth','OrderTex']])
wire(draw_index,index,'DrawIndex');wire(inputs['DataWidth'],index,'DataWidth');wire(order,index,'OrderTex');inputs['Index']=index
custom=node('Custom',code=(root/'Tools/PointCloud/PointSculpture.hlsl').read_text(),output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property('inputs',[data('CustomInput',input_name=name) for name in inputs])

for name,n in inputs.items():wire(n,custom,name,'RGBA' if name in rgba else '')
mel.connect_material_property(custom,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
color_inputs={name:inputs[name] for name in ['Index','DataWidth','Phase','Path','ColourPhase']}
for name in ['FromColor','ToColor']:color_inputs[name]=node('TextureObjectParameter',parameter_name=name,texture=texture,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
color_code='struct Rng {float H(uint i){uint x=i+747796405u+2891336453u;x^=x>>16;x*=2246822519u;x^=x>>13;x*=3266489917u;x^=x>>16;return float(x&0x00ffffffu)/16777216.;}};\nuint id=(uint)(Index+.5);float2 uv=float2(frac((Index+.5)/DataWidth),(floor(Index/DataWidth)+.5)/DataWidth);\nfloat3 b=Texture2DSampleLevel(ToColor,ToColorSampler,uv,0).rgb;\n[branch] if(ColourPhase>=0)return lerp(Texture2DSampleLevel(FromColor,FromColorSampler,uv,0).rgb,b,ColourPhase);\n[branch] if(Phase>=1)return b;\nRng r;float d=r.H(id)*Path.z;float u=saturate((Phase-d)/(1-d));float s=1-pow(1-u,3);\nreturn lerp(Texture2DSampleLevel(FromColor,FromColorSampler,uv,0).rgb,b,s);'
color_vertex=node('Custom',code=color_code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
color_vertex.set_editor_property('inputs',[data('CustomInput',input_name=name) for name in color_inputs])
for name,n in color_inputs.items():wire(n,color_vertex,name,'RGBA' if name=='Path' else '')
tint=node('VertexInterpolator');wire(color_vertex,tint,'Input')
colour=node('Custom',code='float2 q=UV*2-1;return Tint.rgb*(.9+1.1*sqrt(saturate(1-dot(q,q))));',output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
colour.set_editor_property('inputs',[data('CustomInput',input_name='Tint'),data('CustomInput',input_name='UV')]);wire(tint,colour,'Tint');wire(inputs['QuadUV'],colour,'UV')
mel.connect_material_property(colour,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mask=node('Custom',code='float2 q=UV*2-1;return 1-dot(q,q);',output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1);mask.set_editor_property('inputs',[data('CustomInput',input_name='UV')]);wire(inputs['QuadUV'],mask,'UV');mel.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY_MASK)
mel.recompile_material(m);assets.save_loaded_asset(m)
unreal.log('CreatePointSculptureAssets done')
