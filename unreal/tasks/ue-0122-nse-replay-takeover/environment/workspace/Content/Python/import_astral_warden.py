"""Author native skeletal/material assets; optional AW_INSTALL updates BP_Manny asset defaults.
Run in full Unreal Editor with -ExecutePythonScript; keep interactive editor closed for install.
"""
import unreal as u,json
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();ART=ROOT/'SourceArt/AstralWarden'
DEST='/Game/NightSkyEngine/CharacterAssets/AstralWarden'
at=u.AssetToolsHelpers.get_asset_tools();ml=u.MaterialEditingLibrary
p=lambda o,k,v:o.set_editor_property(k,v)
def node(m,cl,**kw):
 n=ml.create_material_expression(m,cl)
 for k,v in kw.items():p(n,k,v)
 return n
def con(a,o,b,i):assert ml.connect_material_expressions(a,o,b,i)
def out(n,o,m,s):assert ml.connect_material_property(n,o,s)
def ci(name):
 q=u.CustomInput();p(q,'input_name',name);return q
def custom(m,code,ty,ins):
 n=node(m,u.MaterialExpressionCustom,code=code,output_type=ty);p(n,'inputs',[ci(k) for k in ins]);return n
def scalar(m,v,slot):out(node(m,u.MaterialExpressionConstant,r=v),'',m,slot)
# Shared material contracts include native battle flash/tint parameters.
specs=[('WovenGraphite',(.019,.028,.043),.12,.58),('DeepJadeEnamel',(.024,.15,.17),.72,.25),('IvoryCeramic',(.76,.81,.75),.25,.28),('ChampagneTitanium',(.59,.37,.13),.86,.27),('GraphiteGaskets',(.009,.015,.022),.10,.49),('AetherLight',(.035,.8,.67),.25,.23),('AubergineTextile',(.15,.025,.075),.02,.66)]
masters=[]
for idx,(name,color,metal,rough) in enumerate(specs):
 path=DEST+'/Materials/M_AW_'+name
 if globals().get('AW_REUSE_MATERIALS',False):masters.append(u.load_asset(path));continue
 m=u.load_asset(path) or at.create_asset('M_AW_'+name,DEST+'/Materials',u.Material,u.MaterialFactoryNew());ml.delete_all_material_expressions(m)
 ml.set_material_usage(m,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
 depth=node(m,u.MaterialExpressionMaterialFunctionCall,material_function=u.load_asset('/Game/NightSkyEngine/Shared/MaterialFunctions/MF_OrthoBlendAndDepthOffset'));out(depth,'Result',m,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 uv=node(m,u.MaterialExpressionTextureCoordinate)
 base=node(m,u.MaterialExpressionVectorParameter,parameter_name='SurfaceColor',default_value=u.LinearColor(*color,1))
 if idx in (0,6):
  code='float2 q=UV*480;float weave=.92+.08*sin(q.x*6.283)*sin(q.y*6.283);float2 f=abs(frac(UV*18)-.5);float brocade=smoothstep(.018,.028,abs(f.x-f.y));return C*weave*'+('lerp(.72,1,brocade);' if idx==6 else '1;')
 else:code='return C*(.985+.015*sin(UV.x*1750+sin(UV.y*130)));'
 surf=custom(m,code,u.CustomMaterialOutputType.CMOT_FLOAT3,['UV','C']);con(uv,'',surf,'UV');con(base,'',surf,'C')
 mul=node(m,u.MaterialExpressionVectorParameter,parameter_name='MulColor',default_value=u.LinearColor(1,1,1,1));mult=node(m,u.MaterialExpressionMultiply);con(surf,'',mult,'A');con(mul,'',mult,'B')
 add=node(m,u.MaterialExpressionVectorParameter,parameter_name='AddColor',default_value=u.LinearColor(0,0,0,0));sum_n=node(m,u.MaterialExpressionAdd);con(mult,'',sum_n,'A');con(add,'',sum_n,'B');out(sum_n,'',m,u.MaterialProperty.MP_BASE_COLOR)
 micro=custom(m,'return saturate(R + .025*sin(UV.x*913)*sin(UV.y*857));',u.CustomMaterialOutputType.CMOT_FLOAT1,['UV','R']);con(uv,'',micro,'UV');con(node(m,u.MaterialExpressionScalarParameter,parameter_name='SurfaceRoughness',default_value=rough),'',micro,'R');out(micro,'',m,u.MaterialProperty.MP_ROUGHNESS)
 scalar(m,metal,u.MaterialProperty.MP_METALLIC)
 if idx in (0,6):
  n=custom(m,'float2 q=UV*3015.9;return normalize(float3(.12*cos(q.x)*sin(q.y),.12*sin(q.x)*cos(q.y),1));',u.CustomMaterialOutputType.CMOT_FLOAT3,['UV']);con(uv,'',n,'UV');out(n,'',m,u.MaterialProperty.MP_NORMAL)
 if idx==5:
  e=node(m,u.MaterialExpressionMultiply,const_b=2.3);con(base,'',e,'A');out(e,'',m,u.MaterialProperty.MP_EMISSIVE_COLOR)
 else:out(add,'',m,u.MaterialProperty.MP_EMISSIVE_COLOR)
 ml.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m);masters.append(m)
palettes=[]
for palette in [1,2]:
 mis=[]
 for idx,((name,color,metal,rough),master) in enumerate(zip(specs,masters)):
  n=f'MI_AW_{name}_P{palette}';mi=u.load_asset(DEST+'/Materials/'+n) or at.create_asset(n,DEST+'/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew());ml.set_material_instance_parent(mi,master)
  if palette==2:color={1:(.23,.035,.074),3:(.39,.45,.53),5:(1,.21,.035),6:(.025,.08,.15)}.get(idx,color)
  ml.set_material_instance_vector_parameter_value(mi,'SurfaceColor',u.LinearColor(*color,1));u.EditorAssetLibrary.save_loaded_asset(mi);mis.append(mi)
 palettes.append(mis)
if not globals().get('AW_SKIP_IMPORT',False):
 opt=u.FbxImportUI();p(opt,'import_mesh',True);p(opt,'import_as_skeletal',True);p(opt,'mesh_type_to_import',u.FBXImportType.FBXIT_SKELETAL_MESH);p(opt,'automated_import_should_detect_type',False)
 p(opt,'skeleton',u.load_asset('/Game/ControlRig/Characters/Mannequins/Meshes/SK_Mannequin'))
 p(opt,'import_animations',False);p(opt,'import_materials',False);p(opt,'import_textures',False);p(opt,'create_physics_asset',False)
 d=opt.skeletal_mesh_import_data;p(d,'update_skeleton_reference_pose',False);p(d,'use_t0_as_ref_pose',False);p(d,'normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS);p(d,'preserve_smoothing_groups',True)
 t=u.AssetImportTask()
 for k,v in dict(filename=str(ART/'Exports/SK_AstralWarden.fbx'),destination_path=DEST+'/Meshes',destination_name='SK_AstralWarden',automated=True,replace_existing=True,save=True,options=opt).items():p(t,k,v)
 at.import_asset_tasks([t]);print('AW_IMPORTED',t.imported_object_paths)
mesh=u.load_asset(DEST+'/Meshes/SK_AstralWarden');assert mesh
original=u.load_asset('/Game/ControlRig/Characters/Mannequins/Meshes/SKM_Manny')
p(mesh,'physics_asset',original.get_editor_property('physics_asset'))
p(mesh,'post_process_anim_blueprint',original.get_editor_property('post_process_anim_blueprint'))
# FBX roundtrip changes two mirrored twist rotations. Restore mesh bind data from the
# untouched reference mesh; this does not modify the shared skeleton or animation assets.
reference=u.SkeletonModifier();assert reference.set_skeletal_mesh(original)
modifier=u.SkeletonModifier();assert modifier.set_skeletal_mesh(mesh)
for bone in ['thigh_twist_01_r','thigh_twist_02_r']:
 print('AW_BIND_BEFORE',bone,modifier.get_bone_transform(bone,False),'REFERENCE',reference.get_bone_transform(bone,False))
 assert modifier.set_bone_transform(bone,reference.get_bone_transform(bone,False),True)
print('AW_BIND_COMMIT',modifier.commit_skeleton_to_skeletal_mesh())

# Preserve material-slot names imported from Blender, assign by name rather than FBX reorder.
slots=list(mesh.get_editor_property('materials'));slot_order=[]
for sl in slots:
 name=str(sl.get_editor_property('material_slot_name'))
 ix=next(i for i,(n,*_) in enumerate(specs) if n in name);slot_order.append(ix);p(sl,'material_interface',palettes[0][ix])
p(mesh,'materials',slots)
sms=u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
print('AW_LOD_GENERATE',sms.regenerate_lod(mesh,3,False,False))
u.EditorAssetLibrary.save_loaded_asset(mesh)
(ART/'unreal-import.json').write_text(json.dumps({'mesh':mesh.get_path_name(),'skeleton':mesh.get_editor_property('skeleton').get_path_name(),'slot_order':slot_order,'lods':sms.get_lod_count(mesh)},indent=2))
if globals().get('AW_INSTALL',False):
 bp=u.load_asset('/Game/NightSkyEngine/Blueprints/Characters/Manny/BP_Manny');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem)
 shadow=u.load_asset('/Game/NightSkyEngine/Shared/CharaMaterial/M_CharaShadow')
 for h in sub.k2_gather_subobject_data_for_blueprint(bp):
  data=u.SubobjectDataBlueprintFunctionLibrary.get_data(h);ob=u.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data,bp)
  if isinstance(ob,u.SkeletalMeshComponent):
   ob.set_skeletal_mesh_asset(mesh)
   p(ob,'override_materials',[shadow]*len(slots) if ob.get_name().startswith('Shadow') else [palettes[0][i] for i in slot_order]);print('AW_COMPONENT_REPLACED',ob.get_name())
 u.BlueprintEditorLibrary.compile_blueprint(bp);u.EditorAssetLibrary.save_loaded_asset(bp)
 for j,path in enumerate(['/Game/NightSkyEngine/CharacterAssets/Mannequins/Materials/Color01/DA_MannyMaterials01','/Game/NightSkyEngine/CharacterAssets/Mannequins/Materials/Color02/DA_MannyMaterials02']):
  data=u.load_asset(path);rows=list(data.get_editor_property('material_structs'))
  for row in rows:p(row,'material',[shadow]*len(slots) if str(row.get_editor_property('name'))=='Shadow' else [palettes[j][i] for i in slot_order])
  p(data,'material_structs',rows);u.EditorAssetLibrary.save_loaded_asset(data,False)
 print('AW_INSTALL_COMPLETE')
print('AW_AUTHORING_COMPLETE',mesh.get_path_name())
