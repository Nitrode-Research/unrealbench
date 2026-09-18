# Facility escape — GDD

## The game

The player is trapped in a powered facility. Escaping means finding tools and learning how its machinery fits together: cut a circuit to disable security, start the backup generator to reach an unpowered area, or flood the plant to change the available route. These actions have consequences. Noise raises the alarm, water can become lethal, and a power change can move a lift or seal a door.

There are two working exits: **MainGate** and **ServiceTunnel**. The player must reach an exit, win, and be able to start a fresh attempt. The loading-dock exit, neutral-character AI and unfinished story routes are outside this task.

## What to implement

Implement both the facility simulation and the player systems in the supplied native C++ architecture. Headers, component layouts, Blueprint APIs, maps, assets and settings are already provided. Keep them intact and work in the implementation files listed at the end. The Shooter and Horror template examples are not additional games to implement.

| System | Responsibility |
| --- | --- |
| Facility rules | Authoritative power, alarm, machinery, inventory and escape state |
| World subsystem | State lifetime, registration, reset snapshots, clock and notifications |
| Devices | Ask the shared rules what is possible, then update movement, visibility and prompts |
| Player components | Focus, interaction, climbing, stealth and character input |
| Game flow | Detect the player at a valid exit, show victory and reset the attempt |

Use the same live state throughout. For example, a generator starting must affect a door's availability, a lamp's visibility and a hazard's safety without each actor maintaining a separate interpretation of power.

The following sections describe the gameplay and its implementation details. Prompt examples specify meaning and enabled state; equivalent English is fine unless a stable enum label is explicitly required. Keep useful diagnostics for invalid setup and important transitions. IDs, timings and placements are configurable, so do not hardcode a test world's values.

## Facility simulation

### State and action conventions

#### Action results

Predicates read without inserting entries or modifying state. Mutation booleans report an effective visible state change, not necessarily success of the player's desired action. For example, an under-level hack can return true because it raised the alarm without opening or rerouting anything; the same failed hack at Lockdown returns false but still resets the quiet counter. Bookkeeping-only quiet/flood count changes are not visible changes. Repeated completed actions normally return false.

#### Initial state

SetInitialState establishes DOORS and SECURITY Live, PLANT Cut, Quiet alarm, zero quiet/generator/flood counters, and clears generator-overloaded, flooded, coolant-valve-open and rack-overloaded flags. It preserves configuration, device routing, registration maps, inventory, escape state, flood fan identity and routing metadata. It is a scalar initializer, not a complete attempt reset: the world subsystem owns registration-time snapshots and whole-attempt reset.

#### Registering gameplay objects

Registration is first-wins: reject empty IDs and duplicates for lifts, doors, cameras, crates and pickups; pickup items cannot be None. Hack targets need a positive level and do not replace an existing target. Flood registration accepts the first nonempty draining-fan ID without changing water. Exits reject None and duplicates. Preserve authored placement/configuration, except that door/lift registration immediately settles against known power and alarm state. Registering the device circuit later must produce the same settlement.

#### Missing IDs and defaults

Unknown circuits read Cut; unknown device routing reads None and never provides power. Missing lifts read Bottom/CargoLift/unhacked, pickups read None/taken, doors read closed/unlocked/mechanical, cameras see nothing, crates read InPlant/not dropped/not jamming, and exits have no record. Do not introduce blanket unknown-ID rejection beyond the existing API contract: SetDoorOpen can create a nonempty unknown door even when setting it closed, and ToggleDoor can open an unknown mechanical door. SetLiftStop cannot create a lift. Assigning an implicit/current circuit value is a no-op, including unknown-device-to-None and unknown-circuit-to-Cut.

### Breakers, effective power and settlement

#### Breaker position and effective power

A breaker is on exactly when its non-None circuit is Live. Effective power is different: Live carries power, Cut also carries it while the generator runs, and Shorted or None never does. Devices follow their routed circuit; fans and lights follow effective device power.

#### Breaker limits

The panel allows two of its three breakers on. Flipping an on breaker cuts it. Flipping a cut breaker turns it on unless two others are on, in which case all currently Live breakers trip to Cut and the requested breaker remains cut. Shorted breakers refuse flips and remain shorted through trips/overload. Low-level SetCircuitState can explicitly change a state; the player's panel cannot repair a short.

#### Door and lift responses to power

Every circuit, routing, alarm or generator change that affects power must settle doors and elevators synchronously. Standard doors are not automatically moved. A lockdown door on a shorted circuit fails open even during Lockdown; otherwise a dead circuit seals it, and Lockdown seals it on powered circuits. A gate seals during Lockdown regardless of circuit and never fails open. Releasing a seal does not automatically reopen a closed door. Forced doors stay open. A registered elevator parks at Bottom when its known circuit loses effective power or Lockdown is called, including a hacked elevator; cargo lifts do not auto-park. A door/lift awaiting device registration has no known circuit to settle against until the circuit arrives.

#### Backup generator

