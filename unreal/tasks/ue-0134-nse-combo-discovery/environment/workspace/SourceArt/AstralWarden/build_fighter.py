"""Original Astral Warden armor; preserves the exported Manny bind rig and undersuit weights.
Blender 5.2.1. Run --background --python this file. No downloaded artwork.
"""
import bpy, math, json, bmesh
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'RigReference.blend'))
rig=next(o for o in bpy.data.objects if o.type=='ARMATURE')
body=next(o for o in bpy.data.objects if o.type=='MESH')
# Bake object transforms only. Never edit the original armature or rest pose.
body.data.transform(body.matrix_world.copy());body.parent=None;body.matrix_world.identity();body.name='AW_Undersuit'
mats=[]
def material(name,color,metal,rough,emit=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
 if emit:p.inputs['Emission Color'].default_value=(*color,1);p.inputs['Emission Strength'].default_value=emit
 mats.append(m);return len(mats)-1
SUIT=material('AW_01_WovenGraphite',(.019,.028,.043),.15,.58)
TEAL=material('AW_02_DeepJadeEnamel',(.024,.15,.17),.72,.25)
IVORY=material('AW_03_IvoryCeramic',(.76,.81,.75),.25,.28)
GOLD=material('AW_04_ChampagneTitanium',(.59,.37,.13),.86,.27)
RUBBER=material('AW_05_GraphiteGaskets',(.009,.015,.022),.1,.49)
LIGHT=material('AW_06_AetherLight',(.035,.8,.67),.25,.23,3)
CLOTH=material('AW_07_AubergineTextile',(.15,.025,.075),.02,.66)
body.data.materials.clear()
for m in mats:body.data.materials.append(m)
for p in body.data.polygons:p.material_index=SUIT;p.use_smooth=True
# Keep original hands, joint shapes, weights and bone hierarchy.
bpy.context.view_layer.objects.active=body;body.select_set(True)
d=body.modifiers.new('Undersuit budget','DECIMATE');d.ratio=.62
bpy.ops.object.modifier_apply(modifier=d.name)
# The replacement has its own head/cowl/mask; remove concealed mannequin head geometry.
bm=bmesh.new();bm.from_mesh(body.data)
bmesh.ops.delete(bm,geom=[f for f in bm.faces if all(v.co.z>1.565 for v in f.verts)],context='FACES')
bm.to_mesh(body.data);bm.free()
# Close-fitting compression layer under the segmented armor, preserving original weights.
# A smooth, position-based recess keeps the soft chest under the rigid breastplates
# in crouches and corrective poses. Identical seam vertices receive identical offsets.
clamp=lambda v:max(0,min(1,v))
for v in body.data.vertices:
 x,y,z=v.co
 if y<-.045:
  blend=clamp((z-1.25)/.04)*clamp((1.49-z)/.035)*clamp((.20-abs(x))/.04)*clamp((-y-.045)/.05)
  v.co.y+=.05*blend
parts=[body]
def finish(o,name,mat,bone,bevel=0):
 o.name=name
 bpy.context.view_layer.objects.active=o
 if bevel:
  b=o.modifiers.new('Machined edge radii','BEVEL');b.width=bevel;b.segments=3
  bpy.ops.object.modifier_apply(modifier=b.name)
 for m in mats:o.data.materials.append(m)
 for p in o.data.polygons:p.material_index=mat;p.use_smooth=True
 if bone:
  g=o.vertex_groups.new(name=bone);g.add(list(range(len(o.data.vertices))),1,'REPLACE')
 # Weighted normals keep broad plate faces clean, with curved highlights at the bevel.
 if bevel:
  n=o.modifiers.new('Panel normals','WEIGHTED_NORMAL');n.keep_sharp=True;n.weight=40
  bpy.ops.object.modifier_apply(modifier=n.name)
 parts.append(o);return o
def mesh(name,verts,faces,mat,bone,bevel=0):
 d=bpy.data.meshes.new(name);d.from_pydata(verts,[],faces);d.update();o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o)
 return finish(o,name,mat,bone,bevel)
