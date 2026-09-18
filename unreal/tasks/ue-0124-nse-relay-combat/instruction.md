# Three-fighter relay combat

Deliver a playable three-fighter relay ruleset with team selection, HUD, training, rollback and replay. Ship sample teams with synchronized melee, projectiles and reserve follow-ups. Preserve ordinary movement, attacks, throws, blocking and hit reactions.

Teams operate independently, each with immutable slots 1–3, one main and surviving reserves. Outside relays only the main receives ordinary player commands; relay participants execute authored moves. A relay command selects an alive, off-screen reserve with zero cooldown while the main is grounded, actionable and not already in a relay; simultaneous requests prefer the lower eligible slot. Acceptance exposes the selected reserve at its main's position and facing, and spends 100 of a separate team relay resource, initially and maximally 200; it does not regenerate during a round. Rejection changes nothing and the HUD explains why. Held commands do not retrigger; requests outside their permitted phase are discarded.

A relay lasts 6 entry, 12 synchronized, up to 10 route-window, optionally 12 follow-up, and 6 exit gameplay frames. The acceptance frame is the first entry frame; handoff follows the sixth exit frame. Stage age, `ElapsedFrames`, is 0 on the frame a stage is entered, including the acceptance frame, and increases by one on each later gameplay frame. The main and selected reserve attack together at synchronized-phase entry. During the ten route-window frames, the first valid command may select either eligible reserve, including the participant if alive; acceptance ends the window that frame. The follow-up and its authored move start next frame, without extra cost; the target becomes main on termination. A nonparticipating target must be off-screen with zero cooldown. Without a route, retain the original main. Only one route is accepted; simultaneous requests prefer the lower slot. An enrolled fighter receives 120 cooldown frames when the sequence terminates, including interruptions; cooldown follows the fighter and decrements starting next frame.

Gameplay frames exclude super freeze. Ordinary hitstop, even affecting only one participant, leaves relay phases, command eligibility, cooldown, recovery and round clocks advancing; move playback retains ordinary hitstop. Team attacks use ordinary combat rules. Attribute each hit/projectile to its originating fighter and team even after handoff. Preserve the team's ongoing combo count and scaling across participants and handoff; changing attacker must not reset scaling. Nonlethal damage to an exposed reserve is fully recoverable; main damage uses the move's authored recoverable percentage. Clamp health plus recoverable health to maximum. An alive off-screen reserve recovers one health per gameplay frame consuming recoverable health; visible fighters never recover. KO sets both to zero permanently for that round.

Resolve contacts already due together, then KO, timeout, hit/throw interruption, and phase advancement or commands, with that precedence. An unblocked, unarmored hit or a successful throw on any participant cancels the relay (blocked or armored contacts and whiffs do not), preserves damage and hit/throw reactions, removes unexecuted attacks and its remaining projectiles, and refunds nothing. KO of a current relay participant also cancels that relay. KO of another exposed fighter does not cancel it unless the KO ends the match. Retain the original main if alive, otherwise promote the lowest surviving selection slot, bypassing cooldown. Remove other survivors from the stage after their hit/throw reactions finish. Surviving teams regain ordinary control without duplicate mains. Both teams eliminated together draw; otherwise elimination wins before timeout. Timeout compares summed current team health, with equality a draw. Match end cancels relays and locks combat. Super freeze suspends all relay, cooldown, recovery and round clocks and contacts until release; it does not cancel. A frame is frozen when either super-freeze counter is nonzero as the frame begins, before the engine decrements it, so a counter set to N freezes the next N frames. Queue command edges during freeze and evaluate them in arrival order on the first resumed frame; the queue retains every edge and has no capacity limit.

Show roles, health, recoverable health, cooldowns, route eligibility and team resource on the HUD. Keep all exposed fighters visible in the battle camera. Training supports lethal damage, keeps the round clock running and accepts live commands for both teams. Training restart restores selected teams, initial roles, health, the team resource to 200, and zero cooldown, without attacks or pending relay commands. Rematch does the same. Online peers and saved replays reproduce confirmed combat, roles, resources, timing and results through all phases and interruptions.

## Editable paths

You may add or modify files within these paths:

- `Source`
- `Config`
- `Content`
- `Plugins`
- `NightSkyEngine.uproject`

## Protected paths

Do not change these paths, including when nested within an editable path:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Fixtures/RelayFixture.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Fixtures/RelayFixture.cpp`
- `RELAY_INTERFACE.md`
