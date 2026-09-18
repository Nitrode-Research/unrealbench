"""Transient Unreal pose review. Never saves the map or adds gameplay logic."""
import unreal as u,time
from pathlib import Path
out=Path(u.Paths.project_saved_dir())/'Screenshots/AstralWarden';out.mkdir(parents=True,exist_ok=True)
es=u.get_editor_subsystem(u.EditorActorSubsystem);le=u.get_editor_subsystem(u.LevelEditorSubsystem);ue=u.get_editor_subsystem(u.UnrealEditorSubsystem)
le.editor_set_game_view(True)
world=ue.get_editor_world()
for cmd in ['t.IdleWhenNotForeground 0','Slate.bAllowThrottling 0']:u.SystemLibrary.execute_console_command(world,cmd)
actor=es.spawn_actor_from_class(u.SkeletalMeshActor,u.Vector(0,0,0));actor.set_actor_label('Transient_AstralWarden_Review')
c=actor.skeletal_mesh_component;c.set_skeletal_mesh_asset(u.load_asset('/Game/NightSkyEngine/CharacterAssets/AstralWarden/Meshes/SK_AstralWarden'))
c.set_update_animation_in_editor(True);c.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
cam=es.spawn_actor_from_class(u.CameraActor,u.Vector(170,370,160));cam.camera_component.set_field_of_view(48)
# Temporary soft portrait lights, shared across poses.
for loc,color,intensity in [((150,180,250),(0.75,0.87,1),350),((-150,50,210),(0.35,1,.8),180),((70,-100,200),(1,.65,.3),240)]:
 a=es.spawn_actor_from_class(u.PointLight,u.Vector(*loc));l=a.point_light_component;l.set_editor_property('intensity_units',u.LightUnits.LUMENS);l.set_intensity(intensity);l.set_light_color(u.LinearColor(*color));l.set_editor_property('source_radius',70);l.set_editor_property('attenuation_radius',650);l.set_cast_shadows(False)
folder='/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/'
views=[('portrait',None,0,(230,540,170),(-8,-113,0)),('mask-detail',None,0,(65,180,174),(-2,-110,0)),('idle','AS_manny_stand',.3,(230,540,170),(-8,-113,0)),('crouch','AS_manny_crouch',.3,(230,540,150),(-8,-113,0)),('attack-a','AS_manny_5a',.14,(230,540,170),(-8,-113,0)),('attack-b','AS_manny_5b',.20,(230,540,170),(-8,-113,0)),('back',None,0,(-230,-540,170),(-8,67,0))]
state={'i':0,'wait':time.monotonic()+8,'capture':False}
def tick(dt):
 try:
  now=time.monotonic()
  if now<state['wait']:return
  if state['i']>=len(views):
   print('AW_CAPTURE_COMPLETE');u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor();return
  name,anim,position,loc,rot=views[state['i']]
  if not state['capture']:
   asset=u.load_asset(folder+anim) if anim else None
   if anim and not asset:print('AW_CAPTURE_MISSING_ANIMATION',anim)
   c.set_animation(asset);c.set_position(position,False)
   if asset:c.play_animation(asset,False);c.set_position(position,False);c.set_play_rate(0)
   cam.set_actor_location(u.Vector(*loc),False,False);cam.set_actor_rotation(u.Rotator(pitch=rot[0],yaw=rot[1],roll=rot[2]),False)
   le.pilot_level_actor(cam);state['capture']=True;state['wait']=now+4;return
  state['wait']=now+60
  u.AutomationLibrary.take_high_res_screenshot(1600,1600,str(out/(name+'.png')),camera=cam)
  print('AW_POSE_CAPTURED',name)
  state['i']+=1;state['capture']=False;state['wait']=now+4
 except Exception:
  import traceback;traceback.print_exc();u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor()
handle=u.register_slate_post_tick_callback(tick)
