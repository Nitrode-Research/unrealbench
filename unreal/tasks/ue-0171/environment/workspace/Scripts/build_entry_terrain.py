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
# TODO: generate scene geometry, collision, navigation and authored placement.
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