The backup generator starts only while stopped and not overloaded, with max(1, configured duration) steps, and raises alarm one level. It powers cut circuits without changing breaker positions. Natural expiry permits restart. Overload requires a running generator and the pry bar, permanently disables that generator for the attempt, zeroes remaining steps and cuts all Live breakers without repairing shorts; breakers remain usable afterward.

### Access and handheld capability

#### Items and handheld upgrades

Inventory is a set of non-None item types. Giving a duplicate item is no change. Taking either of two pickups containing the same item empties each pickup independently, without duplicating inventory. A taken pickup remains taken if the item later leaves inventory. Keycard level is the highest held card (2 or 3); noncards contribute zero. The handheld uses configured base level, upgraded by the held server drive to max(base, configured drive level), never downgraded. A hack target level must be positive to succeed.

#### Opening and forcing doors

A closed door with a positive card lock and/or key item opens to either sufficient card or that key. Open or corroded-lock doors need no credentials. Mechanical doors do not need power. Door obstacle priority is Forced, Sealed, FailedOpen, NoPower, Locked, then None. Toggle respects obstacles; closing needs no credential and is quiet. A pry bar may force a closed nongate door held by a seal, dead circuit or lock, but not an already freely openable door. Mark it forced/open before raising alarm so the resulting Lockdown cannot close it. Nothing, including low-level SetDoorOpen, closes a forced door.

#### Door and gate hacking

Door hacking requires a known nongate door, positive hack level, power and exactly a Locked obstacle; it cannot bypass seals or dead power. Insufficient capability raises alarm without opening. A successful hack opens the door. The gate itself is never hackable or forceable; a powered positive-level controller hacks the unique gate only when its sole obstacle is Locked. Opening the gate by any permitted opening path while Alerted or above raises alarm. Opening while Alerted therefore returns a visible event even though the resulting Lockdown immediately reseals it.

#### Lift calls and hacks

Lifts answer calls with obstacle priority Hacked, NoPower, then Lockdown for elevators only. Same-stop calls report no movement. Hacking needs a known powered positive-level unhacked lift; a successful hack freezes calls at its current stop, while an insufficient hack raises alarm. Being hacked takes obstacle priority over later power loss/Lockdown, but it does not suppress the automatic elevator parking rule.

#### Camera availability

A camera watches only when registered, unlooped, powered, its named light zone is on, and the server drive is not held. Sightings raise alarm only while watching. A powered positive-level unlooped camera can still be hacked in darkness or while the drive blinds it. Success loops it for the attempt; failure raises alarm. Removing the drive allows watching again if all other conditions permit, but does not undo a loop.

### Routing and water

#### Routing panel setup

Only one panel is registered. A route job needs a nonempty device and non-None target circuit; pump jobs normalize their device/circuit to None so equivalent pump registrations deduplicate. Duplicate jobs and a second panel ID are refused. A usable panel must be that registered nonempty panel, powered and have a positive hack level. Route attempts additionally need a registered job and known target device. A target already on that circuit is actionable only if it is the panel's current patch, in which case the switch undoes it.

#### Moving and undoing a patch

The panel supports one patch at a time. Remember the patched device's original home. Routing another device returns the prior one home first; selecting a patched device's current destination returns it home and clears patch metadata. Routing a patch to another offered circuit preserves its original home. Apply routing through the same immediate settlement rules. An under-level routing/pump attempt raises alarm without performing its intended change. Pumping requires an offered pump job and a dry Plant floor, floods quietly without opening the valve, and becomes available again after drainage.

#### Water and the coolant valve

Flooding a dry floor starts at zero wet steps; reflooding standing water is a no-op and never resets its age. Water is lethal while any breaker circuit carries effective power, including backup power. The pry bar opens the closed seized valve and floods quietly. Closing the valve takes no item and leaves existing water.

#### Drainage, shorts and corrosion

On a wet step, running named fans drain first if the valve is closed: clear water/count and perform no shorting or corrosion that step. Otherwise increment wet age; at the configured short threshold, every circuit carrying effective power becomes Shorted, including generator-backed Cut circuits, and settle doors/lifts. At the configured corrosion threshold, mark only the door named by the registered service-tunnel exit as lock-corroded. Missing tunnel/door means nothing to corrode. Corrosion does not itself open that door or bypass power/seals. Shorts and corrosion persist after drainage. A zero/nonpositive threshold is reached on the first standing-water step.

#### Server rack overload

A powered rack can be overloaded once with the pry bar. It stays burning/smoking, floods Plant via sprinklers, raises a Quiet alarm to Alerted but never lowers a higher alarm, and resets the quiet counter even at Lockdown. Power loss does not extinguish it.

### Crates, escape and the clock

#### Crate routes

A crate starts in its authored position. It can move off the shutter to AtShaftHead when it is jamming and either the pry bar is held or its named lift answers calls; movement is quiet. A crate there can drop only with a known car at Bottom, regardless of lift power, marking InPlant/dropped and raising alarm. Alternatively, a successful lift call to Bottom carries all shaft-head crates naming that lift into Plant without marking dropped or making noise. Repeated moves/drops at the terminal position do nothing. Any still-jamming crate keeps the shutter jammed.

