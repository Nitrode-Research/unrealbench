"""Original Jade Ascension kit. Blender metres -> Unreal centimetres, Z up.
Run blender --background -noaudio --python SourceArt/JadeAscension/build_arena.py.
RGB is linear surface colour; vertex alpha selects luminous inlays. No external models.
"""
import bpy, math, random, json
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parent
random.seed(8231)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
STONE=(.17,.23,.26,0); PALE=(.42,.50,.49,0); DARK=(.025,.055,.065,0)
JADE=(.045,.23,.20,0); GOLD=(.48,.28,.085,0); WOOD=(.11,.035,.048,0)
ROOF=(.018,.085,.105,0); LIGHT=(.075,.75,.52,1); AMBER=(1,.32,.065,1)
PINK=(.52,.115,.29,0); LEAF=(.075,.17,.135,0)
parts=[]; catalog=[]
mat=bpy.data.materials.new('JadeSurface');mat.diffuse_color=(.2,.3,.3,1)
def finish(o,color):
    bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.data.materials.clear();o.data.materials.append(mat)
    a=o.data.color_attributes.new(name='Surface',type='FLOAT_COLOR',domain='CORNER')
    for c in a.data:c.color=color
    parts.append(o);return o
def box(size,at,c=STONE,b=.025,rot=(0,0,0)):
    bpy.ops.mesh.primitive_cube_add(size=1,location=at,rotation=rot);o=bpy.context.object;o.scale=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if b:
        m=o.modifiers.new('Dressed edges','BEVEL');m.width=b;m.segments=2
        bpy.ops.object.modifier_apply(modifier=m.name)
        m=o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');m.keep_sharp=True
        bpy.ops.object.modifier_apply(modifier=m.name)
    return finish(o,c)
def cyl(r,h,at,c=GOLD,n=16,rot=(0,0,0),r2=None):
    bpy.ops.mesh.primitive_cone_add(vertices=n,radius1=r,radius2=r if r2 is None else r2,depth=h,location=at,rotation=rot)
    return finish(bpy.context.object,c)
def orb(r,at,c,scale=(1,1,1),sub=2):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=r,location=at);o=bpy.context.object;o.scale=scale
    for p in o.data.polygons:p.use_smooth=True
    return finish(o,c)
def beam(a,b,r,c=GOLD,n=10):
    d=Vector(b)-Vector(a);o=cyl(r,d.length,(Vector(a)+Vector(b))/2,c,n)
    o.rotation_euler=d.to_track_quat('Z','Y').to_euler();return o
def ring(r,t,at,c=GOLD,rot=(0,0,0),n=80):
    bpy.ops.mesh.primitive_torus_add(major_radius=r,minor_radius=t,major_segments=n,minor_segments=8,location=at,rotation=rot)
    o=bpy.context.object
    for p in o.data.polygons:p.use_smooth=True
    return finish(o,c)
def mesh(name,v,f,c,uv=None):
    m=bpy.data.meshes.new(name);m.from_pydata(v,[],f);m.update()
    o=bpy.data.objects.new(name,m);bpy.context.collection.objects.link(o)
    if uv:
        u=m.uv_layers.new()
        for poly in m.polygons:
            for li in poly.loop_indices:u.data[li].uv=uv[m.loops[li].vertex_index]
    return finish(o,c)
def export(name,collision='none'):
    global parts
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:o.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object;o.name=name
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    m=o.modifiers.new('Triangulate','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.ops.export_scene.fbx(filepath=str(R/'Exports'/f'{name}.fbx'),use_selection=True,object_types={'MESH'},global_scale=1,
      apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True,
      mesh_smooth_type='FACE',use_tspace=False,add_leaf_bones=False,bake_anim=False,colors_type='LINEAR')
    catalog.append(dict(name=name,triangles=len(o.data.polygons),collision=collision))
    parts=[];o.hide_set(True)
