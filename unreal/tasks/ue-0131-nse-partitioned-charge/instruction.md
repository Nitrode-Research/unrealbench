Add partitioned charge commands to the battle input system, which today matches only contiguous holds. A state may carry any number of them beside its ordinary input condition lists and is entered when either kind succeeds; ordinary commands must behave exactly as today. A command has a charge input, hold requirement, gap limit, lifetime and trigger: release of the charged input or a press of a trigger input. An input is held when all its bits are in the frame's facing-relative input.

Each command accumulates independently, one per held frame; an interruption longer than the gap limit, or an age (unfrozen frames from and including its first held frame) beyond the lifetime, clears the charge. A cleared charge starts again from zero on the next held frame; the clearing frame never begins one. Frozen frames (hitstop, super freeze) count as nothing; edges are read between consecutive unfrozen frames. A command is satisfied when its trigger fires with charge through the previous unfrozen frame at least the requirement. A side switch keeps the charge and re-reads the previous sample under the new facing, so a stick that stays put is no edge.

Charge commands share the engine's entry gates and state priority with ordinary commands; a satisfied command starts its move only if the move wins entry. Entry consumes every satisfied command of the started move, whichever input caused it; a consumed command leaves that frame at zero. Every other command accumulates and expires that frame as usual; a satisfied command whose move did not start is not remembered.

Only primary state machines are in scope. A command with an empty charge input, a requirement below one or a press trigger without a trigger input never activates; a negative gap limit means zero. Battle start and round reset begin with no charge and nothing previously held. Save and restore must carry everything these rules depend on: an identical replay after a restore reproduces the same move starts; a different continuation inherits nothing from the discarded future.

## Files to edit

You must only change the following files:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Script/State.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Misc/InputBuffer.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Misc/InputBuffer.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/PlayerObject.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/PlayerObject.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/NightSkyGameState.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/NightSkyGameState.cpp`