def plate(name,outline,depth,mat,bone,bevel=.004):
 # Outline points in world coordinates, front surface. Thickness runs toward positive Y.
 n=len(outline);v=[tuple(p) for p in outline]+[(x,y+depth,z) for x,y,z in outline]
 f=[tuple(range(n-1,-1,-1)),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 return mesh(name,v,f,mat,bone,bevel)
def ball(name,loc,scale,mat,bone,segments=32,rings=16):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=segments,ring_count=rings,location=loc);o=bpy.context.object;o.scale=scale
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 return finish(o,name,mat,bone)
def rod(name,a,b,r,mat,bone,verts=12):
 a,b=Vector(a),Vector(b);mid=(a+b)/2
 bpy.ops.mesh.primitive_cylinder_add(vertices=verts,radius=r,depth=(b-a).length,location=mid)
 o=bpy.context.object;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler()
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 return finish(o,name,mat,bone,min(r*.24,.002))
def line(name,points,r,mat,bone):
 for i in range(len(points)-1):rod(name,points[i],points[i+1],r,mat,bone)
def ring(name,center,rx,rz,mat,bone,y=None,r=.004):
 x,cy,z=center
 pts=[(x+rx*math.cos(i*math.tau/48),cy,z+rz*math.sin(i*math.tau/48)) for i in range(49)]
 line(name,pts,r,mat,bone)
def bonepos(name):return rig.matrix_world@rig.data.bones[name].head_local
# Three floating chest plates let the torso articulate instead of bridging the waist.
for s in [-1,1]:
 pts=[(s*.025,-.124,1.425),(s*.16,-.092,1.425),(s*.19,-.085,1.357),(s*.125,-.145,1.285),(s*.022,-.158,1.32)]
 plate('Pectoral ceramic '+str(s),pts,.023,IVORY,'spine_05',.008)
 line('Pectoral brass seam',[(s*.032,-.165,1.331),(s*.126,-.152,1.296),(s*.177,-.105,1.36)],.004,GOLD,'spine_05')
 plate('Chest jade shoulder socket',[(s*.095,-.085,1.464),(s*.178,-.064,1.465),(s*.20,-.062,1.413),(s*.155,-.095,1.412)],.035,TEAL,'spine_05',.006)
 for j in range(3):
  z=1.24-j*.065
  plate('Articulated abdominal lamella',[(s*.009,-.15,z+.028),(s*.104,-.12,z+.02),(s*.098,-.117,z-.025),(s*.008,-.145,z-.035)],.02,TEAL,['spine_04','spine_03','spine_02'][j],.005)
  line('Abdominal titanium edge',[(s*.015,-.156,z-.023),(s*.092,-.127,z-.014)],.0025,GOLD,['spine_04','spine_03','spine_02'][j])
# Central kite-shaped power seal, enclosed in a raised metal frame.
for scale,mat,y in [(1.15,GOLD,-.178),(1,RUBBER,-.19),(.69,LIGHT,-.198)]:
 plate('Aether heart',[(0,y,1.445+scale*.003),(scale*.028,y,1.404),(0,y,1.363-scale*.003),(-scale*.028,y,1.404)],.012,mat,'spine_05',.003)
# Back harness and spine plates are visible during turns.
for j,z in enumerate([1.40,1.33,1.24,1.17]):
 plate('Dorsal overlapping armor', [(-.10,.118,z+.035),(.10,.118,z+.035),(.076,.14,z-.026),(0,.157,z-.04),(-.076,.14,z-.026)],-.022,TEAL,['spine_05','spine_05','spine_04','spine_03'][j],.006)
 rod('Dorsal light',(0,.162,z-.017),(0,.162,z+.019),.006,LIGHT,['spine_05','spine_05','spine_04','spine_03'][j])
# High split collar and fine compression ribs.
for s in [-1,1]:
 plate('Split ceremonial collar',[(s*.052,-.061,1.50),(s*.104,-.017,1.491),(s*.095,.074,1.49),(s*.055,.068,1.55)],.016,IVORY,'spine_05',.004)
 line('Collar gold rim',[(s*.055,-.064,1.501),(s*.104,-.02,1.496),(s*.058,.069,1.554)],.003,GOLD,'spine_05')
# Hood encloses original head; a continuous sculpted faceplate covers the face completely.
ball('Close fitted graphite cowl',(0,.006,1.684),(.087,.106,.131),RUBBER,'head',48,24)
# Mask horizontal rings create a brow, cheek bones, angular chin and a pronounced nose ridge.
levels=[(1.566,.036,-.10),(1.589,.061,-.12),(1.633,.079,-.135),(1.674,.084,-.137),(1.708,.080,-.132),(1.753,.066,-.112),(1.779,.038,-.079)]
v=[]
for z,w,y in levels:
 for f in [-1,-.66,-.30,0,.30,.66,1]:
  nose=(1-abs(f))*.018 if 1.60<z<1.71 else (1-abs(f))*.008
  v.append((w*f,y+abs(f)**1.8*.043-nose,z))
