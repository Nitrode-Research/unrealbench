"""Build the E01 source terrain map using native static meshes and source transforms.

Run in a compiled disposable project first. Existing and legacy maps are never
loaded or saved by this script. Core gameplay remains in C++.
"""
import hashlib
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
DATA=json.loads((ROOT/'Docs/Fixtures/EntryTerrainDefinition.json').read_text())
SOURCE=ROOT/'SourceData/UnityScenes'
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
LIB=unreal.MaterialEditingLibrary
MAP=DATA['targetMap']
assert MAP=='/Game/Task0170/EntryGate_Source'
if ASSETS.does_asset_exist(MAP):
    assert LEVELS.load_level(MAP)
    for actor in ACTORS.get_all_level_actors():
        if actor.get_actor_label().startswith('SourceEntry.'): ACTORS.destroy_actor(actor)
else:
    assert LEVELS.new_level(MAP)


def constant(material,value,prop):
    node=LIB.create_material_expression(material,unreal.MaterialExpressionConstant)
    node.set_editor_property('r',value); LIB.connect_material_property(node,'',prop)


def material(name):
    path='/Game/Task0170/EntryGate/'+name
    value=unreal.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset(name,'/Game/Task0170/EntryGate',unreal.Material,unreal.MaterialFactoryNew())
    LIB.delete_all_material_expressions(value)
    constant(value,.85,unreal.MaterialProperty.MP_ROUGHNESS)
    constant(value,.15,unreal.MaterialProperty.MP_SPECULAR)
    return value


palette=material('M_SourceGeometryPalette')
uv=LIB.create_material_expression(palette,unreal.MaterialExpressionTextureCoordinate)
flip=LIB.create_material_expression(palette,unreal.MaterialExpressionCustom)
flip.set_editor_property('code','return float2(UV.x, 1.0 - UV.y);')
flip.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT2)
pin=unreal.CustomInput(); pin.set_editor_property('input_name','UV'); flip.set_editor_property('inputs',[pin])
assert LIB.connect_material_expressions(uv,'',flip,'UV')
texture=LIB.create_material_expression(palette,unreal.MaterialExpressionTextureSample)
texture.texture=unreal.load_asset('/Game/Environment/T_SourcePalette')
assert LIB.connect_material_expressions(flip,'',texture,'UVs')
assert LIB.connect_material_property(texture,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)

ground=material('M_SourceTerrainFoundation')
color=LIB.create_material_expression(ground,unreal.MaterialExpressionVertexColor)
custom=LIB.create_material_expression(ground,unreal.MaterialExpressionCustom)
custom.set_editor_property('code',(ROOT/'Shaders/SourceTerrainFoundation.ush').read_text())
custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
pin=unreal.CustomInput(); pin.set_editor_property('input_name','Paint'); custom.set_editor_property('inputs',[pin])
assert LIB.connect_material_expressions(color,'',custom,'Paint')
assert LIB.connect_material_property(custom,'',unreal.MaterialProperty.MP_BASE_COLOR)
for value in (palette,ground):
    LIB.recompile_material(value); assert ASSETS.save_loaded_asset(value,only_if_is_dirty=False)

report={'map':MAP,'definitionSha256':hashlib.sha256((ROOT/'Docs/Fixtures/EntryTerrainDefinition.json').read_bytes()).hexdigest(),
        'meshes':{},'placements':[],'boxes':len(DATA['boxes']),'exclusions':len(DATA['exclusions'])}
meshes={}
for number,entry in enumerate(DATA['geometry']):
    target=entry['asset']
    if target not in meshes:
        source=SOURCE/DATA['sourceMeshes'][entry['mesh']]['file']
        mesh=unreal.LinkAssetTools.build_source_mesh(str(source),target,entry['matrix'],entry['collision'])
        if not mesh: raise RuntimeError('Native mesh build failed: '+target)
        slots=mesh.get_editor_property('static_materials')
        for slot_index,slot in enumerate(slots):
            slot.set_editor_property('material_interface',ground if entry['name']=='Terrain' else palette)
            slots[slot_index]=slot
        mesh.set_editor_property('static_materials',slots)
        assert ASSETS.save_loaded_asset(mesh,only_if_is_dirty=False)
        meshes[target]=mesh
        report['meshes'][target]=json.loads(unreal.LinkAssetTools.inspect_source_mesh(mesh))
        report['meshes'][target]['materials']=[s.get_editor_property('material_interface').get_path_name() for s in mesh.get_editor_property('static_materials')]
        assert all('/Game/Task0170/EntryGate/M_' in path for path in report['meshes'][target]['materials']),target
    actor=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*entry['pivot']))
    actor.set_actor_label('SourceEntry.Geometry.'+str(number)+'.'+entry['name'])
    actor.tags=['SourceId.'+entry['sourceId'],'SourceGround' if entry['ground'] else 'SourceObstacle']
    component=actor.static_mesh_component
    component.set_static_mesh(meshes[target]); component.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name('BlockAll')
    component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS if entry['collision'] else unreal.CollisionEnabled.NO_COLLISION)
    component.set_visibility(entry['visible']); actor.set_actor_hidden_in_game(not entry['visible'])
    unreal.LinkAssetTools.set_source_navigation(actor,entry['navRelevant'],entry['navExcluded'])
    report['placements'].append({'sourceId':entry['sourceId'],'asset':target,'location':entry['pivot']})

