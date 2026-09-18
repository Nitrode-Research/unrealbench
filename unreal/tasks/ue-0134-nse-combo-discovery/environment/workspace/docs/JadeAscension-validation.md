# Jade Ascension verification — 2026-09-15

Environment: Unreal Engine 5.8.2 on Linux/Vulkan; Blender 5.2.1.

## Passed

- Original 13-mesh Blender kit exported successfully: 85,901 unique source triangles.
  New Unreal assets total approximately 5 MB. Each ordinary mesh has one material section
  and three conventional LODs; the distant cyclorama has 128 triangles.
- `NightSkyEngineEditor Linux Development` compiled successfully, including native tests.
- Saved scene authoring completed with 66 actors. The existing Floor actor and gameplay map
  remain at their stable identities. Core fighting classes, input and camera logic are unchanged.
- Final rendered Automation run reported **3/3 Success** and a completed nonempty queue:
  `NightSkyEngine.Arena.JadeAscension.CollisionAndClearance`, `MeshBudgets`, `RenderedBattle`.
  Collision checks cover 27 ground traces over the native ±1376 cm stage, four swept air
  corridors, one simple floor box and a clear volume around the fighters. Mesh checks cover
  materials, vertex colour data, LOD counts and triangle budgets.
- The native PIE fixture spawned the shipped Manny fighters at X ±127.925, Y=0, Z=0.
  Native battle camera: (0,544.380,103.852), pitch 2.5, yaw -90, roll 0. The real HUD and both
  fighters were captured and visually inspected. This is a short placement/rendering match
  smoke; it does not claim full move-list, netplay or entire-match acceptance.
- Final 1920 × 1080 art captures cover centre, left/right stage ends, jump height and a wider
  overview. The deck remains unobstructed; lower gardens, blossom trees, temple silhouettes,
  floating crags and the distant sky form distinct depth layers. Lantern intensities were
  reduced and explicitly set in lumens after the first lighting review showed overexposure.

Exact final Automation command:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/NightSkyEngine.uproject" \
 -unattended -nopause -nop4 -RenderOffscreen -ResX=1920 -ResY=1080 \
 '-ExecCmds=t.IdleWhenNotForeground 0,Slate.bAllowThrottling 0,Automation RunTests NightSkyEngine.Arena.JadeAscension' \
 '-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False' \
 '-testexit=Automation Test Queue Empty'
```

The process returned 1 from this engine's forced TestExit path; explicit test results were all
Success. Pre-existing NightSkyEngine uninitialized-struct diagnostics appear at startup,
before the arena tests. They are not new arena assertion failures and are not claimed fixed.

## Evidence and reproduction

- Preview gallery: `Saved/Screenshots/JadeAscension/index.html`.
- Real match: `Saved/Screenshots/JadeAscension/pie-fight.png`.
- Art views: `fight-center.png`, `fight-left.png`, `fight-right.png`, `jump-space.png`,
  `arena-overview.png` in that directory.
- Logs: `Saved/Logs/JadeAscension/` (local ignored evidence; commands and capture tooling
  are repository-readable).
- Art capture recipe: `Content/Python/capture_jade_ascension.py`, launched with the full editor
  and `-ExecCmds="py <absolute script path>" -RenderOffscreen`. Review cameras are transient;
  no fixture camera or test battle setting is saved to the gameplay map or user config.

## Package and limits

Linux Development packaging was requested with:

```bash
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
 -project="$PWD/NightSkyEngine.uproject" -noP4 -platform=Linux -clientconfig=Development \
 -build -cook -stage -pak -package -archive \
 -archivedirectory="$PWD/Saved/JadeDevelopment" -unattended -utf8output -nocompileeditor
```

BuildCookRun completed successfully with exit code 0; Linux Development compile, cook, stage,
package and archive all passed. The local executable is
`Saved/JadeDevelopment/Linux/NightSkyEngine.sh`.

The packaged map loaded successfully. A direct battle-map launch bypasses the existing
character-selection setup and did not initialize a match, so this is not claimed as a
packaged match acceptance test. A second run temporarily overrode the process's game mode
with `/Script/Engine.GameModeBase` and used `BugItGo 0 2450 1100 -14 -90 0` to inspect the
cooked arena from a spectator camera. `packaged-arena.png` was visually reviewed: floor,
architecture, blossoms, emissive lights and backdrop all render. This wide diagnostic camera
shows the cyclorama edges outside the intended battle framing. The native PIE match capture
is the evidence for the actual fighting camera and both characters.

The final spectator smoke exited successfully with code 0. Packaged smoke used `-RenderOffscreen -benchmark -fps=60 -seconds=15`; ten nested `defer`
commands delayed HighResShot until after camera initialization. Logs are retained locally
as `Saved/Logs/JadeAscension/package.log`, `packaged-direct-map.log` and
`packaged-arena.log`. Startup-only screenshots were not used as successful visual evidence.

No new gameplay mechanics,
destructible floors, stage transitions, animated weather, music or sound effects were added.
The architecture and vegetation are stylized static artwork. Mac and Windows packaged runs
and full human matches were not performed. No commit or push was requested for this arena.

Interactive editor handoff: TestMap_PL reopened successfully. A viewport realtime-toggle
ensure was nonfatal; subsequent log frames confirm continued execution and PIE startup.