f=[]
for j in range(len(levels)-1):
 for k in range(6):a=j*7+k;f.append((a,a+1,a+8,a+7))
o=mesh('Seven plane porcelain mask',v,f,IVORY,'head')
bpy.context.view_layer.objects.active=o
so=o.modifiers.new('Mask thickness','SOLIDIFY');so.thickness=.005;bpy.ops.object.modifier_apply(modifier=so.name)
be=o.modifiers.new('Polished faceplate edges','BEVEL');be.width=.002;be.segments=3;bpy.ops.object.modifier_apply(modifier=be.name)
for s in [-1,1]:
 # Swept black sockets and inset luminous slit: clear expression even at fight distance.
 pts=[(s*.010,-.125,1.696),(s*.071,-.078,1.71),(s*.064,-.089,1.682),(s*.016,-.127,1.678)]
 plate('Recessed oblique eye socket',pts,.002,RUBBER,'head',.002)
 line('Aether eye slit',[(s*.018,-.131,1.689),(s*.045,-.113,1.696),(s*.063,-.095,1.700)],.003,LIGHT,'head')
 line('Mask cheek inlay',[(s*.018,-.116,1.605),(s*.054,-.098,1.627),(s*.067,-.086,1.651)],.0026,GOLD,'head')
 plate('Cheek geometric jade inset',[(s*.020,-.119,1.625),(s*.055,-.102,1.646),(s*.049,-.107,1.654),(s*.023,-.123,1.64)],.002,TEAL,'head',.001)
 ball('Temple hinge',(s*.085,-.009,1.697),(.009,.025,.025),GOLD,'head',24,12)
