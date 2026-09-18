# Swept projectile contacts without tunneling

Add optional swept projectile contacts to NightSkyEngine. Projectiles with sweep disabled retain ordinary endpoint collision. Enabled projectiles contact eligible targets crossed by linear movement of either participant during one battle frame. Support multiple fixed hitboxes/hurtboxes, fixed facing and independently oriented rectangles using the engine's integer collision geometry. Before offset, facing and rotation transforms, positive dimensions use truncated integer half-sizes; collapsed boxes count as closed segments or points. World vertices are within ±10,000,000 units. Shape changes, angular or curved motion, and swept clashes are outside scope.

Provide gameplay controls for enabling sweep, piercing, a positive integer lifetime contact limit, distinct nonnegative integer target ordering keys stable for their activation, and teleportation distinct from continuous movement. Contacts include initial overlap, touching and the final endpoint. Each target's earliest box-pair contact determines its order; exact ties use ascending target key. Unequal times remain distinct.

Non-piercing accepts one target; piercing accepts targets in order up to its lifetime limit. Exhaustion leaves the projectile inactive and its pooled slot available for reuse by the end of the current battle frame. Each target counts once per projectile activation across frames, hitstop and hurtbox changes. Guarded and armored contacts count; invulnerable, friendly, off-screen, inactive and otherwise ordinarily excluded targets do not consume or obstruct contacts. Eligibility is evaluated at each contact. Accepted contacts retain ordinary damage, chip, guard, armor, callbacks and hitstop, without duplicate endpoint hits. Hitstop alone permits remaining contacts that frame; explicit attack disable or deactivation cancels them. Impact-position relocation is not required.

A target activation lasts from activation until deactivation. Reactivating a target starts a new activation eligible for a later contact from a still-active projectile. Frames, hitstop, teleportation and hurtbox changes do not create a new activation. Ordering keys determine ties only and do not define target identity.

A participant spawned or teleported during a frame uses endpoint-only contact for that entire frame, ordered at time one under the same rules. Subsequent motion starts at its new position. Objects deactivated before collision cannot participate. Reusing a projectile starts a fresh activation with full capacity and no prior targets or travel.

Battle save/restore followed by identical inputs reproduces contact order, health, hitstop and projectile lifetime. A changed continuation produces its own contacts without effects retained from the discarded future.

The solution must also be directly playable. Opening its project and pressing Play, or launching a packaged Development build, must enter a ready-to-play local training scene with a controllable fighter and an opposing target, without console commands, Python scripts, or manual asset setup. Display "F - Fire swept projectile" on screen. One press of F fires one projectile in the fighter's facing direction; holding F does not repeatedly fire, and releasing then pressing again permits another shot. Provide R to reset the training scene for repeated playtests.

Every fired shot must immediately produce a clearly visible muzzle flash and a bright projectile or travel trail in the game viewport, including shots that miss. A trail or afterimage must remain visible for at least 0.2 seconds so that a fast projectile cannot disappear between rendered frames. Accepted contacts must produce a distinct impact flash and observable target health reduction or an explicit blocked/armored response. Keep the scene readable and the controls visible at normal play resolution; logs, collision debug lines, and counters alone do not satisfy the visual requirement.

The F-bound shot must enable and exercise the swept projectile implementation through the normal battle update and contact/damage pipeline, not a separate hitscan or cosmetic-only simulation. Include a reproducible high-speed shot whose endpoints do not overlap the target but whose path crosses it. Keep firing gameplay in C++ and retain the default endpoint behavior for projectiles that do not opt in. Add Automation coverage for firing on a press, no repeat while held, firing again after release, real swept damage, and reset/reuse. Verify the visible shot and impact feedback in a rendered playtest; headless collision tests alone do not establish visual acceptance.

## Editable paths

You may add or modify files within these paths:

- `Source`
- `Config`
- `Content`
- `Plugins`
- `NightSkyEngine.uproject`

## Protected paths

Do not change these paths, including when nested within an editable path:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Fixtures`
- `PUBLIC_INTERFACE.md`
