# Jade Ascension

An original cultivation-realm arena for NightSkyEngine, authored in Unreal Engine 5.8.2.
The existing TestMap_PL map path, battle GameMode, player starts, camera defaults and native
rollback fighting simulation are retained. New content lives under
`/Game/NightSkyEngine/Stages/JadeAscension`.

## Composition and safety

- A dressed stone combat terrace spans 40 × 12 metres; its surface is exactly Z=0.
  Its continuous simple box supplies Unreal query/physics collision. NightSkyEngine's
  independent integer-space push/hit/floor and stage-bound rules remain authoritative.
- The native stage bounds are ±3200 × OBJ_SCALE 0.43 = ±1376 cm. The deck extends beyond
  those limits. No stair, raised prop, railing or visual effect crosses the reserved region
  X ±1700, Y ±350, Z 0–2600 cm. Air attacks and camera tracking retain open space.
- Scenery layers: lantern balustrade, garden/lotus lights/blossoms, moon gate and pagodas,
  elevated shrine islands, then the distant cloud-sea cyclorama. The multiple levels are
  visual depth; no new floor-break, ring-out or platform-transition mechanic is introduced.
- Background geometry has NoCollision so it cannot unexpectedly block fighters, projectiles,
  traces or cinematic cameras. The nearby visible props are outside the combat volume.
- Moon-blue key, peach rim, amber lantern pools and jade braziers supply complementary light.
  Local lights do not cast dynamic shadows. The main light supplies readable contact shadows.

## Reproduction

```bash
blender --background -noaudio --python SourceArt/JadeAscension/build_arena.py
# Close the interactive editor before authoring the saved map.
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/NightSkyEngine.uproject" \
 -ExecutePythonScript="$PWD/Content/Python/build_jade_ascension.py" \
 -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nop4 -RenderOffscreen
"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" NightSkyEngineEditor Linux Development \
 "$PWD/NightSkyEngine.uproject" -WaitMutex
```

The full editor is required for the StaticMeshEditorSubsystem that builds collision and LODs.
The scene can also be regenerated after import by executing the script with JA_SKIP_IMPORT=True.
Use the equivalent engine executable on other platforms. Authoring paths derive from the
project or script directory. Python is asset-authoring tooling, not runtime gameplay.
All new placed scene actors are native Unreal mesh, light, fog and post-process actors.
The project already contains Blueprint gameplay and UI; this task adds no Blueprint graphs.

`SourceArt/JadeAscension/JadeAscension.blend` contains the editable 13-piece mesh kit.
`Exports/manifest.json` records source triangle counts and collision policy. Each ordinary
mesh gets one material slot and three conventional LODs (100%, 50%, 22%); the cyclorama is a
128-triangle single-LOD background. Nanite and hardware ray tracing are not required.
RGB corner colours encode the surface palette; alpha identifies luminous inlays. The shared
material supplies subtle surface grain and emission. Combat stone has procedural seams and
flush concentric bronze inlays. No source textures or meshes depend on absolute machine paths.

## Verification

Run the focused C++ Automation suite:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/NightSkyEngine.uproject" \
 -unattended -nopause -nullrhi \
 '-ExecCmds=Automation RunTests NightSkyEngine.Arena.JadeAscension' \
 '-testexit=Automation Test Queue Empty'
```

Tests check the saved floor, full-range ground traces, swept air clearance, scenery bounds,
collision policy, material slots, authored colour data and LOD triangle budgets. Rendered PIE
and packaged evidence, plus any failures or limits, are recorded in the final validation notes.

The old saved map is backed up locally in `Saved/JadeAscensionBackup`. The original repository
also reports uninitialized struct properties and missing TestMap lighting build data on startup;
those pre-existing diagnostics are distinct from arena validation.
