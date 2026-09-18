NightSkyEngine applies each hit as its collision walk reaches it, so pool order decides trades, armor and clashes. Decide every contact and clash of a frame from the state at the start of the collision phase, so equivalent encounters end the same in any spawn order or pool slots. Each contact applies with today's damage rules and callbacks, reading hit data and armor as it applies. A landing hit on a fighter attacking at phase start is a counter hit with the strike's counter-hit data, so mutual strikes trade.

Clashes resolve first, as today. A clashing strike contacts nothing that frame yet remains a target for others. Contacts on one target apply in order: higher authored priority, then fighter strikes before other objects', then higher normal-hit damage, then the horizontally nearer attacker. Guard held at phase start blocks every contact it covers before armor is consulted; armor absorbs each covered contact while its hit budget lasts, one unit each; everything else lands. Landing hits extend the attacking side's combo as consecutive hits. Reaction and stun follow the last landing hit, or the last block if nothing landed; absorption never changes them. Each object's hitstop afterwards is the largest value any contact or clash assigned it, as attacker or target, replacing its countdown.

Every battle object gets an authored contact priority: an integer, zero by default and after reset or reuse. An attacker contacts each target (landed, blocked or absorbed) once per hit activation; enabling its hit, even one already active, forgets that history, and a reset object is forgotten in both directions. A hit re-enabled while contacts are being applied forgets as usual; the frame's remaining contacts still belong to the old activation. Priorities and history are part of the battle snapshot, so a restore and identical inputs replay the same contacts.

Throws, hitgrabs, hits that reposition the target, dodge armor and lethal contacts stay out of scope and unchanged.

## Files to edit

You must only change the following files:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/BattleObject.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/BattleObject.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/ContactResolution.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/PlayerObject.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Objects/PlayerObject.cpp`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/NightSkyGameState.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/NightSkyGameState.cpp`