# Single asymmetric swept crown fin — an original silhouette, not borrowed horns.
plate('Ceremonial crown blade',[(-.038,-.050,1.758),(-.066,-.016,1.82),(-.039,.013,1.871),(-.01,-.025,1.799),(.005,-.05,1.765)],.02,TEAL,'head',.003)
line('Crown blade gold edge',[(-.063,-.019,1.818),(-.038,.01,1.867),(-.009,-.03,1.8)],.003,GOLD,'head')
# Shoulder armor is weighted to each upper arm, with generous elbow clearance.
for s,side in [(1,'l'),(-1,'r')]:
 b='upperarm_'+side;center=bonepos(b)
 # Curved pauldron, larger on the left with a split accent ridge.
 size=1.13 if s==1 else .97
 ball('Pauldron dark gasket',center+Vector((s*.012,0,-.02)),(.121*size,.105,.113),RUBBER,b)
 outline=[(s*.145,-.068,1.478),(s*.224,-.072,1.511),(s*.321,-.045,1.438),(s*.306,-.07,1.365),(s*.222,-.097,1.383)]
 plate('Angular jade pauldron',outline,.15,TEAL,b,.009)
 line('Pauldron ivory crest',[(s*.155,-.079,1.48),(s*.225,-.084,1.513),(s*.308,-.058,1.45)],.013,IVORY,b)
 line('Pauldron machined gold perimeter',[(s*.155,-.092,1.455),(s*.219,-.11,1.399),(s*.291,-.081,1.382)],.0035,GOLD,b)
 for j in range(3):rod('Shoulder light vents',(s*(.232+j*.019),-.108,1.431-j*.011),(s*(.243+j*.019),-.108,1.421-j*.011),.003,LIGHT,b)
 # Bracer shell follows the bind forearm rather than a guessed upright cylinder.
 a=bonepos('lowerarm_'+side);end=bonepos('hand_'+side);axis=(end-a).normalized();cx=a.lerp(end,.61)
 br=ball('Ceramic forearm guard',cx,(.082,.074,.145),IVORY,'lowerarm_'+side,32,16)
 br.rotation_euler=axis.to_track_quat('Z','Y').to_euler()
 for frac in [.27,.75]:
  p=a.lerp(end,frac)
  cuff=ball('Jade bracer reinforcement',p,(.087,.081,.030),TEAL,'lowerarm_'+side,32,12);cuff.rotation_euler=axis.to_track_quat('Z','Y').to_euler()
 # External long gold rail and jewel detail stay clear of the wrist.
 p1=a.lerp(end,.30)+Vector((s*.073,-.025,0));p2=a.lerp(end,.77)+Vector((s*.073,-.025,0))
 rod('Bracer power rail',p1,p2,.008,GOLD,'lowerarm_'+side)
 rod('Bracer inset energy',p1.lerp(p2,.15)+Vector((s*.005,-.005,0)),p1.lerp(p2,.82)+Vector((s*.005,-.005,0)),.003,LIGHT,'lowerarm_'+side)
 # Knuckle guards bind to fingers individually; palms remain the original deformation mesh.
 for finger in ['index','middle','ring','pinky']:
  name=finger+'_01_'+side
  p=bonepos(name)+Vector((0,.01,.012))
  ball('Articulated knuckle '+name,p,(.015,.017,.010),GOLD,name,16,8)
 # Thigh bands, side tassets and separate knee/shin plates.
 x=s*.111
 plate('Thigh jade plate',[(x-s*.056,-.102,.894),(x+s*.065,-.097,.878),(x+s*.068,-.099,.691),(x,-.123,.645),(x-s*.060,-.104,.704)],.025,TEAL,'thigh_'+side,.008)
 line('Thigh ceremonial trim',[(x-s*.047,-.114,.866),(x-s*.051,-.118,.717),(x,-.135,.666)],.0035,GOLD,'thigh_'+side)
 for j in range(2):rod('Thigh lacing',(x-s*.05,-.126,.808-j*.035),(x+s*.045,-.124,.784-j*.035),.004,RUBBER,'thigh_'+side)
 x=s*.128
 plate('Floating knee diamond',[(x,-.127,.567),(x+s*.066,-.10,.528),(x+s*.045,-.13,.484),(x,-.149,.455),(x-s*.049,-.13,.488),(x-s*.058,-.103,.53)],.033,IVORY,'calf_'+side,.006)
 line('Knee inset',[(x-s*.029,-.145,.525),(x,-.157,.501),(x+s*.028,-.143,.525)],.003,GOLD,'calf_'+side)
 plate('Tapered greave',[(x-s*.055,-.096,.438),(x+s*.055,-.096,.438),(s*.183,-.064,.162),(s*.142,-.09,.107),(s*.097,-.065,.17)],.06,IVORY,'calf_'+side,.008)
 plate('Greave jade spine',[(x-s*.018,-.111,.414),(x+s*.018,-.111,.414),(s*.158,-.089,.175),(s*.14,-.10,.151),(s*.126,-.087,.18)],.012,TEAL,'calf_'+side,.003)
 rod('Greave energy inlay',(x,-.121,.384),(s*.14,-.114,.199),.004,LIGHT,'calf_'+side)
 # Segmented sabatons, never a rigid bridge across the toe bend.
 for j,(y,z,bl) in enumerate([(-.022,.088,'foot_'),(-.10,.055,'foot_'),(-.18,.035,'ball_')]):
  ball('Ceramic sabaton', (s*.148,y,z),(.060,.055,.026),TEAL if j==1 else IVORY,bl+side,32,12)
# Textile sash, fitted waist band and four individually weighted short coat tabs.
for j in range(4):
 z=1.016+j*.012
 bpy.ops.mesh.primitive_torus_add(major_segments=64,minor_segments=8,location=(0,-.01,z),major_radius=.128,minor_radius=.009)
 o=bpy.context.object;o.scale.y=.82;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);finish(o,'Layered wine sash',CLOTH,'pelvis')
plate('Belt buckle',[(-.038,-.13,1.06),(.038,-.13,1.06),(.045,-.14,1.017),(0,-.151,.995),(-.045,-.14,1.017)],.02,GOLD,'pelvis',.003)
plate('Buckle jade jewel',[(-.023,-.157,1.045),(.023,-.157,1.045),(0,-.17,1.01)],.007,TEAL,'pelvis',.002)
for s,side in [(1,'l'),(-1,'r')]:
 for back in [False,True]:
  y=.135 if back else -.14
  pts=[(s*.04,y,.99),(s*.178,y*.60,.982),(s*.193,y*.83,.81),(s*.146,y*1.15,.739),(s*.078,y*1.18,.792)]
  plate('Split brocade hip tab',pts,.006 if not back else -.006,CLOTH,'thigh_'+side,.002)
  line('Coat tab titanium piping',[(s*.052,y*1.05,.975),(s*.087,y*1.22,.803),(s*.146,y*1.2,.753),(s*.179,y*.9,.816)],.0025,GOLD,'thigh_'+side)
