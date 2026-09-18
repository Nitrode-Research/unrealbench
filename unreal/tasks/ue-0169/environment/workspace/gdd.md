# RTS combat and player presentation — GDD

## Your part of the game

Implement unit health and combat, commands and navigation, camera and selection, player controls, HUD, world overlays and audiovisual feedback. Faction economy, construction, production, reclamation, match accounting and AI strategy are supplied. Keep the supplied counterpart working. The sections below describe how the whole game fits together; they do not expand your editable scope.

## The game

Two factions begin with mobile command vehicles on the supplied M3 map. Each faction develops a base, gathers Materials, manages Power and Supply, produces units and fights the opposing command asset. A match ends once, when a faction loses its active command asset.

Use the existing Unreal Engine 5.8.2 project and its RTS runtime module. Maps, assets, reflected classes, typed APIs, configuration and ownership boundaries are supplied. Keep gameplay native: the task does not require Blueprint gameplay, Gameplay Tags, networking, a new framework or an alternative map. Existing constructors, accessors, text formatting and layout helpers remain in place.

## Economy and building a base

### Resources and ownership

World subsystems own the economy, unit and structure registries, placement, deployment, production transactions, wreck ownership and match state. Actor components handle their own construction, production and reclamation progress. Queries must not change game state.

Materials, Power and Supply use the configured values. Track Power generation and demand, and used, reserved and available Supply for each faction. Used Supply and reserved Supply must remain consistent across all factories, including overlapping requests. Power deficits are valid; Materials and Supply cannot become negative.

Treat a transaction's identity separately from its values. Prevent repeated spending or refunds, and apply reservation conversion, release and rollback once. A failed or replayed transaction must not partly spend resources or reserve capacity.

### Deploying the command vehicle

Deployment promotes a living, eligible command vehicle into one HQ inside its authored starting zone. Check affordability, ownership, ground and obstructions across the full footprint when showing a preview and again when committing. A preview never spends resources, and a failed deployment leaves the vehicle and resources intact. After promotion, the HQ becomes the faction's command identity; cleaning up the old vehicle must not count as losing that identity.

### Placing structures

Ordinary structures need a constructed friendly build area, affordable cost and valid ground. Use the configured build radius, slope and footprint. Reject occupied ground and placements that obstruct factory exits. An Extractor also needs an unclaimed resource deposit.

Recheck these conditions when committing a placement. If spawning, registration or a deposit claim fails, roll back both the resource spending and the ownership changes.

### Construction and power

Construction takes the configured duration. A completed structure contributes its Power, Supply, income and build area once. Death or removal reverses its contributions and releases timers, occupancy and deposit claims once.

A brownout pauses factory work and powered turret fire. Preserve work already completed, and resume without counting the paused time as progress. **Extractor income continues during a brownout** so that a faction can recover. Losing Supply capacity does not disable the existing army. Already accepted reservations can convert into units, while new demand waits for enough capacity.

## Producing units

A living, constructed, friendly factory can queue its configured Infantry, Light and Heavy unit types. Enforce queue capacity and first-in, first-out ordering. Give accepted entries stable, positive, unique identities. Charging Materials, reserving Supply and adding an entry form one transaction across all factories.

Only the front entry makes progress. When it finishes, a blocked exit leaves that entry and its reservation in place for a later retry. Once clear, spawn one real registered unit, convert its reservation into used Supply once, publish one production event and give it the factory's rally order. Cancellation or factory destruction releases pending work and reservations.

## Wrecks and reclamation

A real death can create a configured wreck with stable ownership, value and location. A wreck has at most one reclamation lease. Eligible infantry approach it through ordinary navigation, hold that lease, accumulate configured progress and receive its value once. Credit the faction before consuming the wreck.

Release the lease without payment when the target becomes invalid or expires, the unit dies or leaves the permitted range, the command is replaced, the actor is removed, or the match ends. The reclamation component and command system decide legality; the presentation layer only reports it.

## Faction strategy

Use the supplied Normal and FastTest profiles, seeded candidates and stable registry ordering. The AI must operate through the same paid gameplay transactions as the player:

- Move and deploy its actual command vehicle.
- Establish Extractors, Generators, Supply structures and Factories.
- Produce its configured mix of units and place a bounded, spaced set of turrets.
- Defend threats, reclaim safe wrecks and replace lost infrastructure.
- Gather an attack force, travel to its muster point and then commit to an attack.
- Release or retarget stale commitments as units, targets and threats change.