#### Exit conditions

Escape is once-only. MainGate requires the unique gate door open and needs no separate exit registration. ServiceTunnel requires its registered door open and its named fans stopped. None is never valid. LoadingDock escape is deliberately unavailable in this source version, even after crates move; do not implement an unfinished shutter/winch feature. Once escaped, every further escape is refused.

#### Alarm escalation and decay

Alarm levels run Quiet → Alerted → Lockdown. Setting None or the current level is refused; setting a different level resets quiet age and settles machinery. Noise resets quiet age even at the ceiling, where no level changes; lowering stops at Quiet. A powered alarm console lowers one level, but an unpowered console cannot. Alarm state/decay does not depend on SECURITY power.

#### Simulation step order

AdvanceStep order is essential: (1) decrement the generator and settle on expiry, (2) process drainage/standing flood/shorts/corrosion using the resulting power, (3) count quiet time and lower the raised alarm one level at its configured threshold. Generator countdown itself is visible; flood/quiet age alone is not. At Quiet, clear quiet bookkeeping without reporting it as a change. Nonpositive alarm decay means the next step lowers one level. Returning true must combine all visible changes from the step, not overwrite an earlier change. All state and hypothetical branches must remain independent; no static/global mutable facility state.

## Devices and world state

### State, registration, and lifecycle

#### Subsystem lifetime

Initialize the live and reset states with DOORS and SECURITY live, PLANT cut, quiet alarm, and the rule defaults. Capture the clock interval and scalar settings at initialization; use per-ID settings when registering locks, pickups, and hack targets. Support Game and PIE worlds. Start a repeating clock only for positive StepSeconds when play begins; clear it on deinitialization. On the next tick report configured doors, pickups, and hack targets not claimed by a corresponding registration. Retain useful state-change, failure, and configuration diagnostics without requiring exact log wording.

#### Live state and reset snapshots

Registration seeds both live and reset state through the existing acceptance and first-registration rules, without notifications or overwriting progress. Retain all subsystem public registration/query/action surfaces, including related camera, exit, crate, valve, server, and inventory operations used by the rules. Registering again must preserve open/forced doors, collected pickups, moved/hacked lifts, changed circuits, and routing patches. The reset snapshot retains each initial configuration.

#### Actor lookup

Null actors and empty device IDs do not enter actor lookup. Keep one entry per actor per ID, preserve registration order, update a repeat actor's sharing flag, and remove expired entries during registration. Return the first live actor. Shared-ID actors remain discoverable individually; conflicting nonsharing registrations log the conflict but remain retained. Warn for a differing shared initial circuit and preserve the first circuit. Unregistering removes only that actor, not facility state.

#### Forwarding actions

Actions and queries preserve existing rule semantics, including Boolean action results meaning an effective reported change, not necessarily the desired outcome. An under-level hack can fail to unlock/reroute yet return true because it raised the alarm. Lockdown noise can return false while restarting the quiet counter. Do not impose additional enum/unknown-ID rejection beyond the facility rules.

#### State notifications and reset

For a reported ordinary change, update state and then send exactly one general event. If an action changed alarm level, send its old/new alarm event after the general event. No-change actions send no general event. Reset returns the complete state to its registration-time values without clearing live actor lookup, always sends a general event, and sends an alarm event only if its level changed. IsResetting is true throughout reset broadcasts and false afterward. Update actors' remembered edge values before callbacks, so synchronous nested notifications compare against the current observation.

### Power and routing

#### Breaker switches

Breaker interactions flip their configured circuit through the subsystem; subscribe at BeginPlay and unsubscribe at EndPlay. The switch reflects breaker position, not generator-backed effective power. Notify its Blueprint switch event instantly at startup and afterward only on a position change, including non-instant reset changes. Refresh prompts on every notification: disabled `Not wired to a breaker`, disabled `{Circuit} shorted`, otherwise enabled `Turn {Circuit} on/off` as appropriate. Retain no-world defaults: position off and a configured switch's turn-on prompt, but no effective press.

#### Generator actor

The generator has no device ID, consuming circuit, or independent timer. Use overloads when eligible and otherwise starts; all commands, eligibility, running/overloaded state, and remaining steps come from the subsystem. Without one return false/zero. Startup and every reset report both states instantly, even unchanged. Ordinary changes report only changed states, with running native then Blueprint hooks before overloaded native then Blueprint hooks. Countdown alone must refresh the prompt without repeating state edges. Display priority is disabled `Overloaded`, enabled `Rig to overload (blackout)` when possible, disabled `Running, {N} steps left`, otherwise `Start generator (loud)` enabled exactly when eligible.

#### Routing controls

Routing switches register their configured jobs before base device initialization and share their panel ID. A route option hacks its target/circuit; a pump option hacks the pump. All eligibility and results come from the subsystem, including insufficient-level noisy failures. The panel supports one patch: another device displaces the previous device back home; selecting the current patch again returns it home; moving an already-home unpatched device to that same circuit does nothing. Panel registration, positive hack level, power, job/target validity, and standing-water pump gates remain the facility rules' responsibility. A drained floor can be pumped again.