# Keep all raised front shells safely above the fitted base and shoulder gaskets.
for o in parts[1:]:
 name=o.name
 if any(w in name for w in ['Pectoral','Chest jade','Articulated abdominal','Abdominal','Aether heart']):o.location.y-=.040
 if any(w in name for w in ['Angular jade pauldron','Pauldron','Shoulder light']):o.location.y-=.035
 if any(w in name for w in ['Thigh jade','Thigh ceremonial','Thigh lacing']):o.location.y-=.026
 if 'porcelain mask' in name:o.location.y-=.023
 if any(w in name for w in ['eye socket','eye slit','Mask cheek','Cheek geometric']):o.location.y-=.065
 if 'hip tab' in name or 'Coat tab' in name:
  if sum((o.matrix_world@v.co).y for v in o.data.vertices)/len(o.data.vertices)<0:o.location.y-=.045
# Join the original weighted body and all rigidly weighted armor into ONE skeletal mesh.
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=body;bpy.ops.object.join()
body.name='SK_AstralWarden'
# Consolidate material indices after joining seven shared materials.
old=list(body.data.materials);mapping={i:mats.index(m) for i,m in enumerate(old)}
indices=[mapping[p.material_index] for p in body.data.polygons]
body.data.materials.clear()
for m in mats:body.data.materials.append(m)
for p,i in zip(body.data.polygons,indices):p.material_index=i
for mod in list(body.modifiers):body.modifiers.remove(mod)
a=body.modifiers.new('Original Manny rig','ARMATURE');a.object=rig
body.parent=rig;body.matrix_parent_inverse=rig.matrix_world.inverted()
bpy.context.view_layer.update()
# Per-corner UV for scale-stable procedural material detail; no world-space texture swimming.
uv=body.data.uv_layers.get('AW_Surface') or body.data.uv_layers.new(name='AW_Surface')
for p in body.data.polygons:
 n=p.normal;axis=max(range(3),key=lambda i:abs(n[i]));axes=[i for i in range(3) if i!=axis]
 for li in p.loop_indices:
  co=body.data.vertices[body.data.loops[li].vertex_index].co
  uv.data[li].uv=(co[axes[0]],co[axes[1]])
body.data.uv_layers.active=uv
# Move generated UVs to channel zero; original body UVs not used by the new materials.
while body.data.uv_layers[0].name!='AW_Surface':body.data.uv_layers.remove(body.data.uv_layers[0])
body.data.calc_loop_triangles()
summary={'triangles':len(body.data.loop_triangles),'vertices':len(body.data.vertices),'materials':[m.name for m in mats],'bone_count_including_armature_root':len(rig.data.bones)+1,'rig':rig.name,'design':'Astral Warden: ivory mask, jade enamel, champagne metal, graphite compression suit, aubergine short coat tabs','source_body':'Existing project SKM_Manny; original rest skeleton and weighted undersuit retained. All armor/mask/tab geometry original.'}
(ROOT/'manifest.json').write_text(json.dumps(summary,indent=2))
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);body.select_set(True);bpy.context.view_layer.objects.active=body
bpy.ops.export_scene.fbx(filepath=str(ROOT/'Exports/SK_AstralWarden.fbx'),use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,bake_anim=False,use_armature_deform_only=False,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,mesh_smooth_type='FACE',use_tspace=True)
# Studio review; no camera or studio geometry exported to Unreal.
bpy.context.scene.render.engine='CYCLES';bpy.context.scene.cycles.samples=32
world=bpy.data.worlds.new('AW Studio');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.035,.045,.06,1);world.node_tree.nodes['Background'].inputs[1].default_value=.45;bpy.context.scene.world=world
for name,loc,power,color,size in [('Key',(2,-3,4),500,(.77,.89,1),3),('Warm rim',(-2,1,2.5),650,(1,.63,.30),2),('Softbox',(-2,-2,2),220,(.45,1,.89),2)]:
 d=bpy.data.lights.new(name,'AREA');d.energy=power;d.color=color;d.shape='DISK';d.size=size;o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector((0,0,1))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(2.5,-5,2.05));cam=bpy.context.object;cam.rotation_euler=(Vector((0,0,.99))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=2.15;bpy.context.scene.camera=cam
bpy.context.scene.render.resolution_x=1200;bpy.context.scene.render.resolution_y=1400;bpy.context.scene.render.resolution_percentage=100
bpy.context.scene.view_settings.view_transform='AgX'
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'AstralWarden.blend'))
bpy.context.scene.render.filepath=str(ROOT/'studio.png');bpy.ops.render.render(write_still=True)
print('AW_COMPLETE',json.dumps(summary))