Keep the number of defenders between `MinimumDefenseUnits` and `MaximumDefenseUnits`. Break ties in stable registry order. A threat may interrupt strategy while retaining the needed defense; when it disappears, release that commitment and resume ordinary planning. New commitments need distinct identities, but no particular numeric increment is required.

Waiting until a purchase is affordable is valid. The AI need not deliberately make a rejected Heavy-unit purchase. It must not teleport, inject money, produce free units or run a separate combat simulation.

## Units, orders and combat

### Health and lifetime

Initialize health and movement/weapon statistics from configuration. Accept finite positive damage, clamp health and publish health changes. Publish death once. Units bind and remove their lifetime callbacks, register and unregister, update Supply and loss accounting, create their configured wreck and stop active orders when they die.

### Orders and navigation

Group commands filter out invalid, dead and wrong-team units. Keep explicit command outcomes, deterministic formation assignments and monotonically identified orders. Replacing an order cancels its old navigation, target and reclamation state.

Use Unreal navigation for movement, arrival, projection, path failure and retries. An explicit Move order suppresses idle target acquisition. Do not silently turn it into an attack order.

### Weapons and turrets

Attack orders can target hostile units or structures. Chase targets outside weapon range, then apply real health damage at the configured firing cadence and publish the actual weapon event. Keep a valid target until the order policy permits a change. Clear dead or expired targets. Do not damage friendly, neutral or out-of-range targets.

Changing an attack target must not reset the weapon cooldown. Constructed, powered turrets follow the same typed target rules, stable tie-breaking and cooldown accounting. Brownout prevents firing. Avoid duplicate timers and damage after teardown.

## Player controls and camera

Keep the existing Enhanced Input bindings connected to real gameplay operations. The camera uses the configured pitch, zoom limits, interpolation and movement speed. Normalize keyboard and edge panning, then clamp movement to the authored bounds.

Selection belongs to the local player and accepts living owned units and structures. Support click selection, Shift toggling, clearing on empty ground and marquee selection dragged in either direction.

Resolve a contextual command in this order: wreck, structure, unit, then ground. An empty selection does nothing. Cancel obsolete previews and forward one typed operation for deployment, placement or production. An autonomous match is not a substitute for working human input.

## HUD and feedback

### Live information

HUD snapshots read live state without changing it. Include resources; selected identity, type, health and orders; construction and production queues; turret state; previews and refusals; and match totals. Missing context produces an informative empty view.

| Selection | Available action types |
| --- | --- |
| Infantry | Move, attack, reclaim |
| Command Vehicle | Move, deploy |
| Other combat units | Move, attack |

Display the highest-priority active warning, in this order: brownout, Supply cap, blocked factory exit, production refusal, reclamation refusal, then selected-order failure. Clear or replace the warning when its cause changes. No exact private sentence or layout is required.

The native Slate HUD attaches to the local viewport and is removed with it. Keep resource, status, selection, action and result sections readable at 720p, 1080p and 4K. Collapse unused sections and update from current snapshots.

### World overlays

An overlay descriptor includes its circle or rectangle shape, stable source and faction, validity, dimensions and lifetime. Repeated updates to an identity update one handle and actor. Invalid updates leave a valid entry intact.

Draw real, noncolliding geometry that does not affect navigation, using the supplied thin Material and editable HLSL. Show build rings with subtle fill, green valid footprints, red hatched invalid footprints, and hollow turret ranges with directional ticks.

Clean up overlays when their actor disappears, a preview is canceled, a structure dies or the match ends. Independent showcase entries remain visible when gameplay overlays are cleared.

### Sound and effects

Observe actual command, weapon, construction, production, death, reclamation and result events. Deduplicate by typed event identity. A rejected request must not consume an identity that a later valid event needs.

Bound receipt, key and effect storage, and release expired actors. Effects use configured lifetimes with their defaults and caps, real raw sounds/audio components, and native noncolliding meshes and materials. Receipts may store raw or normalized lifetime; the displayed effect must use the resolved lifetime. Presentation must never apply damage, spend money or decide the match result.

## Finishing a match

Record production, losses and reclamation once per source identity. Losing the current command asset resolves the match once and produces its identity, winner, loser, duration and both factions' totals. `ResolutionSequence` is zero during play and one after resolution. It is independent of the receipt's global `EventSequence`.

Propagate active/resolved state through commands, production and reclamation. Update result controls and remove gameplay overlays without replaying receipts or resolving a second time.

## Scope and validation

The target is the supplied two-faction skirmish. A later unit/structure surface shader based on custom primitive data is outside this task. No new shader feature, unfinished roadmap work or claim about human pacing is required.