Routing IsOn compares a nonempty target's current circuit to its configured circuit, even None; pump IsOn follows flooded state. Deliver instant switch notifications at startup and every reset, otherwise changed edges only, including displacement by another option. Preserve inherited hack-event dispatch. Ordinary interaction is disabled; show routing/patch state or pump state, invalid configuration, and unknown targets. The hack line distinguishes no target/level, no power, capable versus under-level attempts, routing to a circuit, undoing to home, and pumping. Use existing circuit/item names and display setters so listeners receive actual changes. Without a subsystem routing is unavailable with empty prompts.

### Access and transport

#### Door initialization and actions

Doors register kind/initial openness and capture the authored closed panel pose before base BeginPlay; pickups register their configured item ID before base BeginPlay. Initial reconciliation snaps to retained live state. Door Use forces only when the rules permit forcing; Toggle never forces; inherited hack input reaches Hack. Preserve action results even for unsuccessful noisy attempts. Missing-facility door actions/capabilities are false, open falls back to initially-open, obstacle is None, lock level zero, and key None.

#### Door prompts

Door prompts prioritize available pry (`Pry open {name} (loud)`) then the supplied obstacle order: `Pried open`, `Sealed`, `Failed open`, circuit-off description, or lock requirement. Lock text permits the configured card, item, or either alternative. Otherwise show enabled `Close {name}` or `Open {name}`, appending loud before lock-corroded status when applicable. Gate lockdown/resealing and no-pry/no-direct-hack restrictions follow the rules. The separate hack line enables under-level attempts with a loud-failure warning, describes a capable hack, shows disabled no-power when appropriate, and otherwise clears for absent/nonpositive/ineligible targets.

#### Door animation

Door motion changes only panel relative location: authored closed pose plus local OpenOffset when open. Move at constant SlideSpeed, continue through unrelated/same-target notifications, reverse from the current pose, and tick only while moving. Clear moving/tick before native then Blueprint settled callbacks, once per completed movement. Startup/reset snaps cancel motion without settled callbacks.

#### Pickup actors

Pickups grant the configured inventory item once and preserve taken state through actor recreation. Untaken actors are visible/collidable with enabled `Take {item}`; taken or unconfigured/invalid pickups in a facility are hidden/noncollidable with disabled `Taken`. No-world answers are false/None. Preserve canonical item names. Set visibility/collision before native then Blueprint taken-change hooks. Ordinary notifications are changed-only; startup and every reset force instant reconciliation on the same actor. Nested callbacks must not duplicate the taken edge. Ordinary Use/Take interactions remain externally invoked; do not add automatic bindings absent from the actor base.

#### Lifts and call panels

Lifts register initial stop/kind before base routing and snap to retained state afterward. Construction previews the initial stop. Calls and hacks are authoritative immediately; a call panel compares the logical stop even while the car moves. Same-stop or refused calls return false. Panels have no independent device routing. Preserve hacked-before-no-power-before-elevator-lockdown refusal order: elevators park at bottom on power loss/lockdown; cargo lifts retain their logical stop and can operate under lockdown if powered. Obstacles gate new calls; they do not freeze an accepted trip whose destination is unchanged. Calls can redirect a moving car.

Move only the car mesh at constant TravelSpeed toward the current world-space stop. Tick only during travel; arrival disables motion/tick before native then Blueprint arrival hooks. Startup, construction, and reset snaps emit no arrivals. Prompts distinguish ride direction, `Lift is here`, `Call lift`, hacked, circuit-off, and lockdown states; hack prompts distinguish sufficient/insufficient level, absent or already-used targets, and no power. Without a subsystem calls/hacks/availability/panel presence are false, a lift's stop falls back to its initial stop, and its obstacle is NoPower.

### Hazards and coupled time steps

#### Fans

Fan running and flood lethality are live subsystem answers, not animation-derived. Fan BeginPlay captures authored hub rotation, binds overlap, performs base registration and instant reconciliation; EndPlay removes overlap and base subscriptions. Full-range speed ramps use MaxSpinSpeed/SpinUpTime or /SpinDownTime, continue from current speed on reversal, and select target on tick for nonpositive ramp time. Integrate angle from resulting speed about authored local Z; reset snaps speed without rewinding angle. Stop ticking once a stopped fan finishes coasting. Running callbacks are native then Blueprint before an ordinary activation's existing-occupant sweep. Display is noninteractive; the fan lamp is on while its routed room-light zone is dark/absent.

#### Flood surface and events

Flood registers its draining-fan ID, binds/unbinds state and overlap events, and reflects the rules' flooded/lethal answers. Construction previews flooded height; startup/reset snaps to current logical target. Move surface from its present height between +FloodHeight and -DrainedDepth at FloodHeight/RiseSeconds, so the default -5 to +40 journey takes nine seconds, not eight. Reverse without jumping; same targets preserve travel. Nonpositive rise time snaps. Disable motion/tick before one settled event on interpolated arrival; snaps do not settle. Logical flooded notification precedes lethal notification after reconciling the target; ordinary unchanged notifications do not repeat edges, while startup/reset force instant notifications.