def roof(w,d,z,h=1.3):
    # Continuous curved hip roof with lifted eaves, gold edge rails and tile ribs.
    N=20;v=[];f=[]
    def xyz(x,y):
        t=max(abs(x),abs(y));return (x*w/2,y*d/2,z+h*(1-t)**1.2+.35*t**9)
    for j in range(N+1):
        for i in range(N+1):v.append(xyz(-1+2*i/N,-1+2*j/N))
    for j in range(N):
        for i in range(N):a=j*(N+1)+i;f.append((a,a+1,a+N+2,a+N+1))
    mesh('Swept glazed roof',v,f,ROOF)
    for axis in [0,1]:
        for s in [-1,1]:
            for i in range(N):
                p=[-1+2*i/N,s];q=[-1+2*(i+1)/N,s]
                if axis:p.reverse();q.reverse()
                beam(xyz(*p),xyz(*q),.045,GOLD,6)
    for i in range(-8,9):
        x=i/9
        for s in [-1,1]:
            for j in range(5):
                a=xyz(x,s*j/5);b=xyz(x,s*(j+1)/5)
                beam((a[0],a[1],a[2]+.025),(b[0],b[1],b[2]+.025),.018,JADE,5)
    for x in [-1,1]:
        for y in [-1,1]:
            a=xyz(x,y);beam(a,(a[0]*1.03,a[1]*1.03,a[2]+.45),.075,GOLD)
def pedestal(r,z=0):
    cyl(r,.25,(0,0,z+.125),STONE,8);cyl(r*.87,.18,(0,0,z+.34),GOLD,8)
    cyl(r*.75,.32,(0,0,z+.57),JADE,8)

# Flat playable deck: all surface relief lies at/below the Z=0 fighting plane.
box((40,12,.6),(0,0,-.3),STONE,.06)
for w,d,z in [(40.7,12.7,-.8),(41.3,13.3,-1.35)]:box((w,d,.5),(0,0,z),DARK,.09)
for y in [-5.8,5.8]:
    box((39.8,.10,.035),(0,y,-.02),GOLD,.01)
    box((39.8,.04,.025),(0,y-.15,-.018),LIGHT,.005)
for x in [-19.7,19.7]:box((.1,11.5,.035),(x,0,-.02),GOLD,.01)
export('SM_JA_CombatTerrace','box')

# Moon gate is an open architectural silhouette with layered circular bronze work.
for x in [-5.2,5.2]:
    for w,h,z in [(1.8,.35,.175),(1.4,.3,.5),(1.0,5.9,3.6),(1.55,.3,6.7)]:box((w,1.5,h),(x,0,z),STONE if h<1 else WOOD,.06)
    for z in [.9,2.1,5.8,6.4]:box((1.15,1.65,.13),(x,0,z),GOLD,.025)
    box((.085,.055,4.4),(x,.78,3.6),LIGHT,.01)
for r,t,c in [(3.35,.21,STONE),(3.11,.075,GOLD),(3.00,.035,LIGHT),(3.55,.03,GOLD)]:ring(r,t,(0,0,3.85),c,(math.pi/2,0,0),112)
for i in range(16):
    a=2*math.pi*i/16
    box((.16,.12,.48),(3.34*math.sin(a),.18,3.85+3.34*math.cos(a)),GOLD,.025,(0,a,0))
box((11.7,1.9,.3),(0,0,6.8),WOOD,.08)
for x in range(-5,6):
    box((.5,2.3,.24),(x,0,7.05),GOLD,.04)
roof(13.4,4,7.25,1.3)
box((2.8,.2,.6),(0,1.03,6.76),DARK,.07)
for x in [-.7,0,.7]:
    beam((x-.15,1.18,6.7),(x+.15,1.18,6.9),.035,GOLD)
    beam((x-.15,1.18,6.9),(x+.15,1.18,6.7),.035,GOLD)
export('SM_JA_MoonGate')

for tier in range(4):
    w=6.4-tier*1.3;z=tier*2.4
    box((w,w,.28),(0,0,z+.14),STONE,.06)
    box((w*.76,w*.76,2.0),(0,0,z+1.1),WOOD,.04)
    for x in [-1,1]:
        for y in [-1,1]:cyl(.16,2.1,(x*w*.39,y*w*.39,z+1.2),GOLD,12)
    for side in [-1,1]:
        for i in range(-2,3):
            box((w*.105,.08,.92),(i*w*.13,side*w*.385,z+1.3),AMBER,.015)
            for j in [-.3,0,.3]:box((w*.13,.12,.035),(i*w*.13,side*w*.39,z+1.3+j),WOOD,.008)
    roof(w*1.3,w*1.3,z+2.2,.95)
