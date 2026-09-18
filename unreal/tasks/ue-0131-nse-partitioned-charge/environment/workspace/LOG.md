# Active decisions

## 2026-09-15 — Jade Ascension arena

- User requested a high-quality mystical cultivation arena with multiple layers, expressive
  lighting and collision, without scenic interference in fights. Chose an original moon-gate
  sanctuary above a cloud sea with physical temple architecture and floating-island depth.
- Keep TestMap_PL's identity and existing gameplay/camera actors. Native battle coordinates
  map X to world X, fighting height to world Z and depth to world Y, with OBJ_SCALE 0.43.
  Native ±3200 stage bounds correspond to ±13.76 m. Use a 40 × 12 m deck at Z=0, with all raised
  scenery behind Y=-350 cm and extra margin for attacks/cinematics.
- Multiple layers are scenic elevation and depth, compatible with the existing 2D fight.
  No obstacle, staircase or ring-out rule is added to the combat lane. The deck gets one solid
  collision box; unreachable scenic geometry has no collision. Native fighting rules stay intact.
- Original Blender kit, one shared vertex-colour PBR/emissive material, three conventional LODs
  and a lightweight distant image cyclorama avoid reliance on marketplace content or Nanite.
- Use editor Python solely for asset import, material construction and saved scene composition.
  Verification is native C++ Automation in the existing project module, editor-only guarded.
- Preserve the user's pre-existing Linux rendering settings in Config/DefaultEngine.ini.
  Interactive editor crashed in the NVIDIA Vulkan swapchain during initial window interaction;
  authoring proceeds in a separate NullRHI process, with rendered validation still required.
- Assets and image generation provenance are in docs/ASSETS.md. No commit or push requested.
- Final authoring fixes use named Rotator fields (Python positional order is roll/pitch/yaw)
  and rotate the FBX cyclorama 180 degrees so it occupies negative world Y behind combat.
  Lanterns now specify lumens explicitly, preventing the first render's over-bright light pools.
- Native validation passed CollisionAndClearance, MeshBudgets and RenderedBattle, 3/3 completed
  tests. Actual PIE fighters stood at X ±127.925, Y=0, Z=0 with the native camera at
  (0,544.380,103.852), pitch 2.5/yaw -90. The rendered match shows intact HUD and readable
  fighters on the unobstructed deck. Review battle settings are process-local and restored.
- Updated the project association to 5.8, matching the engine used to author the new assets and
  the successful editor/runtime builds. Earlier user Linux RHI settings remain unchanged.
  The editable Blender scene is arranged as a gallery; FBX pivots and exported geometry are
  unchanged by that presentation-only arrangement.
- Linux Development BuildCookRun passed compile, cook, stage, package and archive with exit 0.
  The packaged arena renders from a temporary spectator camera. Direct map travel skips the
  game's character selection, so packaged match acceptance is not claimed; actual fighters
  and the native battle camera passed the separate PIE test. Deferred screenshot commands
  avoid capturing the initial camera before its first update.
- Added a narrow gitignore exception for the original celestial PNG so future commits retain
  the editable sky source. Removed Blender's generated backup; retained the final .blend.
- Reopened the interactive editor on TestMap_PL and confirmed the process remains live.
  Startup viewport realtime toggling emitted an engine ensure, then the editor continued
  ticking and entered PIE. No scene edits were needed after final validation.

## Astral Warden fighter — 2026-09-15

- Preserve BP_Manny and its existing skeleton, animation blueprint, fighter data, hitboxes and
  moves. Replace the Body and Shadow skeletal asset defaults and both costume material arrays.
- Build an original ivory-masked cultivation fighter with jade enamel, champagne metal,
  aubergine textile and cyan accents. Keep silhouette detail out of major joint bends.
- Reuse the existing weighted Manny body as an undersuit and rig reference; remove its hidden
  head. Model armor and mask procedurally in Blender, rigidly weighted to existing bones.
- Blender import retains a centimetre-scaled parent. Convert body geometry to world metres for
  authoring, then preserve the original armature transform and inverse parent matrix on export.
- Review caught initial surface intersections. Removed unnecessary undersuit shrinking (which
  separated duplicate seams), moved front plates clear of the base, and fitted the new mask
  outside the cowl. No skeleton rest transforms were edited.
- Native tests compare original and replacement rest bones and sample all existing animation
  clips at three times, as well as checking BP/material/shadow references and LOD budgets.
- First full validation passed 25 animation clips sampled at three times each, but found two
  mirrored thigh-twist bind rotations outside tolerance and unsaved costume arrays. Restored
  those local transforms through SkeletonModifier on the replacement mesh only; forced asset
  saves for the nested material arrays. Kept the strict test tolerances.
- Nested Unreal arrays yield copied structs during iteration. Material slot and MaterialData
  authoring now converts those arrays to Python lists before editing and reassigns the full
  lists, then force-saves costume assets. Read-back confirmed seven actual material entries
  per Body/Shadow row in both palettes. Initial failure evidence is retained locally.
