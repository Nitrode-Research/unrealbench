# Astral Warden — BP_Manny visual replacement

An original masked armored fighter: ivory geometric faceplate and swept crest, teal enamel,
champagne titanium, graphite woven undersuit, cyan eye/power inlays and aubergine hip tabs.
The alternate costume swaps teal/cyan/gold for crimson/amber/silver. Broad shapes and joint
breaks borrow fighting-game design principles from Street Fighter and Guilty Gear without
copying a named character or importing their artwork.

## Compatibility contract

- Stable blueprint: `/Game/NightSkyEngine/Blueprints/Characters/Manny/BP_Manny`.
- Body and Shadow component templates use the new `SK_AstralWarden`. Their animation class,
  transforms, gameplay behavior, move list, hitboxes and references are retained.
- Original skeleton: `/Game/ControlRig/Characters/Mannequins/Meshes/SK_Mannequin`.
  All 161 bones including the FBX armature root remain; no retargeted duplicate skeleton.
- Original weighted Manny body forms the close-fitting undersuit. Hidden head geometry is
  removed and replaced by a new cowl and mask. All armor, mask, trim and short coat tabs are
  original procedural geometry. This is a costumed replacement using the existing proportions,
  not a claim that the base human topology was newly sculpted.
- Armor uses rigid per-bone weights; knuckles follow individual fingers, sabatons split at the
  toe joint, chest plates split across spine bones, and short tabs follow the thighs. No new
  cloth simulation, physics movement or independently simulated attachments.
- Original physics asset retained. Gameplay collision/hitboxes remain native NightSkyEngine.
- Seven surface slots covered by both existing costume MaterialData assets and by the shadow.
  Materials use stable mesh UVs for procedural weave, brocade and fine roughness variation.
  Native `MulColor` and `AddColor` parameters are preserved in the material interface.
  Every surface uses the original MF_OrthoBlendAndDepthOffset WPO function, and the mesh
  retains ABP_Manny_PostProcess for the original corrective animation setup.

## Source and reproduction

`SourceArt/AstralWarden/build_fighter.py` generates the model in Blender 5.2.1 from the included
rig reference. `AstralWarden.blend` is editable; `Exports/SK_AstralWarden.fbx` is the Unreal input.
The original body reference comes from the engine mannequin already in this repository.

`Content/Python/import_astral_warden.py` builds native Unreal materials and imports the mesh.
Run in the full editor with `-ExecutePythonScript=<absolute path> -RenderOffscreen -unattended`.
To also replace BP_Manny's asset defaults, execute with `AW_INSTALL=True` in its globals.
`AW_SKIP_IMPORT=True` skips the FBX step for material-only work. Keep the interactive editor
closed while writing the BP and costume assets. The script does not add Blueprint graph logic.

`Content/Python/capture_astral_warden.py` captures temporary review actors in the arena via
`-ExecCmds="py <absolute path>" -RenderOffscreen`; it does not save review actors into the map.
Native validation: `NightSkyEngine.Fighter.AstralWarden` plus the existing
`NightSkyEngine.Arena.JadeAscension.RenderedBattle` smoke.

Validation results and limits are recorded in `AstralWarden-validation.md`.

FBX reproduction note: two mirrored thigh-twist bone rotations require correction after
import. The import script copies their local bind transforms from SKM_Manny into the new
mesh using SkeletonModifier, then rebuilds LODs. The shared skeleton is never edited.