cyl(.2,1.3,(0,0,10.4),GOLD,12,r2=.015);orb(.22,(0,0,10.1),LIGHT)
export('SM_JA_Pagoda')

# Reusable stone balustrade, placed only outside the combat safety envelope.
box((3.8,.65,.22),(0,0,.11),STONE,.04)
for x in [-1.7,0,1.7]:
    box((.32,.42,1.35),(x,0,.8),PALE,.045);box((.55,.6,.16),(x,0,1.5),GOLD,.03)
    orb(.20,(x,0,1.72),JADE,sub=1)
for z in [.5,1.25]:box((3.4,.2,.16),(0,0,z),PALE,.025)
for i in range(-5,6):box((.10,.16,.66),(i*.28,0,.87),JADE,.015)
export('SM_JA_Balustrade')

pedestal(.65)
cyl(.19,1.25,(0,0,1.2),STONE,8)
box((.85,.85,.14),(0,0,1.82),GOLD)
box((.62,.62,.69),(0,0,2.23),AMBER,.05)
for x in [-.34,.34]:
    for y in [-.34,.34]:beam((x,y,1.86),(x,y,2.58),.045,WOOD)
roof(1.3,1.3,2.6,.45);orb(.08,(0,0,3.13),GOLD)
export('SM_JA_Lantern')

pedestal(1.25)
for i in range(8):
    a=i*math.tau/8;x,y=math.cos(a),math.sin(a)
    # Sculptural lotus petals cup an illuminated jade pearl.
    v=[(.25*x,.25*y,.75),(1.2*x+.28*y,1.2*y-.28*x,1.1),(1.5*x,1.5*y,1.65),(1.2*x-.28*y,1.2*y+.28*x,1.1),(.7*x,.7*y,1.3)]
    mesh('Lotus petal',v,[(0,1,4),(1,2,4),(2,3,4),(3,0,4),(0,3,2,1)],GOLD)
orb(.55,(0,0,1.48),LIGHT);ring(.72,.04,(0,0,1.55),LIGHT)
export('SM_JA_LotusBrazier')

pedestal(1.1)
box((.65,.32,5.3),(0,0,3.35),JADE,.12,rot=(0,.1,0))
box((.09,.07,4.5),(.23,.2,3.5),LIGHT,.025,rot=(0,.1,0))
for z in [1.3,2.8,4.3,5.8]:ring(.40,.045,(.1*z,0,z),GOLD,n=24)
cyl(.4,.7,(.35,0,6.15),GOLD,4,r2=0)
export('SM_JA_SpiritObelisk')

def branch(a,b,r):beam(a,b,r,WOOD,9)
branch((0,0,0),(.35,0,2),.36);branch((.35,0,2),(-.3,.1,3.5),.27);branch((-.3,.1,3.5),(.2,.1,5.4),.18)
tips=[]
for i in range(13):
    a=i*2.4;z=2.6+(i%5)*.58;start=(0,0,z)
    end=(math.cos(a)*(2+(i%3)*.48),math.sin(a)*(1.3+(i%4)*.2),z+.8)
    mid=((start[0]+end[0])*.5,(start[1]+end[1])*.5,z+.25)
    branch(start,mid,.14);branch(mid,end,.075);tips.append(end)
for x,y,z in tips:
    for j in range(7):
        at=(x+random.uniform(-.8,.8),y+random.uniform(-.65,.65),z+random.uniform(-.1,.6))
        c=random.choice([PINK,(.72,.29,.41,0),(.38,.075,.23,0),(.64,.22,.35,0)])
        orb(random.uniform(.30,.59),at,c,(1.3,1,.55),sub=1)
        for k in range(4):
            a=random.random()*math.tau;orb(.09,(at[0]+.5*math.cos(a),at[1]+.5*math.sin(a),at[2]+.20),(.85,.46,.55,0),(1,.6,.4),sub=1)