- Visual PIE review found the original character material's projection/depth WPO function
  missing on new surfaces. All seven now call the same MF_OrthoBlendAndDepthOffset as Manny,
  preserving native camera projection and shadow separation. Mesh also retains the original
  ABP_Manny_PostProcess corrective graph. Added native assertions for both interfaces.
- Strengthened animation validation to require visible joint movement away from bind pose,
  in addition to comparing original and new joints; static pose equality alone is insufficient.
- Final fitted mesh passed all five native tests at 09:24 UTC, including true articulation
  across 25 existing clips. Linux Development packaging passed with exit 0. The final packaged
  match rendered both palettes and the native HUD, then exited 0. Temporary review BattleData
  omits the hard Stage pointer to avoid the observed synchronous asset-loading stall in the SET fixture.
  No menu/gameplay assets were modified for that fixture.
- Reopened Unreal Editor and confirmed AW_EDITOR_HANDOFF_READY with SK_AstralWarden open
  in the skeletal mesh editor. Final screenshots and logs are indexed in the local gallery.

### Astral Warden compatibility recheck — 2026-09-15 15:35 UTC

- Reran all five existing native tests against saved assets; all report Success. Read-only
  asset inspection confirms exact component animation classes, costume row/material assignments,
  original physics/post-process references and three LODs. No art/gameplay assets changed.
- Recorded a material interface gap: new body surfaces omit Transparency, DamageColor and
  DamageColor2 consumed by native material updates. Existing tests do not cover those effects.
- Documented bounded animation coverage and future regression-test gaps instead of treating
  five passing tests as exhaustive compatibility proof. Automation still exits 1 through
  GScopedTestExit despite successful test results; the separate asset read-back exits 0.
- Preserved current editor configuration changes. Package evidence is from the earlier run;
  no new package, commit or push was part of this audit.

### Requested repository handoff — 2026-09-15

- User requested commit and push after the compatibility audit. Split the pending work into
  arena, fighter and audit commits on main, retaining source artwork and asset provenance.
- Included the Linux rendering settings used by the validated build. Left automatic
  DefaultEditor.ini preview-profile changes local. The recorded material limitations remain open.

### Directional light priority fix — 2026-09-15

- The live viewport reported two directional lights competing for forward shading,
  translucency, water and volumetric fog. Read-back showed MoonKey and PeachRim both at priority 0.
- Set JA_MoonKey ForwardShadingPriority to 1 and JA_PeachRim to 0. Kept their existing
  intensity, color, transform and shadow settings. Updated the arena generator to reproduce this.
- Applied the change in the open editor and saved TestMap_PL, preserving the user's current
  scene edits, including their recent actor deletions. Read-back confirms a unique highest
  priority; the warning disappeared from the rendered live viewport. No full rebuild required.

### Restore real-time gameplay under rendering load — 2026-09-15

- Disabled Engine.bUseFixedFrameRate in DefaultEngine.ini. The local fighter runner
  already accumulates DeltaTime and advances combat in fixed 1/60-second steps;
  forcing the engine delta to 1/60 made simulation slow down below 60 rendered FPS.
- Preserved the combat step size and authored animation timing. Restarted the base
  editor to apply the setting and ran a rendered PIE timing check capped at 30 FPS.
- Validation: 8.0154 wall-clock seconds advanced 8.0194 game seconds (1.0005x),
  at 28.20 measured render FPS; global time dilation was 1.0. Restored the normal
  60 FPS render cap after the check and left PIE running for playtesting.
- Evidence: /tmp/nse-speed-validation.json and /tmp/nse-base-normal-speed.log.
  No C++ or asset changes, rebuild, packaging run, commit or push for this fix.

### Restore editor playtest performance — 2026-09-15

- Found two obsolete task-121 solution editors still running alongside the base
  project. They used roughly four CPU cores and 9 GB of RAM together. Closed both
  cleanly through their window-close requests; only the base editor remains.
- Measured the same live PIE scene with the same 60 FPS cap before and after.
  Baseline: 29.65 FPS, p95 45.97 ms, p99 52.72 ms, 16 frames over 50 ms in 20 s.
  After cleanup: 59.91 FPS, p95 18.30 ms, p99 19.44 ms, no frames over 33.33 ms
  in 20 s. A subsequent 25 s sample with movement/jump/attack key input measured
  59.99 FPS, p99 19.15 ms, maximum 21.85 ms, no frames over 33.33 ms.
- Frame measurements use monotonic intervals between Slate post-tick callbacks
  in rendered PIE, not DCV/browser presentation timing. CSV cross-check after
  warm-up: 16.70 ms mean frame time, 10.82 ms game thread, 7.52 ms render thread.
- Kept the existing art, render quality, and real-time combat timing. No gameplay
  code or asset changes were necessary; no rebuild or Automation rerun required.
  Avoid leaving duplicate playtests running during interactive use.
- Evidence saved under Saved/Performance/2026-09-15; engine CSV under
  Saved/Profiling/CSV/Profile(20260915_202147).csv. Stopped profiling and removed
  the temporary measurement callback after verification; left base PIE running.