for number,entry in enumerate(DATA['boxes']):
    actor=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector())
    actor.set_actor_label('SourceEntry.Collision.'+str(number)+'.'+entry['name'])
    actor.tags=['SourceId.'+entry['sourceId']]
    actor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    assert unreal.LinkAssetTools.apply_source_box_transform(actor,entry['matrix'])
    actor.set_actor_hidden_in_game(True); actor.static_mesh_component.set_visibility(False)
    actor.static_mesh_component.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    unreal.LinkAssetTools.set_source_navigation(actor,entry['navRelevant'],entry['navExcluded'])

for number,entry in enumerate(DATA['exclusions']):
    volume=ACTORS.spawn_actor_from_class(unreal.NavModifierVolume,unreal.Vector())
    volume.set_actor_label('SourceEntry.Exclusion.'+str(number)+'.'+entry['name'])
    volume.tags=['SourceId.'+entry['sourceId']]
    volume.set_editor_property('area_class',unreal.NavArea_Null)
    assert unreal.LinkAssetTools.apply_source_box_transform(volume,entry['matrix'])

layout=ACTORS.spawn_actor_from_class(unreal.LinkSceneLayout,unreal.Vector())
layout.set_actor_label('SourceEntry.Layout'); layout.set_editor_property('source_scene','EntryGate')
for player in DATA['players']:
    start=ACTORS.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(*player['position']),unreal.Rotator(0,player['yaw'],0))
    start.set_actor_label('SourceEntry.Start.'+('LT' if player['identity']==0 else 'RT'))
    start.set_editor_property('player_start_tag','LT' if player['identity']==0 else 'RT')

# The original ground extends into the neighbouring area. Its source exclusion
# volumes, fences and gate determine access; no substitute flat floor is added.
nav=ACTORS.spawn_actor_from_class(unreal.NavMeshBoundsVolume,unreal.Vector(-2600,-600,200))
nav.set_actor_label('SourceEntry.Navigation')
_,extent=nav.get_actor_bounds(False)
nav.set_actor_scale3d(unreal.Vector(4200/extent.x,3600/extent.y,1400/extent.z))
for actor in ACTORS.get_all_level_actors():
    if isinstance(actor,unreal.RecastNavMesh):
        actor.set_editor_property('runtime_generation',unreal.RuntimeGenerationType.DYNAMIC)
        actor.set_editor_property('force_rebuild_on_load',True)

# Temporary readable illumination; source lighting/camera matching remain E05/C01.
sun=ACTORS.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1000),unreal.Rotator(-50,125,0))
sun.set_actor_label('SourceEntry.FoundationMoon')
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('intensity',3.0)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('forward_shading_priority',1)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_light_color(unreal.LinearColor(.58,.70,1,1))
fill=ACTORS.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1000),unreal.Rotator(-35,-50,0))
fill.set_actor_label('SourceEntry.FoundationFill')
fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('intensity',1.0)
fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('cast_shadows',False)
post=ACTORS.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector())
post.set_actor_label('SourceEntry.FoundationExposure'); post.set_editor_property('unbound',True)
settings=post.get_editor_property('settings')
for key,value in [('auto_exposure_min_brightness',.5),('auto_exposure_max_brightness',.5),('bloom_intensity',.15)]:
    settings.set_editor_property('override_'+key,True); settings.set_editor_property(key,value)
post.set_editor_property('settings',settings)
assert LEVELS.save_current_level()
out=ROOT/'Saved/EntryTerrain'; out.mkdir(parents=True,exist_ok=True)
(out/'ImportReport.json').write_text(json.dumps(report,indent=2)+'\n')
unreal.log('SOURCE_ENTRY_TERRAIN_BUILT')