Validate subsystems and complete matches through the same native components. The full-match scenario starts two command vehicles on M3 and must observe HQs, income, factories, units, turrets, attacks, combat and exactly one outcome. Both factions use seeded strategy. The test's player driver changes only the Normal profile's `MinimumRaidsBeforeHeadquarters` to one; its 15-game-minute limit is a test budget.

Behavioral and rendered checks are both required. Reward is the fraction of required behavioral groups passed, with full success requiring every mandatory group and complete-match scenario. Missing or duplicate tests, missing GPU support, engine/shader crashes and harness compilation failures invalidate the result. A headless-only run cannot establish full success. Native Windows input, Linux/Harbor input, sensitivity checks and rendered execution need validation before publishing difficulty claims.

Work in one solve session with local build/test iteration and then a fixed submission. Do not alter tests or protected files. Related full and partial tasks share a game implementation and must stay in one dataset partition without shared solver context.

## Acceptance scenarios

These sequences describe interactions between your implementation and the supplied systems.

| ID | Trigger and required consequence |
|---|---|
| RT-01 | Intervening occupancy/spending invalidates an earlier placement preview; commit has no cost/deposit/registry leak. |
| RT-02 | Completion applies Power/Supply/build area once and enables dependent placement/production. |
| RT-03 | Two Factories share reservation capacity; rejected work cannot partially spend or enqueue. |
| RT-04 | Supply loss leaves the existing army functional; accepted reservations convert; new demand waits for capacity. |
| RT-05 | Brownout preserves queue progress and stops turret fire; power returning resumes without paused-time catch-up; Extractor income continues. |
| RT-06 | Blocked production exits retain work/reservation; clearance yields one real unit/rally; Factory death releases pending reservations. |
| RT-07 | Attack chases, fires, changes real health, kills once and produces one wreck/loss event. |
| RT-08 | Explicit Move suppresses idle autoattack; replacing attack targets cannot reset weapon cooldown. |
| RT-09 | Contended wreck has one lease; interruption/death/range departure releases without payout; successful credit precedes consumption. |
| RT-10 | HQ promotion replaces mobile identity; old-vehicle cleanup cannot resolve the match; current command asset death resolves once. |
| RT-11 | Threat preempts strategy with retained defense; disappearance releases/recovery; muster travel precedes the explicit attack commitment. |
| RT-12 | Context input resolves wreck/structure/unit/ground precedence, cancels previews and forwards one authority operation. |
| RT-13 | Refusal changes update warning precedence/recovery; match result changes actions and removes gameplay overlays only. |
| RT-14 | Real producers feed one correctly identified cue; repeated bindings/receipts do not duplicate effects; expiry frees capacity. |
| RT-15 | Two real faction loops progress from mobile HQs to economies/production/combat and one immutable command-asset outcome. |

## Files you may change

Only the following files are editable. Keep supplied constructors, assets, settings and files outside this list intact. Where a file contains supplied functions, change only the marked implementation bodies. The GDD is a design reference.

- `Shaders/RTSOverlayPrimitive.ush`
- `Source/RTS/Private/Camera/RTSCameraPawn.cpp`
- `Source/RTS/Private/Combat/RTSHealthComponent.cpp`
- `Source/RTS/Private/Combat/RTSTurretCombatComponent.cpp`
- `Source/RTS/Private/Game/RTSHUD.cpp`
- `Source/RTS/Private/Game/RTSPlayerController.cpp`
- `Source/RTS/Private/Orders/RTSCommandSubsystem.cpp`
- `Source/RTS/Private/Orders/RTSUnitOrderComponent.cpp`
- `Source/RTS/Private/Presentation/RTSPresentationSubsystem.cpp`
- `Source/RTS/Private/Presentation/RTSTransientEffect.cpp`
- `Source/RTS/Private/Presentation/RTSWorldOverlayActor.cpp`
- `Source/RTS/Private/Presentation/RTSWorldOverlaySubsystem.cpp`
- `Source/RTS/Private/Selection/RTSSelectionSubsystem.cpp`
- `Source/RTS/Private/Structures/RTSDeploymentViewSubsystem.cpp`
- `Source/RTS/Private/UI/RTSHUDSnapshot.cpp`
- `Source/RTS/Private/UI/SRTSStatusOverlay.cpp`
- `Source/RTS/Private/Units/RTSCombatUnit.cpp`
- `Source/RTS/Private/Units/RTSUnitController.cpp`
- `Source/RTS/Private/Units/RTSUnitRegistrySubsystem.cpp`
