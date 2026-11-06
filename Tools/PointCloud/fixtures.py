"""Generic target assemblies used for renderer tests, not a runtime noun-to-preset lookup."""
import copy

def object(id,shape,color,position,size,**kw):
    return dict(id=id,shape=shape,color=color,position=position,size=size,**kw)

def scene(subject='eggplant',count=131072):
    value=dict(schema=3,title=subject,color='#8B4BCB',accent='#84CA54',motion=1,viscosity=.35,swirl=.3,attraction=.5,tilt=1,glow=.65,trail=0,shakeThreshold=1.65,shake='scatter',note='粒が動き、立体の表面を構成します。',particles=dict(count=0),world=dict(background='#030712',floor='#101520',ambient=1,bloom=.35,box=False,keyDirection=[-40,-35,0]),camera=dict(autoFrame=True,yaw=-8,pitch=24,zoom=1,fill=.76),pointCloud=dict(enabled=True,count=count,floor=False,duration=5,arc=65,twist=1.25,stagger=.35))
    if subject=='eggplant':
        value['title']='粒のなす'
        value['objects']=[
            object('body','lathe','#6E27BA',[0,0,160],[110,90,200],profile=[[-.5,0],[-.47,.16],[-.38,.33],[-.2,.47],[0,.5],[.18,.39],[.35,.24],[.47,.12],[.5,0]],curve=[[-12,0,-100],[-7,0,-30],[5,0,40],[20,0,100]]),
            object('calyx','star','#64A539',[18,0,256],[58,56,21]),
            object('stem','tube','#7EB346',[19,0,263],[8,8,30],curve=[[0,0,0],[4,0,14],[15,1,26]],radius=3.3),
        ]
    elif subject=='apple':
        value['title']='粒のりんご'
        value['objects']=[object('body','sphere','#E33C3F',[0,0,155],[140,130,135]),object('stem','tube','#795036',[0,0,216],[8,8,28],curve=[[0,0,0],[3,0,15],[10,0,26]],radius=3.2),object('leaf','leaf','#7FC348',[24,0,232],[47,23,7],rotation=[15,25,5])]
    elif subject=='butterfly':
        value['title']='粒の蝶'
        value['objects']=[object('butterfly','butterfly','#68C5E8',[0,0,160],[140,220,32],rotation=[65,0,0])]
    else:
        value['title']='粒の輪'
        value['objects']=[object('ring','torus','#FFD25A',[0,0,160],[180,180,18],rotation=[78,0,0],radius=7)]
    return copy.deepcopy(value)