#### Water mesh and kill volume

Hang the kill volume one unscaled half-height beneath the surface. Water hides with no mesh or at/below floor. Above floor preserve authored XY transform; solid-mesh Z scaling/placement spans floor to surface regardless of pivot, while a mesh at most 1 cm tall retains scale and moves to surface Z. Missing fan IDs/mesh should be diagnosable.

#### Hazard damage

Both hazards apply configured engine damage to valid non-destroying pawns with themselves as causer and no controller instigator. Entry callbacks consult live safety, including a running fan at zero speed and harmless coasting blades. Ordinary safe-to-dangerous changes also sweep already-overlapping pawns after state hooks. Recheck current safety before each victim because damage can reset the facility; ignore nonpawns and invalid victims. Instant startup/reset skips the explicit occupant sweep, but ordinary overlap callbacks caused by movement still use current rules.

#### Shared clock

Manual and timer steps use the same supplied ordering: generator countdown/expiry, flood drainage or shorting/corrosion, then alarm decay. Generator decrements are visible changes; quiet/flood-age bookkeeping alone may change internal state without notifications. Preserve configured thresholds. Draining fans cannot drain an open coolant valve; a standing flood can short live circuits, fail lockdown doors open, stop fans/park elevators, and corrode the registered service-tunnel lock. Generator-backed cut circuits carry power; shorted circuits do not. Reset must return all of these systems to their initial state together.

#### Console commands

Implement `Facility.GiveItem <enum-name>`, `Facility.SetAlarmLevel <display-or-enum-name>` (case-insensitive alarm names), `Facility.AdvanceStep [count]`, and `Facility.FloodPlant`. Missing/invalid item or alarm arguments, unavailable world/subsystem, and step counts below one report usage. AdvanceStep uses one step unless exactly one count argument is present. Do not hard-code fixture IDs or tuning values: native test worlds use configurable IDs, settings, placement, and action sequences.

## Player interaction and security

### Object discovery and presentation

#### Finding and focusing objects

Periodically discover every valid interactable in the configured sphere around the owner. Highlight all in-range objects, including off-axis and occluded objects. Focus only the in-range object hit first by the camera-forward Visibility trace; fall back to owner eye viewpoint without a camera. A visibility blocker removes focus, not proximity highlights. Route Interact and Hack only to valid focus and identify the acting owner. No focus, stopped detection or a destroyed target is a safe no-op. Direct component action dispatch still publishes the actor even when the display marks the action unavailable; the owner enforces its gameplay rule.

#### Interaction lifecycle

Notify focus changes, detection/loss and HUD display changes only for effective transitions. Repeated detection startup is idempotent; starting runs an immediate scan. Stop, range exit, shutdown and destruction must clear stale focus, overlays, timers and delegate subscriptions without repeated loss notifications. Display and hack-display setters publish only effective changes.

#### Highlight overlays

Highlight every directly owned mesh except NoHighlight-tagged meshes, remembering each mesh's distinct prior overlay and putting it back exactly. Replacing a live highlight must not replace that saved prior value. Disabled highlighting prevents new overlays and cleans up an active one. Refresh in-range overlay availability colors immediately when display data changes, without waiting for another scan.

### Ladder and hatch traversal

#### Joining a ladder

Start climbing only for a character with a non-null ladder and no active climb. Cache the ladder's world bottom/top, facing toward the climber, stand-off, exit distance and scaled capsule half-height. Join at the closest clamped feet distance; a degenerate path uses world up. Request uncrouching, stop movement, select Flying, ignore this ladder's collision and place the capsule unswept at the joined point plus stand-off and half-height. Face controller yaw toward the rungs, preserving pitch/roll. Clear pending input, enable ticking, and notify after state is ready.

#### Climbing and leaving the ladder

Accumulate input, clamp its total to [-1,1] per tick, consume it once, and move at ClimbSpeed (150 cm/s by default). Nearly zero input does not move. Sweep ordinary travel against collision and use actual reached distance, so a closed hatch blocks progress. Exact endpoints stay attached. Crossing below bottom lets go in place. Crossing above top sweeps first 5 cm upward then across to the far-side exit. If either move blocks, return to top capsule position and stay attached; otherwise end at the exit. Stop, jump routing and loss of ladder clear pending input, ticking, references and collision ignore, switch to Falling, then notify. Inactive stop is silent.

#### Ladder ownership and cleanup

Ladder Use rejects null/unsupported users and users climbing another ladder; on this same ladder it lets go. A successful start tracks that climb component and shows enabled Let go; otherwise show enabled Climb. Tracking is not exclusive: another eligible user can replace the tracked user without ending the first climb. Unbind the previous listener before replacement. Old-user endings cannot clear a new user's tracking. Ladder EndPlay ends its tracked climb and clears its listener; untracked users detect ladder destruction on their next climb tick. Direct component starts do not themselves register ladder tracking.

#### Hatches

