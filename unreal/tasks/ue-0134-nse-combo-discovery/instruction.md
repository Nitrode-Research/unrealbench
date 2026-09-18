NightSkyEngine has no combo lab; implement the one declared in ComboDiscovery.h. Discovery finds every combo route the declared moves allow from the current situation within the step and last-hit limits, prefixes included, returning the best as trials; validation replays a trial's tape. Both step the real battle and leave its gameplay state exactly as found, even after running out of budget or failing. Routes rank by highest total step damage, then fewer steps, earlier last hit, then per step lower move index, cancel before link. Every simulated update counts against the budget; an unfinished search says so and returns only valid routes in order; more budget never worsens any returned rank. Requests naming an unknown or repeated state are rejected.

A step presses its move's input on one frame, neutral otherwise, and the move starts that frame. Each move and kind takes its earliest start: a cancel after the previous move's hit while that move still runs, a link once player 1 is idle, judged at the frame's end under neutral input. The first step continues the action underway at the situation or links from idle. Every step lands its unblocked hits that continue the combo: the opponent has been unable to act since the latest earlier hit, even one landed before the situation; regaining control on the hit frame is an escape. A first step with no earlier hit is exempt. A step's HitFrame is its first contact; its damage is the sum of its contacts.

Validation replays the tape, lets attacks it began resolve, and completes only if player 1's contacts are exactly the trial's steps in order, each an unblocked, combo-continuing hit by that step's state removing that step's damage; other recorded fields are ignored. A hit by the action underway at the situation is an earlier hit, not a step.

Scope: health-removing moves on single-frame button inputs, an unstunned player 1, an opponent whose only escape is the supplied input; no projectiles or assists.

## Files to edit

You must only change the following files:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Lab/ComboDiscovery.h`
- `Plugins/NightSkyEngine/Source/NightSkyEngine/Battle/Lab/ComboDiscovery.cpp`