for a in range(6):branch((0,0,.4),(.7*math.cos(a),.7*math.sin(a),.03),.13)
export('SM_JA_BlossomTree')

v=[];f=[];N=13
for j,(z,r) in enumerate([(-7,.3),(-5.5,1),(-3,2.3),(-.7,3.2),(0,2.5)]):
    for i in range(N):
        a=i*math.tau/N;rr=r*random.uniform(.77,1.15);v.append((rr*math.cos(a),rr*math.sin(a),z+random.uniform(-.2,.2)))
for j in range(4):
    for i in range(N):a=j*N+i;b=j*N+(i+1)%N;f.extend([(a,b,a+N),(b,b+N,a+N)])
f.append(tuple(range(4*N,5*N)))
o=mesh('Weathered levitating crag',v,f,STONE)
colors=o.data.color_attributes.active_color
for poly in o.data.polygons:
    shade=random.uniform(.65,1.3)
    for li in poly.loop_indices:colors.data[li].color=(.09*shade,.16*shade,.19*shade,0)
for i in range(9):
    a=i*2.4;r=random.uniform(.3,1.8);orb(.65,(r*math.cos(a),r*math.sin(a),-.05),LEAF,(1.2,1,.28),sub=1)
export('SM_JA_FloatingCrag')

# Embroidered hanging banner, modeled folds and hem. No simulated cloth near combat.
for x in [-1,1]:beam((x*.7,0,0),(x*.7,0,4.8),.055,GOLD)
beam((-.9,0,4.7),(.9,0,4.7),.075,GOLD)
v=[];f=[]
for j in range(17):
    t=j/16
    for i in range(9):
        x=-.63+i*.1575;v.append((x,.13*math.sin(i*.75+t*4),4.6-t*3.4))
for j in range(16):
    for i in range(8):a=j*9+i;f.append((a,a+1,a+10,a+9))
mesh('Silk folds',v,f,(.27,.035,.08,0))
ring(.38,.035,(0,.19,3.6),GOLD,(math.pi/2,0,0),40)
for x in [-.59,.59]:beam((x,0,1.2),(x,0,4.6),.02,GOLD)
for i in range(9):beam((-.6+i*.15,0,1.2),(-.6+i*.15,0,.95),.018,GOLD)
export('SM_JA_PrayerBanner')

# Terrace at a lower background elevation, and central processional stair.
box((17,10,.8),(0,0,-.4),STONE,.12)
for i in range(10):box((8,1,.18),(0,4.5-i,.09+i*.18),PALE,.025)
export('SM_JA_GardenTerrace')

# Animated effects remain shader-owned, no collision or per-object Tick.
ring(1,.025,(0,0,0),LIGHT,(math.pi/2,0,0),80)
for i in range(12):
    a=i*math.tau/12;box((.025,.025,.13),(math.sin(a),0,math.cos(a)),GOLD,.005,(0,a,0))
export('SM_JA_SpiritHalo')

# Curved cyclorama: UV0 maps one panorama, inward facing from the fighting plane.
v=[];f=[];uv=[];N=64
for row,z in enumerate([-65,135]):
    for i in range(N+1):
        a=-1.25+2.5*i/N;v.append((220*math.sin(a),-220*math.cos(a),z));uv.append((i/N,row))
for i in range(N):f.append((i,i+1,i+N+2,i+N+1))
mesh('Distant painted sky',v,f,(1,1,1,0),uv);export('SM_JA_CelestialCyclorama')

# Arrange only the editable blend as a gallery; FBXs above retain their local origin.
for i,o in enumerate(bpy.context.scene.objects):
    o.location=(i%4*28,i//4*28,0)
    o.hide_set('Cyclorama' in o.name)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.shading.color_type='VERTEX'
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1
bpy.ops.wm.save_as_mainfile(filepath=str(R/'JadeAscension.blend'))
(R/'Exports'/'manifest.json').write_text(json.dumps(catalog,indent=2)+'\n')
print('JADE_ASSET_BUILD_COMPLETE',sum(x['triangles'] for x in catalog),len(catalog))