Register the hatch door before initial display refresh, preserving existing state on actor recreation. Mechanical hatches open without power; configured locks still use facility rules. Toggle/CanOpen return subsystem answers, or false without it; IsOpen falls back to configured initial state without a subsystem. Display enabled Open hatch / Close hatch, or disabled Locked. Logical state changes immediately, before the lid finishes moving. Preserve the authored closed hinge rotation and apply OpenDelta in its local frame. Swing at constant angular speed, redirecting mid-swing from current pose. Repeated target requests do not restart motion. Arrival disables ticking/motion before native then Blueprint settled callbacks, once each. Startup and facility reset snap silently without settled callbacks.

### Player stealth

#### Stealth states

Compute live summary priority Hidden > Dark > Sneaking > Exposed. A valid hiding spot grants Hidden if its optional crouch requirement is satisfied. Otherwise no valid lit overlapping zone means Dark, including no zones at all. Any lit overlap wins over dark zones. In lit open space, crouching or horizontal speed at or below SneakSpeed (250 cm/s inclusive) means Sneaking. Other cases are Exposed; only Exposed is detectable. Individual predicates remain independent of summary priority. Read character crouching and horizontal velocity live; non-character crouch is false, and speed without an owner is zero. Light zones query current facility lighting using LightsId, never cached power; no subsystem means unlit.

Zone id is the first lit zone in tracked overlap order, otherwise the first valid non-None zone id, otherwise None. Track initial overlaps at BeginPlay and subsequent owner overlap events without duplicate memberships. Null/unrelated actors do nothing. Destroyed zones/cover stop influencing queries even before an end event. Initial summary establishes a baseline without a change event. Tick and overlap events reconcile; notify old/new values only when summary changes, recording the new baseline before listeners run so reentrant queries/ticks are safe. Remove owner subscriptions at EndPlay. Queries reflect current conditions before the next tick.

#### Light zones and hiding spots

Preserve roots/boxes: light-zone half-extents (500,500,200), hiding-spot (50,50,100), yellow/purple visualization respectively. Query-only WorldDynamic boxes overlap Pawn and ignore other channels, generate overlaps, do not affect navigation, and need no actor tick. Hiding does not require crouching by default. State descriptions are Hidden, In the dark, Sneaking, Exposed; unknown enum values use Exposed.

### Surveillance and security overrides

#### Camera sightings

Register the camera's LightsId before base BeginPlay. Watching requires a powered, registered unlooped camera, powered associated lights and an unpulled server drive. Player-controller pawns entering the View overlap box cause one sighting per entry; ignore unpossessed/AI pawns and non-pawns. Remaining inside does not repeat sightings. Ordinary watching transitions notify native then Blueprint hooks with instant=false only on change, recording the new state before callbacks. Notify before reporting at most one already-inside player when watching resumes. Startup/reset always publish instant=true without reporting an already-inside player. Bind entry handling for play and remove it at EndPlay. Missing facility gives false queries/actions.

#### Camera movement

Capture authored housing rotation; pan about mount up while preserving tilt. Start at StartYaw heading toward MaxYaw, even for reversed endpoints. Use PanDirection times nonnegative TurnSpeed, clamp at an endpoint, discard overshoot and reverse next tick. Powered motion continues even looped, dark or blinded. Unpowered freezes; power returning resumes. Disable tick for nonpositive speed or a nearly zero interval. Reset returns to the starting pose and direction.

#### Security hacks

Hack dispatch from the interactable flows through the existing base hook into actor Hack. CanHack means eligible attempt, not guaranteed success. Camera hacking needs power, positive level and unlooped registration, but does not independently require lights or an unpulled drive. Sufficient handheld level loops silently; insufficient level raises alarm if possible. A controller can hack only a powered, locked gate found by door kind; it does not bypass missing/open/sealed/unpowered/unlocked states. Sufficient level opens through facility rules, including configured opening noise. Insufficient level leaves it closed and raises alarm. Return values indicate state change: a loud failed bypass can return true; ineffective attempts return false.

#### Security prompts

Camera interaction is never actionable: show watching, looped, blinded, circuit-off, or in-the-dark status in that priority. Eligible hack text says whether it loops or will fail loudly. Hide absent/nonpositive/ineligible hack lines except disabled no-power explanation. Controller interaction likewise displays no gate, open, sealed, unpowered, locked or unlocked state; eligible hack text explains opening or loud failure. Circuit explanation is Not wired for None, otherwise {circuit name} is off.

#### Alarm console

A powered alarm console lowers a nonquiet alarm exactly one level per accepted Silence, never all the way automatically. Quiet or unpowered refuses. Cutting SECURITY does not itself silence alarm. Display actionable Silence alarm or disabled Alarm is quiet / circuit-off explanation. The caller invokes Silence; automatic interaction Blueprint wiring and console hack effects are not required.

#### Camera and stealth policies

The camera deliberately does not consult player stealth or trace line of sight; keep hiding spots outside its View when authoring a level. Shared lighting affects both systems, but their detection policies remain distinct. Do not invent stealth immunity to camera volume sightings.

### Dock crate and attempt lifecycle

#### Crate actions

