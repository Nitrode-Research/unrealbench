"""Build the second mechanics area from the owned yard kit and pinned source rule IDs."""
import unreal

MAP="/Game/Maps/LoadingDocks"
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not assets.does_asset_exist(MAP):
    if not assets.duplicate_asset("/Game/Maps/EntryGate",MAP):raise RuntimeError("Cannot create LoadingDocks")
levels.load_level(MAP)
remove=["Rock","Chair","GateChair","KioskWindow","WindowEffect"]
for actor in actors.get_all_level_actors():
    label=actor.get_actor_label()
    if label.startswith("Docks.") or label in ["Yard."+n for n in remove] or label.startswith(("Yard.GlassShard","Yard.Interaction.","Yard.LoadingDoor","Yard.DoorLamp")):
        actors.destroy_actor(actor)
    elif label=="Yard.SecurityKioskAndGate":
        actor.static_mesh_component.set_static_mesh(unreal.load_asset("/Game/Environment/SM_KioskBody"))

def mesh(name,path,position,scale=(1,1,1),yaw=0,material=None,collision=True,movable=False):
    actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*position),unreal.Rotator(0,yaw,0));actor.set_actor_label("Docks."+name)
    actor.static_mesh_component.set_static_mesh(unreal.load_asset(path));actor.set_actor_scale3d(unreal.Vector(*scale))
    if material:actor.static_mesh_component.set_material(0,unreal.load_asset("/Game/Environment/M_"+material))
    if not collision:actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    if movable:actor.static_mesh_component.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE)
    return actor

def box(name,position,size,material,yaw=0,collision=True):
    return mesh(name,"/Engine/BasicShapes/Cube",position,tuple(v/100 for v in size),yaw,material,collision)

def interaction(name,title,event,item,position,offsets,radius=230,visual=0):
    actor=actors.spawn_actor_from_class(unreal.LinkInteractionActor,unreal.Vector(*position));actor.set_actor_label("Docks.Interaction."+name)
    for key,value in [("persistent_id","LoadingDocks."+name),("display_name",title),("event_id",event),("initial_item",item),("arrival_radius",radius),
                      ("visual_item_id",visual or item),("item_visual_tag","Item.LoadingDocks."+name)]:actor.set_editor_property(key,value)
    actor.set_editor_property("waypoint_offsets",[unreal.Vector(*v) for v in offsets])
    if name in ["Bay2","EmployeeDoor"]:actor.set_editor_property("completes_run",True)
    return actor

door=mesh("KioskDoor","/Game/Environment/SM_KioskDoor",(-1215,840,10),movable=True);door.tags=["World.Docks.KioskDoor"]
effect=actors.spawn_actor_from_class(unreal.LinkWorldEffect,unreal.Vector());effect.set_actor_label("Docks.KioskHinge")
effect.set_editor_property("input_fact",1388906);effect.set_editor_property("moving_tag","World.Docks.KioskDoor");effect.set_editor_property("active_yaw",-95)
interaction("Kiosk","Kiosk controls",1388910,0,(-1260,780,0),[(-130,-150,0),(-130,150,0)],270)
interaction("Gate","Gate mechanism",1388800,1389509,(150,780,0),[(-100,-150,0),(100,-150,0)],220)
pickaxe=mesh("JammedPickaxe","/Game/Environment/SM_Pickaxe",(150,720,60),yaw=90,collision=False);pickaxe.tags=["Item.LoadingDocks.Gate"]
wedge=mesh("InsertedWedge","/Game/Environment/SM_Wedge",(175,720,12),scale=(0.65,0.65,0.65),collision=False);wedge.tags=["World.Docks.InsertedWedge"]
interaction("Pole","Floodlight pole",1388999,0,(1220,-1150,0),[(-100,100,0),(100,100,0)])
for index,y in enumerate([-950,-300,350,1000],1):
    mesh("BayDoor"+str(index),"/Game/Environment/SM_LoadingDoc",(1450,y,0),yaw=180)
    box("BayHeader"+str(index),(1390,y,410),(10,230,12),"SafetyPaint",collision=False)
    sign=actors.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(1390,y-45,360),unreal.Rotator(0,0,0));sign.set_actor_label("Docks.BayNumber"+str(index))
    text=sign.get_component_by_class(unreal.TextRenderComponent);text.set_text(str(index).zfill(2));text.set_world_size(60);text.set_text_render_color(unreal.Color(214,191,140))
    interaction("Bay"+str(index),"Loading bay "+str(index).zfill(2),[1389005,1389009,1389013,1389036][index-1],1389508 if index==1 else 0,
        (1360,y,0),[(-170,-110,0),(-170,110,0)],270)
loose=mesh("LooseWedge","/Game/Environment/SM_Wedge",(1050,-1030,0),scale=(0.7,0.7,0.7),collision=False);loose.tags=["Item.LoadingDocks.Bay1"]
box("TrailerBody",(700,-940,100),(180,340,180),"Warehouse")
box("TrailerRoof",(700,-940,195),(190,350,12),"SteelTrim")
for index,y in enumerate([-1070,-830]):mesh("TrailerWheel"+str(index),"/Engine/BasicShapes/Cylinder",(610,y,35),scale=(0.65,0.65,0.2),material="SteelTrim",collision=False)
side=box("TrailerSide",(605,-940,110),(8,270,120),"CrateWood",collision=False);side.tags=["World.Docks.TrailerIntact"]
for index in range(3):
    debris=box("TrailerFragment"+str(index),(540-index*24,-960+index*36,10),(35,100,8),"CrateWood",yaw=index*35,collision=False);debris.tags=["World.Docks.TrailerBroken"]
broken=actors.spawn_actor_from_class(unreal.LinkWorldEffect,unreal.Vector());broken.set_actor_label("Docks.TrailerState")
broken.set_editor_property("input_fact",1389671);broken.set_editor_property("false_tag","World.Docks.TrailerIntact");broken.set_editor_property("true_tag","World.Docks.TrailerBroken")
interaction("EmployeeDoor","Employee entrance",1389040,1389510,(1000,1320,0),[(-110,-160,0),(110,-160,0)],240)
box("EmployeeDoor",(1000,1383,110),(120,8,220),"SteelTrim")
knob=mesh("Doorknob","/Game/Environment/SM_Doorknob",(1040,1370,110),collision=False);knob.tags=["Item.LoadingDocks.EmployeeDoor"]

for index,y in enumerate([-600,0]):
    light=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(1130,y,560));light.set_actor_label("Docks.PuzzleLight"+str(index));light.tags=["World.Docks.Floodlights"]
    component=light.point_light_component;component.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE);component.set_editor_property("intensity_units",unreal.LightUnits.LUMENS)
    component.set_editor_property("intensity",0);component.set_editor_property("attenuation_radius",1300);component.set_light_color(unreal.LinearColor(0.75,0.88,1,1));component.set_editor_property("source_radius",40)
    lamp=mesh("Floodlight"+str(index),"/Game/Environment/SM_Floodlight",(1250,y,430),yaw=180,collision=False)
power=actors.spawn_actor_from_class(unreal.LinkWorldEffect,unreal.Vector());power.set_actor_label("Docks.FloodlightPower")
for key,value in [("input_fact",1389508),("interaction_id","LoadingDocks.Gate"),("output_fact",1389150),("light_tag","World.Docks.Floodlights"),("light_intensity",6000),("true_tag","World.Docks.InsertedWedge")]:power.set_editor_property(key,value)
if not levels.save_current_level():raise RuntimeError("Cannot save LoadingDocks")
unreal.log("FOUNDATIONS_LOADING_DOCKS_BUILT")
