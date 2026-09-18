NightSkyEngine's rollback snapshots are raw process memory that cannot leave the process. Implement the declared export, a byte array from a live battle, and import, replacing a battle's whole gameplay state from one. A checkpoint is a pure function of gameplay state: instances with the same configuration and state export identical bytes; any state difference changes them; exporting changes nothing. Presentation (audio, camera, HUD, particles, colours, drawing transforms) and process identity do not count. The array starts with the ASCII bytes NSKYCKPT and the schema version, a little-endian uint32, currently 1.

State includes the SaveGame-tagged fields of players, of every registered player state (current or not, on screen or not) and of every battle extension, each active pooled object's slot and script, and every pool slot's update order, active or inactive. References to battle objects, players and registered states resolve to the target's own, inactive slots included. Configuration (rules, registrations, templates, collision data) stays the target's.

After an import the target behaves on every later frame, under the same inputs, exactly as the source, with no prior state surviving, and re-exporting reproduces the bytes. Import runs no gameplay (nothing executes, no callback fires); importing the same bytes twice equals once. Each active object returns to its source slot with the checkpoint's saved script fields over a fresh spawn's settings from the owner's template; later spawns, even there, use the target's unchanged templates.

Import rejects without touching the battle: damaged bytes (altered, truncated or extended) are malformed; the right magic with another version is unsupported, whatever follows; a different lineup (players per side and their classes, pool capacity, extensions by class and name, ordered) or a registration the target lacks is a lineup mismatch. Arbitrary bytes never crash. A newly activated pooled object has no collision boxes until its script sets a cel, whatever its slot held before. Rollback keeps its behaviour; storage, transfer and UI are out of scope.

## Files to edit

You must only change the following files:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/NightSkyGameState.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/NightSkyGameState.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/BattleCheckpoint.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/BattleObject.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/BattleObject.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/PlayerObject.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/PlayerObject.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Script/StateMachine.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Script/StateMachine.cpp`
