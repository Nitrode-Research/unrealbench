"""Asset authoring only; run with Unreal's PythonScript commandlet, editor closed.
Creates native StaticMesh/Material/light assets and updates TestMap_PL in place.
No Blueprint graph logic, combat values or camera defaults are changed.
"""
import unreal as u, json, math
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve()
ART=ROOT/'SourceArt/JadeAscension'
DEST='/Game/NightSkyEngine/Stages/JadeAscension'
MAP='/Game/NightSkyEngine/Maps/TestMap/TestMap_PL'
assets=u.AssetToolsHelpers.get_asset_tools(); ml=u.MaterialEditingLibrary
es=u.get_editor_subsystem(u.EditorActorSubsystem)
sms=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
def prop(obj,name,value):obj.set_editor_property(name,value)
def custom_input(name):
    i=u.CustomInput();prop(i,'input_name',name);return i
def reduction(percent,screen):
    r=u.EditorScriptingMeshReductionSettings();prop(r,'percent_triangles',percent);prop(r,'screen_size',screen);return r
def material(name):
    path=DEST+'/Materials/'+name
    m=u.load_asset(path)
    if not m:m=assets.create_asset(name,DEST+'/Materials',u.Material,u.MaterialFactoryNew())
    ml.delete_all_material_expressions(m)
    return m
def node(m,cls,**kw):
    n=ml.create_material_expression(m,cls)
    for k,v in kw.items():prop(n,k,v)
    return n
def connect(a,out,b,slot):assert ml.connect_material_expressions(a,out,b,slot)
def output(a,out,m,slot):assert ml.connect_material_property(a,out,slot)
def scalar(m,value,slot):output(node(m,u.MaterialExpressionConstant,r=value),'',m,slot)
def done(m):ml.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m)

