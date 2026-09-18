"""Build the owned EntryGate presentation map; all layout decisions are source-controlled."""
import unreal

MAP="/Game/Maps/EntryGate"
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools=unreal.AssetToolsHelpers.get_asset_tools(); lib=unreal.MaterialEditingLibrary
if assets.does_asset_exist(MAP):
    levels.load_level(MAP)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith("Yard."): actors.destroy_actor(actor)
else:
    if not levels.new_level(MAP): raise RuntimeError("Cannot create EntryGate map")

def solid(name,color,roughness=0.8,emission=0):
    path="/Game/Environment/M_"+name
    m=unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset("M_"+name,"/Game/Environment",unreal.Material,unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model",unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    lib.delete_all_material_expressions(m)
    c=lib.create_material_expression(m,unreal.MaterialExpressionConstant3Vector); c.set_editor_property("constant",unreal.LinearColor(*color,1))
    lib.connect_material_property(c,"",unreal.MaterialProperty.MP_BASE_COLOR)
    r=lib.create_material_expression(m,unreal.MaterialExpressionConstant); r.set_editor_property("r",roughness)
    lib.connect_material_property(r,"",unreal.MaterialProperty.MP_ROUGHNESS)
    if emission:
        e=lib.create_material_expression(m,unreal.MaterialExpressionConstant3Vector); e.set_editor_property("constant",unreal.LinearColor(*(v*emission for v in color),1))
        lib.connect_material_property(e,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(m); assets.save_loaded_asset(m,only_if_is_dirty=False)
    return m

wallmat=solid("Warehouse",(0.12,0.17,0.20))
trim=solid("SteelTrim",(0.045,0.065,0.085),0.5)
yellow=solid("SafetyPaint",(0.72,0.46,0.12),0.7)
white=solid("LanePaint",(0.44,0.48,0.43))
warm=solid("LampGlow",(1.0,0.47,0.12),0.4,6)
cool=solid("WindowGlow",(0.16,0.55,0.72),0.4,2)
crate=solid("CrateWood",(0.24,0.16,0.095))

def mesh(label,asset,position,scale=(1,1,1),yaw=0,material=None,collision=True):
    actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*position),unreal.Rotator(0,yaw,0))
    actor.set_actor_label("Yard."+label)
    actor.static_mesh_component.set_static_mesh(unreal.load_asset(asset))
    actor.set_actor_scale3d(unreal.Vector(*scale))
    if material: actor.static_mesh_component.set_material(0,material)
    if not collision: actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return actor

def box(label,position,size,material,yaw=0,collision=True):
    return mesh(label,"/Engine/BasicShapes/Cube",position,tuple(v/100 for v in size),yaw,material,collision)

asphalt=unreal.load_asset("/Game/Environment/M_YardAsphalt")
box("Ground",(0,0,-15),(3400,3000,30),asphalt)
# Low foreground boundaries preserve the camera view; taller structures sit behind the players.
box("RearWarehouse",(1580,0,230),(200,3000,460),wallmat)
box("NorthWall",(0,1440,165),(3200,100,330),wallmat)
for y in [-900,-350,200,750,1250]:
    box("WallColumn"+str(y),(1470,y,240),(35,35,480),trim)
for z in [25,430]: box("WallTrim"+str(z),(1460,0,z),(30,3000,20),trim)
for y in [-720,180,1040]:
    mesh("LoadingDoor"+str(y),"/Game/Environment/SM_LoadingDoc",(1450,y,0),yaw=180)
    box("DoorLamp"+str(y),(1395,y,410),(12,140,14),warm,collision=False)
mesh("SecurityKioskAndGate","/Game/Environment/SM_Kiosk",(-1150,780,0))
pane=box("KioskWindow",(-1234,760,175),(4,80,65),cool,collision=False)
pane.tags=["World.Kiosk.Glass"]
for i,position in enumerate([(-1250,730,3),(-1240,756,3),(-1260,780,3)]):
    shard=box("GlassShard"+str(i),position,(12,20,1),cool,yaw=i*37,collision=False)
    shard.tags=["World.Kiosk.Shards"]
effect=actors.spawn_actor_from_class(unreal.LinkWorldEffect,unreal.Vector())
effect.set_actor_label("Yard.WindowEffect");effect.set_editor_property("input_fact",1389382)
effect.set_editor_property("false_tag","World.Kiosk.Glass");effect.set_editor_property("true_tag","World.Kiosk.Shards")
for x,y,yaw in [(-700,-630,90),(-1030,-620,90),(970,-1000,0),(990,-650,0)]:
    mesh("Barrier"+str(x)+str(y),"/Game/Environment/SM_HighwayBarrier",(x,y,0),yaw=yaw)
for i,(x,y) in enumerate([(-560,470),(-690,480),(-590,570),(1090,800),(1100,930)]):
    mesh("Pallet"+str(i),"/Game/Environment/SM_Pallet",(x,y,7),yaw=i*13)
    if i in [0,3]:
        box("Crate"+str(i),(x,y,65),(95,70,105),crate,yaw=i*13)
        box("CrateStrap"+str(i),(x,y,119),(102,10,5),trim,yaw=i*13,collision=False)
for i,(x,y) in enumerate([(-900,440),(-960,500),(1180,-250),(1180,-340)]):
    mesh("Drum"+str(i),"/Game/Environment/SM_Drum",(x,y,44))
for i,(x,y) in enumerate([(-480,-560),(-1130,-440),(870,-1030),(1050,400),(1080,560)]):
    mesh("Cone"+str(i),"/Game/Environment/SM_Cone",(x,y,0))
