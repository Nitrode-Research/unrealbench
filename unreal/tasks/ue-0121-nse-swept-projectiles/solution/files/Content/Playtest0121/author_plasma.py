"""Rebuild the task 121 plasma materials and casting animation in Unreal Editor.
Uses only existing project animation and engine material nodes; no external assets.
"""
import unreal as u
DEST='/Game/Playtest0121'
ml=u.MaterialEditingLibrary
at=u.AssetToolsHelpers.get_asset_tools()
def prop(o,k,v):o.set_editor_property(k,v)
def node(m,cls,**values):
 n=ml.create_material_expression(m,cls)
 for k,v in values.items():prop(n,k,v)
 return n
def wire(a,out,b,slot):assert ml.connect_material_expressions(a,out,b,slot)
def output(a,m,p):assert ml.connect_material_property(a,'',p)
def build(name,halo=False):
 m=u.load_asset(DEST+'/'+name)
 if m:return
 m=at.create_asset(name,DEST,u.Material,u.MaterialFactoryNew())
 prop(m,'shading_model',u.MaterialShadingModel.MSM_UNLIT)
 prop(m,'blend_mode',u.BlendMode.BLEND_ADDITIVE if halo else u.BlendMode.BLEND_OPAQUE)
 prop(m,'two_sided',False)
 prop(m,'used_with_instanced_static_meshes',True)
 color=node(m,u.MaterialExpressionVectorParameter,parameter_name='Color',default_value=u.LinearColor(.015,.18,1,1))
 energy=node(m,u.MaterialExpressionScalarParameter,parameter_name='Energy',default_value=12.)
 mul=node(m,u.MaterialExpressionMultiply);wire(color,'',mul,'A');wire(energy,'',mul,'B')
 output(mul,m,u.MaterialProperty.MP_EMISSIVE_COLOR)
 if halo:
  fresnel=node(m,u.MaterialExpressionFresnel,exponent=2.5,base_reflect_fraction=.08)
  opacity=node(m,u.MaterialExpressionScalarParameter,parameter_name='Opacity',default_value=.18)
  mult=node(m,u.MaterialExpressionMultiply);wire(fresnel,'',mult,'A');wire(opacity,'',mult,'B')
  output(mult,m,u.MaterialProperty.MP_OPACITY)
 ml.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m)
 print('PLASMA_MATERIAL',m.get_path_name())
build('M_PlasmaCore')
build('M_PlasmaHalo',True)
src='/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_5a'
a=u.load_asset(DEST+'/AS_PlasmaCast') or u.EditorAssetLibrary.duplicate_asset(src,DEST+'/AS_PlasmaCast')
assert a
u.EditorAssetLibrary.save_loaded_asset(a)
print('PLASMA_ANIMATION',a.get_path_name(),a.get_play_length(),a.get_editor_property('skeleton').get_path_name())
