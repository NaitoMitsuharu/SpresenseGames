"""Regenerate only scenery materials; preserves maps and ship materials.

UnrealEditor-Cmd.exe SpresenseMaze.uproject -run=pythonscript -script=Tools/CreateFlightScenery.py -unattended -nullrhi
Analytic surface detail: no external textures, plugins or volumetric rendering.
"""
import unreal

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def material(name, translucent=False):
    path = "/Game/Flight/" + name
    m = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        name, "/Game/Flight", unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("two_sided", translucent)
    if translucent:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    return m


def node(m, cls):
    return mel.create_material_expression(m, cls)


def custom(m, code, inputs, vector=False):
    n = node(m, unreal.MaterialExpressionCustom)
    n.set_editor_property("code", code)
    n.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3 if vector else unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for k in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", k)
        pins.append(pin)
    n.set_editor_property("inputs", pins)
    for k, v in inputs.items():
        mel.connect_material_expressions(v, "", n, k)
    return n


def scalar(m, value, prop):
    n = node(m, unreal.MaterialExpressionConstant)
    n.set_editor_property("r", value)
    mel.connect_material_property(n, "", prop)


def finish(m):
    mel.recompile_material(m)
    assets.save_loaded_asset(m)


ROCK_NOISE = """
struct RockNoise {
    float hash(float2 p) {
        p = frac(p * float2(123.34,456.21));
        p += dot(p,p+45.32);
        return frac(p.x*p.y);
    }
    float value(float2 p) {
        float2 i=floor(p), f=frac(p); f=f*f*(3-2*f);
        return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),
                    lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
    }
};
"""

P = unreal.MaterialProperty
m = material("M_FlightRock")
world = node(m, unreal.MaterialExpressionWorldPosition)
vertex = node(m, unreal.MaterialExpressionVertexColor)
detail = custom(m, ROCK_NOISE + """
RockNoise n;
float2 uv = float2(W.x*0.0027+W.y*0.0018, W.z*0.004);
float broad = n.value(uv);
float chips = n.value(uv*4.7 + broad*1.8);
float grain = n.value(uv*17.3);
float footprint = max(length(ddx(W)),length(ddy(W)));
grain = lerp(grain,0.5,saturate(footprint*0.055));
float strata = pow(saturate(0.5+0.5*sin(W.z*0.026+broad*3.0)), 12);
return (0.69 + broad*0.25 + chips*0.13 + grain*0.09 - strata*0.12).xxx;
""", {"W": world}, True)
mul = node(m, unreal.MaterialExpressionMultiply)
mel.connect_material_expressions(vertex, "", mul, "A")
mel.connect_material_expressions(detail, "", mul, "B")
mel.connect_material_property(mul, "", P.MP_BASE_COLOR)
rough = custom(m, "return lerp(0.3,0.94,smoothstep(60,300,W.z));", {"W": world})
mel.connect_material_property(rough, "", P.MP_ROUGHNESS)
vertex_normal = node(m, unreal.MaterialExpressionVertexNormalWS)
m.set_editor_property("tangent_space_normal", False)
rock_normal = custom(m, ROCK_NOISE + """
RockNoise n;
float2 uv = float2(W.x*0.02+W.y*0.013,W.z*0.025);
float h = n.value(uv);
float2 d = (float2(n.value(uv+float2(0.08,0)),n.value(uv+float2(0,0.08)))-h)/0.08;
float3 g = float3(d.x*0.1,d.x*0.065,d.y*0.12);
float fade = 1-saturate(max(length(ddx(W)),length(ddy(W)))*0.035);
return normalize(N-(g-N*dot(g,N))*fade);
""", {"W": world, "N": vertex_normal}, True)
mel.connect_material_property(rock_normal, "", P.MP_NORMAL)
scalar(m, 0.25, P.MP_SPECULAR)
finish(m)

m = material("M_FlightOcean")
world = node(m, unreal.MaterialExpressionWorldPosition)
time = node(m, unreal.MaterialExpressionTime)
normal = custom(m, """
float2 p = W.xy;
float a = dot(p, float2(0.0041,0.0017)) + T*0.85;
float b = dot(p, float2(-0.0023,0.0063)) + T*1.13;
float c = dot(p, float2(0.017,0.011)) - T*1.8 + sin(a)*1.3;
float d = dot(p, float2(-0.032,0.025)) + T*2.1 + sin(b)*0.8;
float fade = 1-saturate(max(length(ddx(p)),length(ddy(p)))*0.015);
float2 n = float2(0.92,0.38)*cos(a)*0.085 + float2(-0.34,0.94)*cos(b)*0.065;
n += (float2(0.84,0.54)*cos(c)*0.042 + float2(-0.79,0.61)*cos(d)*0.028)*fade;
return normalize(float3(n,1));
""", {"W": world, "T": time}, True)
mel.connect_material_property(normal, "", P.MP_NORMAL)
color = custom(m, """
float v = 0.5+0.5*sin(W.x*0.00018+sin(W.y*0.0003));
return lerp(float3(0.012,0.044,0.061),float3(0.027,0.095,0.105),v);
""", {"W": world}, True)
mel.connect_material_property(color, "", P.MP_BASE_COLOR)
scalar(m, 0.30, P.MP_ROUGHNESS)
scalar(m, 0.65, P.MP_SPECULAR)
finish(m)

