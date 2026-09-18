# Astral Warden validation — 2026-09-15

Environment: Unreal Engine 5.8.2 Linux/Vulkan, Blender 5.2.1.

## Passed on the finished assets

- Editor Development compile/link succeeded.
- Final native Automation queue: **5/5 Success** at 09:24 UTC, including:
  - `NightSkyEngine.Fighter.AstralWarden.RigAndReplacement`: shared skeleton, all original
    161 bone names/parents/rest transforms, physics asset, corrective animation class,
    Body/Shadow references, seven costume slots, projection/depth parameters and three LODs.
  - `NightSkyEngine.Fighter.AstralWarden.ExistingAnimationPoses`: 25 existing animation clips
    sampled at 0%, 37% and 80%. Head, both hands, both feet and pelvis match original Manny
    within 0.1 cm. The test additionally requires movement away from bind pose so unevaluated
    static actors cannot pass as animation coverage.
  - Existing Jade Ascension `CollisionAndClearance`, `MeshBudgets` and `RenderedBattle`.
    The actual match spawns BP_Manny in both costume colors, using the original HUD and camera.
- Original procedural kit: 95,222 source triangles, 50,212 vertices, seven material sections,
  three decreasing Unreal LODs. New Unreal assets approximately 22 MB; editable source 16 MB.
- Final match screenshot visually reviewed after a smooth chest undersuit recess eliminated
  the observed plate intersections. New geometry, source FBX, bind correction and LODs all
  reflect that final fit.
- Linux Development BuildCookRun passed compile, cook, stage, package and archive with exit 0
  after the final fit. Archive: `Saved/AstralWardenDevelopment/Linux/NightSkyEngine.sh`.

Automation invocation:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/NightSkyEngine.uproject" \
 -unattended -nopause -nop4 -RenderOffscreen -ResX=1920 -ResY=1080 \
 '-ExecCmds=t.IdleWhenNotForeground 0,Slate.bAllowThrottling 0,Automation RunTests NightSkyEngine.' \
 '-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False' \
 '-testexit=Automation Test Queue Empty'
```

As in the prior arena run, this engine's forced TestExit returns process code 1 despite the
completed queue reporting all tests successful. Existing upstream uninitialized-struct,
GameplayTag and Blueprint pin warnings are not claimed fixed by this art replacement.

## Evidence and limits

- Local gallery: `Saved/Screenshots/AstralWarden/index.html`.
- Final real match: `Saved/Screenshots/AstralWarden/pie-fight.png`.
- Native engine logs: `Saved/Logs/AstralWarden/` (ignored local evidence).
- Animation tests compare rig poses, not an exhaustive triangle-intersection proof for every
  frame of every possible move combination. Reviewed idle, crouch and attack poses are listed
  in the capture script. No full human match, netplay or Mac/Windows package acceptance run.
- Existing mannequin body topology/weights are reused beneath original new costume geometry;
  there is no new move set, facial animation or cloth simulation. No gameplay Blueprint graphs
  were added. No commit/push requested for this fighter task.

## Packaged match smoke

The final packaged Development build successfully ran a real match with both Astral Warden
costumes and the native HUD/camera. `packaged-fight.png` was visually reviewed. The process
exited cleanly with code 0 after the bounded benchmark run. Log: `packaged-match.log` in the
local evidence directory.

The review starts the existing arena under GameModeBase, sets process-local BattleData with
both existing CHR_Manny references and color indices 1/2, then opens the arena normally.
It does not modify the packaged content or normal menu flow. Macro commands are retained as
`Saved/Logs/AstralWarden/packaged-review-macro.txt`; Unreal requires the execution file path to
contain `Binaries`, so copy that file to a `Binaries` directory before using `-Exec=<path>`.
The native game resolves the stage from the opened map; the fixture omits the unnecessary
hard Stage pointer to avoid the synchronous asset-loading stall observed in the earlier console fixture.

Smoke flags: `-RenderOffscreen -ResX=1920 -ResY=1080 -unattended -nosound -benchmark -fps=60
-seconds=20`. This is short startup/render acceptance, not full-match or netplay acceptance.
A pre-existing optional GameInstance subsystem warning appeared at startup; it did not prevent
loading the match. Earlier fixture attempts are not counted as successful validation.


Editor handoff: Unreal Editor reopened successfully and opened `SK_AstralWarden` in its
skeletal mesh editor at 09:30 UTC. BP_Manny remains installed as the playable replacement.

## Compatibility audit — 15:35 UTC recheck

Reran the existing five native tests against saved assets: **5 Success (one with warnings)**, including the
rendered PIE match and 25 clips sampled at three times. The compiled test module is newer
than its source. Report: `Saved/Automation/AstralWardenRecheck/index.json`; log:
`Saved/Logs/AstralWarden/compatibility-recheck.log`. The process again returned 1 at
`FEngineLoop::Tick.GScopedTestExit`; this is not a clean exit-code-based CI acceptance run.
The separate read-only asset inspection exited 0. No meshes or gameplay assets were edited.

Read-back independently confirms Body and Shadow both reference SK_AstralWarden and
ABP_Manny, the original non-null PA_Mannequin and ABP_Manny_PostProcess are retained, and
both costume assets have the expected Body/Shadow rows and seven correctly ordered materials.
Unreal reports 72,178 / 23,937 / 13,754 vertices across LOD0/1/2, with seven sections each.
These imported vertex counts differ from Blender source counts because import can split vertices.
Evidence: `Saved/Logs/AstralWarden/compatibility-audit.json` and `compatibility-readback.py`.
The fresh PIE screenshot shows both costume palettes with the normal HUD and camera.

**Open material interface gap:** all seven new body surfaces expose MulColor, AddColor and
ScreenSpaceDepthOffset, but omit Transparency, DamageColor and DamageColor2. BattleObject.cpp
sets those three omitted parameters during its material update. Effects relying on them cannot
affect these surfaces. This confirms unsupported interfaces, not that a particular current
Manny move has a visible regression; that would require a targeted gameplay/effect check.
Existing native tests do not check these parameters and therefore do not catch this gap.

Coverage remains bounded: animation assertions compare six joint positions, not every bone
rotation, skin weight or deformed vertex. They do not prove that armor never intersects during
all frames, blends or moves. The rig test only requires a non-null AnimClass and iterates costume
rows without requiring their names; the supplemental read-back verified the current assets but
these are still future regression-test gaps. Prior Linux package/smoke evidence remains from
09:26–09:28; packaging was not rerun for this audit. Full-match, netplay and other OS acceptance
remain untested. Unsaved interactive-editor changes are outside this saved-asset audit.