Register CrateId/LiftId before base refresh, preserving existing state on recreation. Delegate Move, Drop and Use to facility rules. From JammingShutter, pry bar or usable lift permits a quiet move to AtShaftHead. Drop from there requires its lift car at bottom, not lift power: change to InPlant, record dropped and raise alarm one capped level. Failed/repeated actions have no repeated effects. Use drops if permitted, otherwise moves. A usable lift called down quietly transports its shaft-head crate without dropped flag or damage. Queries report logical state during animation.

#### Crate prompts and defaults

Prompts distinguish blocked/movable shutter, loud drop, crate on lift, dropped or quietly moved InPlant; only permitted actions are enabled. With no facility position falls back to JammingShutter and action/capability/dropped queries are false. With facility but unknown id retain the subsystem's InPlant answer and false capabilities.

#### Crate animation and events

Body alone slides between authored world poses Root, ShaftHead and Landing. Startup snaps to actual state, publishes native then Blueprint position hook with instant=true, dropped=false, and does not replay historical damage. Every reset snaps, cancels motion/ticking and publishes the instant position hook even unchanged. Ordinary position changes publish immediately (native then Blueprint), before visual arrival, with instant=false and dropped only on a new drop edge. Record position/drop state before any reaction; reconcile motion before hooks and hooks before damage. Unrelated or nested unchanged notifications do not replay transitions or damage.

Use constant MoveSpeed (200 cm/s) or DropSpeed (1500 cm/s) from current world position, read the destination marker live each tick, and do not restart same-target motion or change its selected speed. Arrival clears motion/ticking and emits one Blueprint settled event. Instant snaps emit no settled event.

#### Crate damage

A new non-reset drop immediately damages every valid, non-destroying pawn currently overlapping KillVolume through engine damage, using KillDamage (100000), this crate as causer and no instigator controller. Physical landing is not awaited. Quiet transport, repeats, unrelated notifications, startup/reset and later entrants do not initiate a damage sweep. Victim death behavior belongs to the victim. The reference does not promise cancellation of an already-started sweep when a victim resets state.

Retain meaningful diagnostic logging for important transitions and invalid setup. The loading-dock shutter and route-search oracle remain outside this implemented native target. The facility reset owns world state; actor/component runtime cleanup must remain consistent with its notifications without introducing a new global state machine.

## Connecting the game

#### Shared actor lifecycle

Actor base BeginPlay binds focus/hack delegates and the shared-state notification; EndPlay removes them. State notification calls pre-state, native state, Blueprint state, then display refresh. Focus refreshes display only when focused and forwards native/Blueprint notifications. Hack dispatch forwards native/Blueprint hooks. Device base registers before actor-base BeginPlay/initial display, reports initial effective power afterward, and unregisters only its actor on EndPlay. Reconcile power before subclass state handling, recording the new value before callbacks so reentry is safe. Player-only volumes mean a pawn controlled by a player controller, not just any pawn, an AI pawn or an unpossessed pawn.

#### Lights

Light fixtures read effective shared lighting and set visibility on every light component owned by the actor, including Blueprint-added lights. Startup/reset publishes an instant state; ordinary changes publish only edges, with remembered state updated before callbacks. They are not interactable actions.

#### Valve and rack actors

Coolant valve Use opens when closed, closes when open. Opening requires the pry bar, floods Plant and prevents drainage while open; closing leaves existing water until the rules drain it. Server rack requires power and pry bar to overload; overload is once-only, creates persistent fire/smoke, floods Plant and raises the alarm to at least Alerted without lowering Lockdown. Both actor families delegate every query and action, refresh availability/status, emit instant startup/reset state and only changed ordinary edges, and record state before callbacks. No rack hack effect is implemented or requested.

#### Character input

The character retains its Enhanced Input bindings and original component layout. Move/look inputs delegate their two axes; normal movement uses controller-relative actor forward/right, while climbing sends ONLY forward input to its climb component. Jump during climb releases instead of jumping; outside climb it uses normal jump. Interact/Hack dispatch to the actual focused interactor. Crouch hold is remembered while airborne but does not initiate crouch until landing; no crouch starts while climbing. Release clears hold and requests uncrouch. The camera moves smoothly toward the authored default or crouched offset with its configured interpolation.

#### Victory and a new attempt

Exit volumes register their enum/door/fan configuration before gameplay queries, listen for player-pawn entry, and recheck the same inside player when facility state changes. Opening a door alone does not win without the player crossing/occupying the threshold. MainGate follows the registered Gate; ServiceTunnel follows its named open door AND stopped named fans. Escape is once per attempt. GameMode subscribes to facility changes, records escape state before callbacks, freezes player movement/look on victory without disabling UI input, emits the native/Blueprint escape event once, and reenables input when a reset starts the next attempt. Repeated changes must not stack input-disable counts. Missing facility queries safely fail.

#### Playing through the real systems

An integrated scenario must be possible using the real player/components/actors: collect an upgrade through focused interaction, hack a gate through the focused handheld route, cross its threshold, win and reset; another scenario uses live breaker/generator power, light/stealth, pry pickup, coolant/rack flood and tunnel access. Do not satisfy these by hardcoding fixture IDs, replacing dependencies with mocks, directly marking the game won or inventing independent actor state.