for name, foam in [("M_FlightCloud", False), ("M_FlightFoam", True)]:
    m = material(name, True)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate)
    density = custom(m, """
float2 p = (UV-0.5)*2;
float r = dot(p,p);
float n = 0.65+0.16*sin(p.x*11+sin(p.y*9)*2)+0.12*cos(p.y*15+p.x*7);
return saturate((1-r)*1.6)*saturate((1-r)*1.6)*n*0.7;
""" if not foam else """
float2 p = (UV-0.5)*2;
float n = 0.5+0.5*sin(p.x*26+sin(p.y*11)*2);
return pow(saturate(1-dot(p,p)),2)*n*0.25;
""", {"UV": uv})
    mel.connect_material_property(density, "", P.MP_OPACITY)
    color = node(m, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property("constant", unreal.LinearColor(0.65, 0.72, 0.76) if foam else unreal.LinearColor(0.56, 0.61, 0.65))
    mel.connect_material_property(color, "", P.MP_EMISSIVE_COLOR)
    finish(m)

m = material("M_FlightStructure")
world = node(m, unreal.MaterialExpressionWorldPosition)
normal = node(m, unreal.MaterialExpressionVertexNormalWS)
vertex = node(m, unreal.MaterialExpressionVertexColor)
paint = node(m, unreal.MaterialExpressionVectorParameter)
paint.set_editor_property("parameter_name", "BaseColor")
paint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1))
weather = custom(m, """
float3 an = abs(N);
float2 p = an.z>0.6 ? W.xy : (an.x>an.y ? W.yz : W.xz);
float2 uv = p/float2(1400,500);
uv.x += floor(uv.y)*0.5;
float2 edge = min(frac(uv),1-frac(uv))*float2(1400,500);
float footprint = max(length(ddx(p)),length(ddy(p)));
float seam = 1-smoothstep(1,3+footprint,min(edge.x,edge.y));
float scratch = pow(saturate(0.5+0.5*sin(p.x*1.71+sin(p.y*0.013)*9)),30);
scratch *= 1-saturate(footprint*0.3);
float stain = sin(p.x*0.0017+p.y*0.0031)*sin(p.y*0.009+sin(p.x*0.002));
return float3(0.93+stain*0.08-seam*0.11+scratch*0.09,
              0.94+stain*0.07-seam*0.11+scratch*0.08,
              0.94+stain*0.06-seam*0.10+scratch*0.07);
""", {"W": world, "N": normal}, True)
mul = node(m, unreal.MaterialExpressionMultiply)
mel.connect_material_expressions(vertex, "", mul, "A")
mel.connect_material_expressions(paint, "", mul, "B")
mul2 = node(m, unreal.MaterialExpressionMultiply)
mel.connect_material_expressions(mul, "", mul2, "A")
mel.connect_material_expressions(weather, "", mul2, "B")
mel.connect_material_property(mul2, "", P.MP_BASE_COLOR)
for name, value, prop in [("Metallic", 0.5, P.MP_METALLIC), ("Roughness", 0.55, P.MP_ROUGHNESS)]:
    n = node(m, unreal.MaterialExpressionScalarParameter)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", value)
    mel.connect_material_property(n, "", prop)
finish(m)

for name, waterfall in [("M_FlightShoreFoam", False), ("M_FlightWaterfall", True)]:
    m = material(name, True)
    uv, time = node(m, unreal.MaterialExpressionTextureCoordinate), node(m, unreal.MaterialExpressionTime)
    opacity = custom(m, """
float streams = 0.55+0.25*sin(UV.x*120+sin(UV.y*16-T*5)*2);
float surge = 0.6+0.4*sin(UV.y*55-T*12+UV.x*8);
return smoothstep(0,0.12,UV.x)*smoothstep(0,0.12,1-UV.x)*streams*surge*0.8;
""" if waterfall else """
float broken = 0.55+0.3*sin(UV.x*180+sin(UV.x*53)*3-T*1.5);
float wave = sin(UV.y*12-T*2+sin(UV.x*60));
return pow(saturate(1-abs(UV.y*2-1)),1.5)*saturate(broken+wave*0.24)*0.65;
""", {"UV": uv, "T": time})
    mel.connect_material_property(opacity, "", P.MP_OPACITY)
    color = node(m, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property("constant", unreal.LinearColor(0.58, 0.73, 0.77) if waterfall else unreal.LinearColor(0.72, 0.8, 0.81))
    mel.connect_material_property(color, "", P.MP_EMISSIVE_COLOR)
    finish(m)

unreal.log("CreateFlightScenery: saved seven scenery materials")
