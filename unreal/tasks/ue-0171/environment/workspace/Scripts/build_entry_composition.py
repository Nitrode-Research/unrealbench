"""Add source E02 composition to the E01 map without changing its navigation."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
DATA=json.loads((ROOT/'Docs/Fixtures/EntryCompositionDefinition.json').read_text())
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
LIB=unreal.MaterialEditingLibrary
FOLDER='/Game/Task0170/EntryComposition'
assert DATA['targetMap']=='/Game/Task0170/EntryGate_Source'
assert LEVELS.load_level(DATA['targetMap'])
for actor in ACTORS.get_all_level_actors():
    if actor.get_actor_label().startswith('SourceComposition.'):ACTORS.destroy_actor(actor)

def scalar(material,name,value):
    node=LIB.create_material_expression(material,unreal.MaterialExpressionScalarParameter)
    node.set_editor_property('parameter_name',name);node.set_editor_property('default_value',value);return node
def vector(material,name,value):
    node=LIB.create_material_expression(material,unreal.MaterialExpressionVectorParameter)
    node.set_editor_property('parameter_name',name);node.set_editor_property('default_value',unreal.LinearColor(*value[:3],1));return node
def custom(material,code,inputs,output):
    node=LIB.create_material_expression(material,unreal.MaterialExpressionCustom);node.set_editor_property('code',code)
    node.set_editor_property('output_type',output);pins=[]
    for name in inputs:
        p=unreal.CustomInput();p.set_editor_property('input_name',name);pins.append(p)
    node.set_editor_property('inputs',pins)
    for name,(source,pin) in inputs.items():assert LIB.connect_material_expressions(source,pin,node,name)
    return node

texture_path=FOLDER+'/T_SourcePaintDiffuse'
if not ASSETS.does_asset_exist(texture_path):
    task=unreal.AssetImportTask();task.filename=str(ROOT/'SourceArt/EntryComposition/PaintDiffuse.png')
    task.destination_path=FOLDER;task.destination_name='T_SourcePaintDiffuse';task.automated=True;task.save=True
    TOOLS.import_asset_tasks([task]);assert task.imported_object_paths
paint=unreal.load_asset(texture_path)
paint.set_editor_property('srgb',True);paint.set_editor_property('filter',unreal.TextureFilter.TF_BILINEAR)
paint.set_editor_property('address_x',unreal.TextureAddress.TA_WRAP);paint.set_editor_property('address_y',unreal.TextureAddress.TA_WRAP)
ASSETS.save_loaded_asset(paint,only_if_is_dirty=False)

leaf_path=FOLDER+'/M_SourceLeaves'
leaf=unreal.load_asset(leaf_path) if ASSETS.does_asset_exist(leaf_path) else TOOLS.create_asset('M_SourceLeaves',FOLDER,unreal.Material,unreal.MaterialFactoryNew())
LIB.delete_all_material_expressions(leaf)
leaf.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
leaf.set_editor_property('opacity_mask_clip_value',.5);leaf.set_editor_property('two_sided',False)
code=(ROOT/'Shaders/SourceLeaves.ush').read_text()
before=LIB.create_material_expression(leaf,unreal.MaterialExpressionWorldPosition)
before.set_editor_property('world_position_shader_offset',unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
after=LIB.create_material_expression(leaf,unreal.MaterialExpressionWorldPosition)
uv=LIB.create_material_expression(leaf,unreal.MaterialExpressionTextureCoordinate)
clock=LIB.create_material_expression(leaf,unreal.MaterialExpressionTime)
axis_x=vector(leaf,'SourceAxisX',[100,0,0]);axis_y=vector(leaf,'SourceAxisY',[0,100,0])
wind=custom(leaf,code+'\nFSourceLeaves L; return L.Wind(World,Clock,AxisX,AxisY);',
    {'World':(before,''),'Clock':(clock,''),'AxisX':(axis_x,''),'AxisY':(axis_y,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT3)
assert LIB.connect_material_property(wind,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
texture=LIB.create_material_expression(leaf,unreal.MaterialExpressionTextureObject);texture.texture=paint
props=DATA['leafProperties']
surface=custom(leaf,code+'\nFSourceLeaves L; return L.Surface(Paint,PaintSampler,UV,Before,After,Light.rgb,Dark.rgb,Scales.rgb,Strengths.rgb,Line.xy);',
    {'Paint':(texture,''),'UV':(uv,''),'Before':(before,''),'After':(after,''),
     'Light':(vector(leaf,'SourceLight',props['_ColorLight']),''),'Dark':(vector(leaf,'SourceDark',props['_ColorDark']),''),
     'Scales':(vector(leaf,'SourceScales',props['_Scales']),''),'Strengths':(vector(leaf,'SourceStrengths',props['_Strengths']),''),
     'Line':(vector(leaf,'SourceLine',props['_Line']),'')},unreal.CustomMaterialOutputType.CMOT_FLOAT4)
rgb=LIB.create_material_expression(leaf,unreal.MaterialExpressionComponentMask)
rgb.set_editor_property('r',True);rgb.set_editor_property('g',True);rgb.set_editor_property('b',True);rgb.set_editor_property('a',False)
assert LIB.connect_material_expressions(surface,'',rgb,'')
assert LIB.connect_material_property(rgb,'',unreal.MaterialProperty.MP_BASE_COLOR)
alpha=LIB.create_material_expression(leaf,unreal.MaterialExpressionComponentMask)
for channel in ['r','g','b']:alpha.set_editor_property(channel,False)
alpha.set_editor_property('a',True);assert LIB.connect_material_expressions(surface,'',alpha,'')
assert LIB.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY_MASK)
rough=scalar(leaf,'Roughness',.5);spec=scalar(leaf,'Specular',0)
LIB.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
LIB.connect_material_property(spec,'',unreal.MaterialProperty.MP_SPECULAR)
LIB.recompile_material(leaf);assert ASSETS.save_loaded_asset(leaf,only_if_is_dirty=False)

palette_texture_path=FOLDER+'/T_SourcePaletteExact'
if not ASSETS.does_asset_exist(palette_texture_path):
    task=unreal.AssetImportTask();task.filename=str(ROOT/'SourceArt/Environment/Palette.png')
    task.destination_path=FOLDER;task.destination_name='T_SourcePaletteExact';task.automated=True;task.save=True
    TOOLS.import_asset_tasks([task]);assert task.imported_object_paths
exact_palette=unreal.load_asset(palette_texture_path)
# A 32x32 colour/data palette must not be block-compressed. Keep raw RGBA8
# values and apply the source sRGB transfer explicitly in the sampling shader.
exact_palette.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
exact_palette.set_editor_property('srgb',False)
exact_palette.set_editor_property('filter',unreal.TextureFilter.TF_NEAREST)
exact_palette.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
assert ASSETS.save_loaded_asset(exact_palette,only_if_is_dirty=False)
palette_path=FOLDER+'/M_SourcePaletteExact'
palette=unreal.load_asset(palette_path) if ASSETS.does_asset_exist(palette_path) else TOOLS.create_asset('M_SourcePaletteExact',FOLDER,unreal.Material,unreal.MaterialFactoryNew())
LIB.delete_all_material_expressions(palette)
tex=LIB.create_material_expression(palette,unreal.MaterialExpressionTextureObject);tex.texture=exact_palette
coord=LIB.create_material_expression(palette,unreal.MaterialExpressionTextureCoordinate)
sample=custom(palette,(ROOT/'Shaders/SourcePaletteColor.ush').read_text(),
    {'Palette':(tex,''),'UV':(coord,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT3)
LIB.connect_material_property(sample,'',unreal.MaterialProperty.MP_BASE_COLOR)
LIB.connect_material_property(scalar(palette,'Roughness',.85),'',unreal.MaterialProperty.MP_ROUGHNESS)
LIB.connect_material_property(scalar(palette,'Specular',.15),'',unreal.MaterialProperty.MP_SPECULAR)
LIB.recompile_material(palette);assert ASSETS.save_loaded_asset(palette,only_if_is_dirty=False)
# Override only this source map's palette bindings. E01/Legacy mesh assets and
# the existing prototype palette remain unchanged.
for actor in ACTORS.get_all_level_actors():
    if isinstance(actor,unreal.StaticMeshActor) and actor.get_actor_label().startswith('SourceEntry.Geometry.'):
        component=actor.static_mesh_component
        for slot in range(component.get_num_materials()):
            existing=component.get_material(slot)
            if existing and existing.get_path_name().startswith('/Game/Task0170/EntryGate/M_SourceGeometryPalette'):
                component.set_material(slot,palette)
report={'map':DATA['targetMap'],'meshes':{},'placements':[]}
# TODO: generate scene composition and bind supplied materials.
# Keep the E01 inspection lighting temporary, but illuminate camera-facing
# landmarks so their authored colour cues remain readable pending E05.
for actor in ACTORS.get_all_level_actors():
    if actor.get_actor_label()=='SourceEntry.FoundationMoon':
        actor.set_actor_rotation(unreal.Rotator(pitch=-50,yaw=125,roll=0),False)
    if actor.get_actor_label()=='SourceEntry.FoundationFill':
        actor.set_actor_rotation(unreal.Rotator(pitch=-35,yaw=50,roll=0),False)
assert LEVELS.save_current_level()
out=ROOT/'Saved/EntryComposition';out.mkdir(parents=True,exist_ok=True)
(out/'ImportReport.json').write_text(json.dumps(report,indent=2)+'\n')
unreal.log('ENTRY_COMPOSITION_BUILT')