### Acceptance scenarios

| ID | Initial condition and action | Required coupled result |
|---|---|---|
| IS-01 | Two live breakers; request the third | Silent trip, machinery settles, lights/stealth/hazards read effective power. |
| IS-02 | Backup expires during a wet step | Process expiry before flood drain/short/corrosion, then alarm decay. |
| IS-03 | Patch cargo lift away from fan supply; displace patch | Lift operates independently, displaced device returns home and affected machinery settles. |
| IS-04 | Pry a sealable door under rising alarm | Forced/open state precedes noise; subsequent Lockdown cannot reseal forced door. |
| IS-05 | Open main gate while Alerted | Opening noise can cause Lockdown and immediate reseal; action change is not escape. |
| IS-06 | Conductive flood reaches configured thresholds | Powered circuits short, DOORS fail open, lift parks; only named tunnel corrodes. |
| IS-07 | Drain fans with valve open versus closed | Open valve retains water; after drain a pump can be used again. |
| IS-08 | Collect/remove drive | Upgrade and camera blindness change live; removing drive does not undo a camera loop. |
| IS-09 | Character climbs into hatch then hatch opens | Real sweeps block then permit traversal; jump/destruction release climb state. |
| IS-10 | Change lighting around stationary overlapping player | Immediate stealth queries reflect it; camera retains its separate volume policy. |
| IS-11 | Quiet lift crate carriage versus drop | Different alarm/drop/damage results; reset/recreation never replay historical damage. |
| IS-12 | Reset from hazard victim callback | Later hazard victims read current safety; this does not extend to crate sweeps. |
| IS-13 | Upgrade through focus, hack gate, cross threshold | Real shared actors reach one win; reset releases input and resets actors and inventory. |


## Implementation boundaries

Keep constructor component creation, reflected signatures and supplied assets intact. File-local helpers are allowed in the listed C++ files. Local builds and self-tests are welcome; only the listed implementation files belong in the submission. Do not move gameplay into protected files or inspect evaluator artifacts.

All required subsystem groups and integrated game scenarios must pass for full success. Reward is the fraction of required behavioral groups passed; a complete run with no passing groups receives zero. Missing or duplicate tests, harness crashes, platform failures and incomplete execution invalidate the result. Native component movement and visibility are checked, but headless checks do not certify rendered pixels.

Work in one solve session with normal build/test iteration, followed by a fixed submission. The full and partial facility tasks share an implementation and must stay in one dataset partition without shared solver context. Earlier component baselines do not validate this combined task.

## Files you may change

Only the following files are editable. Keep supplied constructors, assets, settings and files outside this list intact. Where a file contains supplied functions, change only the marked implementation bodies. The GDD is a design reference.

- `Source/ScifiSimEscape/Climbing/ClimbComponent.cpp`
- `Source/ScifiSimEscape/Facility/FacilityRules.cpp`
- `Source/ScifiSimEscape/Facility/FacilityStateSubsystem.cpp`
- `Source/ScifiSimEscape/FacilityDevices/AlarmConsole/FacilityAlarmConsole.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Camera/FacilityCamera.cpp`
- `Source/ScifiSimEscape/FacilityDevices/CoolantValve/FacilityCoolantValve.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Crate/FacilityCrate.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Door/FacilityDoor.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Exit/FacilityExit.cpp`
- `Source/ScifiSimEscape/FacilityDevices/FacilityActorBase.cpp`
- `Source/ScifiSimEscape/FacilityDevices/FacilityDeviceBase.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Fan/FacilityFan.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Flood/FacilityFlood.cpp`
- `Source/ScifiSimEscape/FacilityDevices/GateController/FacilityGateController.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Generator/FacilityGenerator.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Hatch/FacilityHatch.cpp`
- `Source/ScifiSimEscape/FacilityDevices/HidingSpot/FacilityHidingSpot.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Ladder/FacilityLadder.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Lift/FacilityLift.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Lift/FacilityLiftCallPanel.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Light/FacilityLight.cpp`
- `Source/ScifiSimEscape/FacilityDevices/LightZone/FacilityLightZone.cpp`
- `Source/ScifiSimEscape/FacilityDevices/PanelSwitch.cpp`
- `Source/ScifiSimEscape/FacilityDevices/Pickup/FacilityPickup.cpp`
- `Source/ScifiSimEscape/FacilityDevices/RoutingPanel/FacilityRoutingSwitch.cpp`
- `Source/ScifiSimEscape/FacilityDevices/ServerRack/FacilityServerRack.cpp`
- `Source/ScifiSimEscape/InteractionSystem/InteractableComponent.cpp`
- `Source/ScifiSimEscape/InteractionSystem/InteractorComponent.cpp`
- `Source/ScifiSimEscape/ScifiSimEscapeCharacter.cpp`
- `Source/ScifiSimEscape/ScifiSimEscapeGameMode.cpp`
- `Source/ScifiSimEscape/Stealth/StealthComponent.cpp`