m=material('M_JA_StoneAndMetal')
vc=node(m,u.MaterialExpressionVertexColor)
pos=node(m,u.MaterialExpressionWorldPosition)
base=node(m,u.MaterialExpressionCustom,code='float n=sin(P.x*.09+sin(P.y*.073))*sin(P.z*.057+P.y*.061);return C*(.94+.06*n);',output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
prop(base,'inputs',[custom_input('C'),custom_input('P')])
connect(vc,'',base,'C');connect(pos,'',base,'P');output(base,'',m,u.MaterialProperty.MP_BASE_COLOR)
emit=node(m,u.MaterialExpressionMultiply);connect(vc,'',emit,'A');connect(vc,'A',emit,'B')
power=node(m,u.MaterialExpressionMultiply,const_b=3.5);connect(emit,'',power,'A');output(power,'',m,u.MaterialProperty.MP_EMISSIVE_COLOR)
scalar(m,.55,u.MaterialProperty.MP_ROUGHNESS);scalar(m,.25,u.MaterialProperty.MP_METALLIC)
prop(m,'two_sided',True);done(m);surface=m

m=material('M_JA_CombatStone')
pos=node(m,u.MaterialExpressionWorldPosition)
code='''float2 q=P.xy/180; float2 cell=floor(q);float2 f=frac(q);
float edge=min(min(f.x,1-f.x),min(f.y,1-f.y));
float grain=sin(P.x*.21+sin(P.y*.16))*sin(P.y*.18)*.013;
float variation=frac(sin(dot(cell,float2(12.9898,78.233)))*43758.5453);
float3 c=lerp(float3(.12,.18,.20),float3(.23,.30,.31),variation*.45)+grain;
c*=lerp(.35,1,smoothstep(.005,.019,edge));
float ring=abs(length(P.xy/float2(1,1)) - 295);
float ring2=abs(length(P.xy)-315);
float inlay=1-smoothstep(1.0,2.0,min(ring,ring2));
return lerp(c,float3(.45,.29,.10),inlay*.72);'''
base=node(m,u.MaterialExpressionCustom,code=code,output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
prop(base,'inputs',[custom_input('P')]);connect(pos,'',base,'P');output(base,'',m,u.MaterialProperty.MP_BASE_COLOR)
scalar(m,.66,u.MaterialProperty.MP_ROUGHNESS);scalar(m,.10,u.MaterialProperty.MP_METALLIC);done(m);floor_mat=m

task=u.AssetImportTask();prop(task,'filename',str(ART/'T_CelestialVista.png'));prop(task,'destination_path',DEST+'/Textures')
prop(task,'automated',True);prop(task,'replace_existing',True);prop(task,'save',True);assets.import_asset_tasks([task])
tex=u.load_asset(DEST+'/Textures/T_CelestialVista')
prop(tex,'lod_group',u.TextureGroup.TEXTUREGROUP_SKYBOX);prop(tex,'max_texture_size',4096);u.EditorAssetLibrary.save_loaded_asset(tex)
m=material('M_JA_CelestialVista');prop(m,'shading_model',u.MaterialShadingModel.MSM_UNLIT);prop(m,'two_sided',True)
sample=node(m,u.MaterialExpressionTextureSample,texture=tex)
strength=node(m,u.MaterialExpressionMultiply,const_b=.7);connect(sample,'RGB',strength,'A');output(strength,'',m,u.MaterialProperty.MP_EMISSIVE_COLOR);done(m);sky_mat=m

manifest=json.loads((ART/'Exports/manifest.json').read_text());meshes={}
for entry in ([] if globals().get('JA_SKIP_IMPORT',False) else manifest):
    name=entry['name'];task=u.AssetImportTask();opt=u.FbxImportUI()
    prop(opt,'import_mesh',True);prop(opt,'import_materials',False);prop(opt,'import_textures',False)
    prop(opt,'import_as_skeletal',False);prop(opt,'mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH)
    d=opt.static_mesh_import_data;prop(d,'combine_meshes',True);prop(d,'auto_generate_collision',False)
    prop(d,'vertex_color_import_option',u.VertexColorImportOption.REPLACE)
    prop(d,'normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    for k,v in dict(filename=str(ART/'Exports'/f'{name}.fbx'),destination_path=DEST+'/Meshes',destination_name=name,automated=True,replace_existing=True,save=True,options=opt).items():prop(task,k,v)
    assets.import_asset_tasks([task]);sm=u.load_asset(DEST+'/Meshes/'+name);assert sm
    sm.set_material(0,sky_mat if 'Cyclorama' in name else surface)
    sms.remove_collisions(sm)
    if entry['collision']=='box':sms.add_simple_collisions(sm,u.ScriptingCollisionShapeType.BOX)
    if 'Cyclorama' not in name:
        lods=u.EditorScriptingMeshReductionOptions()
        prop(lods,'auto_compute_lod_screen_size',False)
        prop(lods,'reduction_settings',[reduction(1,1),reduction(.5,.30),reduction(.22,.10)])
        sms.set_lods(sm,lods)
    u.EditorAssetLibrary.save_loaded_asset(sm);meshes[name]=sm

for entry in manifest:
    meshes[entry['name']]=u.load_asset(DEST+'/Meshes/'+entry['name'])

world=u.EditorLoadingAndSavingUtils.load_map(MAP)
assert world
for a in es.get_all_level_actors():
    if a.get_actor_label().startswith('JA_'):es.destroy_actor(a)
    elif a.get_actor_label() in ['DirectionalLight','SkyLight','SkyAtmosphere','ExponentialHeightFog','VolumetricCloud','SM_SkySphere']:
        es.destroy_actor(a)

def actor(mesh_name,label,xyz,scale=(1,1,1),yaw=0,collide=False):
    a=es.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*xyz),u.Rotator(pitch=0,yaw=yaw,roll=0))
    a.set_actor_label('JA_'+label);a.set_folder_path('Jade Ascension/Scenery')
    a.set_actor_scale3d(u.Vector(*scale));c=a.static_mesh_component
    c.set_static_mesh(meshes['SM_JA_'+mesh_name]);c.set_mobility(u.ComponentMobility.STATIC)
    c.set_collision_profile_name('BlockAll' if collide else 'NoCollision')
    c.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS if collide else u.CollisionEnabled.NO_COLLISION)
    prop(a,'tags',['JA_Scenery']);return a

# Keep the existing Floor actor identity and transform origin for all existing references.
floor=next(a for a in es.get_all_level_actors() if a.get_name()=='Floor')
floor.set_actor_scale3d(u.Vector(1,1,1));floor.set_actor_location(u.Vector(0,0,0),False,False)
floor.static_mesh_component.set_static_mesh(meshes['SM_JA_CombatTerrace'])
floor.static_mesh_component.set_material(0,floor_mat)
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.static_mesh_component.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
floor.set_folder_path('Jade Ascension/Combat');prop(floor,'tags',['JA_CombatFloor'])

actor('MoonGate','MoonGate',(0,-1650,-30),yaw=180)
actor('GardenTerrace','LowerGarden',(0,-1550,-210))
for side in [-1,1]:
    actor('GardenTerrace',f'SideGarden_{side}',(side*1560,-1850,-130),(.85,1,.9))
    actor('Pagoda',f'NearPagoda_{side}',(side*1830,-2450,-50),(1.0,1.0,1.0))
    actor('Pagoda',f'FarPagoda_{side}',(side*3550,-6000,800),(.65,.65,.65))
    actor('BlossomTree',f'Blossom_{side}',(side*1220,-920,5),(1.1,1.1,1.1),side*25)
    actor('BlossomTree',f'GardenBlossom_{side}',(side*2380,-1830,-60),(.95,.95,.95),side*72)
    actor('SpiritObelisk',f'SpiritObelisk_{side}',(side*780,-880,0),(.75,.75,.75))
    actor('LotusBrazier',f'LotusBrazier_{side}',(side*460,-760,0),(.8,.8,.8))
    actor('PrayerBanner',f'GateBanner_{side}',(side*620,-1530,50),(1,1,1),180+side*8)
for i in range(-5,6):
    actor('Balustrade',f'RearRail_{i}',(i*360,-625,0),(.95,1,1))
for i,x in enumerate([-1800,-1100,-380,380,1100,1800]):
    actor('Lantern',f'Lantern_{i}',(x,-670,0),(.75,.75,.75))
for i,(x,y,z,s) in enumerate([(-1800,-2450,-80,2),(1900,-2500,-70,2.2),(-3550,-6000,820,1.5),(3550,-6000,810,1.5),(-1100,-5300,1650,.7),(1400,-4800,1450,.9),(-5200,-9000,1900,2.4),(5800,-10000,2400,2.8)]):
    actor('FloatingCrag',f'FloatingCrag_{i}',(x,y,z),(s,s,s),i*37)
for i,(x,y,z,s) in enumerate([(-980,-3600,670,1.7),(1200,-4200,900,2.1),(0,-1670,355,1.2)]):
    actor('SpiritHalo',f'SpiritHalo_{i}',(x,y,z),(s,s,s))
sky=actor('CelestialCyclorama','CelestialVista',(0,0,-1000),scale=(1,1,.7),yaw=180)
sky.static_mesh_component.set_cast_shadow(False)

def light(cls,label,at):
    a=es.spawn_actor_from_class(cls,u.Vector(*at));a.set_actor_label('JA_'+label);a.set_folder_path('Jade Ascension/Lighting')
    a.root_component.set_mobility(u.ComponentMobility.MOVABLE);return a
sun=light(u.DirectionalLight,'MoonKey',(0,0,1800));sun.set_actor_rotation(u.Rotator(pitch=-36,yaw=-65,roll=0),False)
c=sun.get_component_by_class(u.DirectionalLightComponent);c.set_intensity(5);c.set_light_color(u.LinearColor(.65,.8,1));prop(c,'light_source_angle',4)
c.set_forward_shading_priority(1)  # Unique primary light for translucency, water and volumetric fog.
rim=light(u.DirectionalLight,'PeachRim',(0,-2000,1400));rim.set_actor_rotation(u.Rotator(pitch=-20,yaw=100,roll=0),False)
c=rim.get_component_by_class(u.DirectionalLightComponent);c.set_intensity(2.3);c.set_light_color(u.LinearColor(1,.47,.32));c.set_cast_shadows(False)
c.set_forward_shading_priority(0)
sky=light(u.SkyLight,'SoftAmbient',(0,0,1500));c=sky.get_component_by_class(u.SkyLightComponent)
c.set_intensity(1.25);prop(c,'lower_hemisphere_is_black',False);c.set_light_color(u.LinearColor(.56,.72,1))
for i,x in enumerate([-1100,-380,380,1100]):
    a=light(u.PointLight,f'LanternPool_{i}',(x,-610,175));c=a.get_component_by_class(u.PointLightComponent)
    prop(c,'intensity_units',u.LightUnits.LUMENS);c.set_intensity(500);c.set_light_color(u.LinearColor(1,.42,.12));c.set_attenuation_radius(650);c.set_cast_shadows(False)
for s in [-1,1]:
    a=light(u.PointLight,f'JadeGlow_{s}',(s*460,-690,160));c=a.get_component_by_class(u.PointLightComponent)
    prop(c,'intensity_units',u.LightUnits.LUMENS);c.set_intensity(700);c.set_light_color(u.LinearColor(.07,1,.62));c.set_attenuation_radius(850);c.set_cast_shadows(False)
fog=light(u.ExponentialHeightFog,'ValleyMist',(0,-1500,-450));c=fog.get_component_by_class(u.ExponentialHeightFogComponent)
prop(c,'fog_density',.008);prop(c,'fog_height_falloff',.3);prop(c,'fog_inscattering_luminance',u.LinearColor(.10,.16,.25))
prop(c,'start_distance',1800);prop(c,'fog_max_opacity',.45)
pp=es.spawn_actor_from_class(u.PostProcessVolume,u.Vector());pp.set_actor_label('JA_Atmosphere');pp.set_folder_path('Jade Ascension/Lighting')
prop(pp,'unbound',True);prop(pp,'priority',50)
settings=pp.get_editor_property('settings')
for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=u.AutoExposureMethod.AEM_MANUAL,
    override_auto_exposure_bias=True,auto_exposure_bias=0,
    override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
    override_bloom_intensity=True,bloom_intensity=.45,
    override_vignette_intensity=True,vignette_intensity=.16,
    override_motion_blur_amount=True,motion_blur_amount=0).items():prop(settings,k,v)
prop(pp,'settings',settings)

u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(0,2200,850),u.Rotator(pitch=-12,yaw=-90,roll=0))
assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
print('JADE_ARENA_AUTHORING_COMPLETE',len(manifest),'mesh assets',len(es.get_all_level_actors()),'actors')
