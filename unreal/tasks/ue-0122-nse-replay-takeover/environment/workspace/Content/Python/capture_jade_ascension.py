import unreal as u,time,json
from pathlib import Path
out=Path(u.Paths.project_saved_dir())/'Screenshots/JadeAscension';out.mkdir(parents=True,exist_ok=True)
es=u.get_editor_subsystem(u.EditorActorSubsystem);le=u.get_editor_subsystem(u.LevelEditorSubsystem);ue=u.get_editor_subsystem(u.UnrealEditorSubsystem)
u.AutomationLibrary.finish_loading_before_screenshot()
le.editor_set_game_view(True)
u.SystemLibrary.execute_console_command(ue.get_editor_world(),'t.IdleWhenNotForeground 0')
u.SystemLibrary.execute_console_command(ue.get_editor_world(),'Slate.bAllowThrottling 0')
cam=es.spawn_actor_from_class(u.CameraActor,u.Vector(0,600,125));cam.set_actor_label('Transient_Review_Camera')
cam.camera_component.set_field_of_view(54)
views=[('fight-center',(0,600,125),(2.5,-90,0)),('fight-left',(-1050,600,125),(2.5,-90,0)),('fight-right',(1050,600,125),(2.5,-90,0)),('jump-space',(0,850,420),(2.5,-90,0)),('arena-overview',(0,2700,850),(-10,-90,0))]
state={'i':0,'wait':time.monotonic()+5,'capture':False}
def tick(dt):
 try:
  now=time.monotonic()
  if now<state['wait']:return
  if state['i']>=len(views):
   print('JADE_RENDER_CAPTURE_COMPLETE')
   u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor();return
  name,loc,rot=views[state['i']]
  if not state['capture']:
   cam.set_actor_location(u.Vector(*loc),False,False);cam.set_actor_rotation(u.Rotator(pitch=rot[0],yaw=rot[1],roll=rot[2]),False)
   le.pilot_level_actor(cam);state['capture']=True;state['wait']=now+4;return
  u.AutomationLibrary.take_high_res_screenshot(1920,1080,str(out/(name+'.png')),camera=cam)
  state['i']+=1;state['capture']=False;state['wait']=now+5
 except Exception:
  import traceback;traceback.print_exc();u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor()
handle=u.register_slate_post_tick_callback(tick)