mesh("NoEntry","/Game/Environment/SM_NoEntry",(-920,600,0),yaw=90)
chair=mesh("Chair","/Game/Environment/SM_Chair",(-820,470,0),yaw=-25)
chair.tags=["Item.EntryGate.Kiosk"]
gate_chair=mesh("GateChair","/Game/Environment/SM_Chair",(200,620,0),yaw=90)
gate_chair.tags=["Item.EntryGate.Gate"]
stone=solid("Stone",(0.14,0.16,0.17))
rock=mesh("Rock","/Engine/BasicShapes/Sphere",(-180,160,18),scale=(0.55,0.45,0.35),material=stone,collision=False)
rock.tags=["Item.EntryGate.Rock"]
mesh("PoleLeft","/Game/Environment/SM_UtilityPole",(-1400,1100,-120),scale=(0.7,0.7,0.7))
mesh("PoleRight","/Game/Environment/SM_UtilityPole",(1300,-1250,-120),scale=(0.7,0.7,0.7))
for i,x in enumerate([-400,400]):
    box("Lane"+str(i),(x,-160,0.5),(8,1500,1),white,collision=False)
for i,y in enumerate([-900,-500,-100,300]):
    box("CrossStripe"+str(i),(790,y,0.7),(380,7,1),yellow,collision=False)
for i in range(8): box("Drain"+str(i),(1100,-820+i*17,1),(120,7,2),trim,collision=False)

def light(label,position,color,intensity,radius=900):
    actor=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(*position)); actor.set_actor_label("Yard."+label)
    component=actor.point_light_component
    component.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE)
    component.set_editor_property("intensity_units",unreal.LightUnits.LUMENS)
    component.set_editor_property("intensity",intensity); component.set_editor_property("attenuation_radius",radius)
    component.set_light_color(unreal.LinearColor(*color,1))
    component.set_editor_property("source_radius",35)
    return actor

light("KioskWarm",(-1040,580,290),(1,0.56,0.24),3500,900)
light("DockWarm",(1120,180,360),(1,0.67,0.36),4500,1300)
light("FrontWarm",(-650,-650,400),(1,0.6,0.28),3000,1100)
light("CoolWindow",(-1200,800,180),(0.15,0.48,0.85),1000,550)
sun=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,800),unreal.Rotator(-65,125,0)); sun.set_actor_label("Yard.Moon")
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity",2.5)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("forward_shading_priority",1)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_light_color(unreal.LinearColor(0.42,0.57,0.82,1))
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("light_source_angle",8)
fill=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,800),unreal.Rotator(-35,-50,0)); fill.set_actor_label("Yard.AmbientFill")
fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity",1.2)
fill.get_component_by_class(unreal.DirectionalLightComponent).set_light_color(unreal.LinearColor(0.35,0.47,0.66,1))
fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("cast_shadows",False)
fog=actors.spawn_actor_from_class(unreal.ExponentialHeightFog,unreal.Vector(0,0,-150)); fog.set_actor_label("Yard.Haze")
fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property("fog_density",0.012)
fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property("fog_inscattering_luminance",unreal.LinearColor(0.025,0.04,0.075,1))
post=actors.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector()); post.set_actor_label("Yard.ColorGrade")
post.set_editor_property("unbound",True)
settings=post.get_editor_property("settings")
for name,value in [("auto_exposure_min_brightness",0.5),("auto_exposure_max_brightness",0.5),("bloom_intensity",0.3),("vignette_intensity",0.25),("film_grain_intensity",0.0)]:
    settings.set_editor_property("override_"+name,True); settings.set_editor_property(name,value)
post.set_editor_property("settings",settings)
start=actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(0,0,110)); start.set_actor_label("Yard.PlayerStart")
for label,title,event,item,position,offsets,radius,visual in [
    ("Rock","Loose stone",1389412,1389410,(-180,160,0),[(-100,-40,0),(100,-40,0)],170,1389410),
    ("Kiosk","Security kiosk",1389388,1389396,(-1150,780,0),[(-130,-180,0),(280,-180,0)],360,1389396),
    ("Gate","Rolling gate",1389383,0,(200,780,0),[(-100,-150,0),(100,-150,0)],210,1389396)]:
    interaction=actors.spawn_actor_from_class(unreal.LinkInteractionActor,unreal.Vector(*position))
    interaction.set_actor_label("Yard.Interaction."+label)
    interaction.set_editor_property("persistent_id","EntryGate."+label)
    interaction.set_editor_property("display_name",title)
    interaction.set_editor_property("event_id",event)
    interaction.set_editor_property("initial_item",item)
    if label=="Gate":interaction.set_editor_property("destination_map","LoadingDocks")
    interaction.set_editor_property("arrival_radius",radius)
    interaction.set_editor_property("waypoint_offsets",[unreal.Vector(*p) for p in offsets])
    interaction.set_editor_property("visual_item_id",visual)
    interaction.set_editor_property("item_visual_tag","Item.EntryGate."+label)
volume=actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume,unreal.Vector(0,0,150)); volume.set_actor_label("Yard.Navigation")
_,extent=volume.get_actor_bounds(False); volume.set_actor_scale3d(unreal.Vector(1650/extent.x,1450/extent.y,450/extent.z))
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.RecastNavMesh):
        actor.set_editor_property("runtime_generation",unreal.RuntimeGenerationType.DYNAMIC)
        actor.set_editor_property("force_rebuild_on_load",True)
if not levels.save_current_level(): raise RuntimeError("Cannot save EntryGate")
unreal.log("FOUNDATIONS_YARD_BUILT")
