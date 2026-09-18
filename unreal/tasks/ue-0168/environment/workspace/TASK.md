# Restore RTS faction simulation and strategy

Only Half A below is removed; the complementary half is supplied and protected.
# Restore the implemented native RTS skirmish

Restore the missing implementations in the existing Unreal 5.8.2 project. This is
one autonomous solve session with ordinary local compilation/testing, followed by
a frozen submission. No post-grading repair attempt is part of the task.

The complete target is the implemented two-faction skirmish, not a new game or an
unfinished roadmap. Preserve the contractor's native modules, reflected classes,
typed public APIs, data formats, authored maps, asset references and ownership.
Use the existing RTS runtime module. No Blueprint gameplay, Gameplay Tags,
replication, external framework, alternate authority or substitute map is needed.
Engine construction, immutable headers/configuration/assets, simple accessors,
text-formatting and layout helper functions remain as supplied infrastructure.
The task's editable-path list is authoritative. Do not alter files outside it,
add substitute source, change tests, or circumvent the existing architecture.

## Half A: faction simulation, economy and strategy

- World subsystems own atomic economy, unit/structure registration, placement,
  deployment, production transactions, wreck ownership and match state. Actor
  components own construction, production and reclaim progress. Human and AI
  adapters use these same authorities rather than granting resources or spawning
  completed work directly.
- Preserve per-faction Materials, Power generation/demand and used/reserved/capacity
  Supply. Validate deltas atomically, distinguish transaction identity from values,
  prevent replayed spending/refunds and reconcile reservation conversion, release
  and rollback exactly once. Invalid/replayed operations cannot partially mutate
  state. Power deficits remain representable; no negative Materials or Supply.
- A living eligible Command Vehicle deploys one HQ in its authored starting zone
  through valid complete-footprint ground/obstruction checks. Failure leaves its
  vehicle/resources intact; success consumes the vehicle and registers one HQ.
  Previews do not spend and commits revalidate current legality.
- Other structures need a constructed friendly build area, valid ground/footprint,
  affordable configured cost and, for Extractors, an available deposit. Respect
  configured slope, footprint and build radius, occupied ground and Factory exits.
  Failed spawning/registration/claiming rolls back spending and ownership.
- Construction advances by configured duration; completion contributes Power,
  Supply and income once. Death/end-play removes contributions, timers, occupancy
  and deposit claims once. This pinned version keeps Extractor income running
  during brownout so that the faction can recover. Factory work and powered
  turret fire pause during brownout; do not inherit an earlier milestone's
  Extractor-pauses policy.
- Factory queues accept configured Infantry/Light/Heavy types only for available
  constructed friendly factories. Respect capacity, FIFO, stable positive unique
  queue identities, atomic cost/reservation and cross-Factory Supply. Only the
  front advances; blocked exit retains completed work and retries. Completion
  spawns a real registered unit, converts Supply once, reports production once
  and issues its rally order. Destruction/cancellation releases pending work.
- Wrecks originate from real death, have stable ownership/value/location and one
  lease. Eligible infantry approach through ordinary navigation, hold the lease,
  accrue configured progress and credit/consume once. Invalid/dead/expired targets,
  command replacement, actor teardown or match completion release safely. Reclaim
  legality belongs to the component/command authority, not presentation.
- Match reports account for production, loss and reclaim once by source identity.
  Command-structure loss resolves the game once with winner/loser, identity,
  duration and both factions' totals. ResolutionSequence is the independent outcome
  ordinal (zero while active, one after the only resolution), not the global
  match-event sequence; that receipt retains its own separate EventSequence.
  Active/resolved policy must propagate through
  the existing command/production/reclaim interfaces.
- AI uses the configured Normal/FastTest profiles, stable registries and seeded
  deterministic candidates. It moves and deploys its real vehicle, establishes
  Extractor/Generator/Supply/Factory through paid transactions, produces its mixed
  roster, builds bounded spaced turrets, defends threats, reclaims safe wrecks,
  replaces lost infrastructure, musters attacks and releases/retargets stale
  commitments. No teleporting, free production, injected money or parallel combat.
  Defender bounds are MinimumDefenseUnits..MaximumDefenseUnits; stable registry
  order breaks ties. Waiting until affordable is allowed: a deliberately rejected
  Heavy purchase is not required. New commitment identity must differ, not follow
  an undisclosed exact numeric progression.

## Half B: units, commands and player-facing game

- Health initializes from configuration, applies finite positive damage correctly,
  clamps health and emits health/death changes with one death transition. Units
  bind/unbind lifetime callbacks, register and unregister, account for Supply/loss,
  create their configured wreck and stop active orders on death. Preserve typed
  team relationships and configured movement/weapon statistics.
- Group commands filter invalid/dead/wrong-team units, preserve explicit outcomes,
  stable deterministic formation assignment and monotonically identified orders.
  Replacing a command cancels stale navigation/target/reclaim state. Move dispatch,
  arrival, projection/path failure and retry use engine navigation. Explicit move
  is not silently replaced by idle auto-acquisition.
- Attack accepts hostile unit or structure targets, chases outside range, fires
  through real health damage at configured cadence and publishes actual weapon
  events. Retain valid targets; clear dead/expired targets and acquire only when
  the current order policy permits. Ignore friendly/neutral/out-of-range targets.
  Powered constructed turrets share the same typed rules, stable tie-breaking and
  cooldown accounting; no duplicate timers or damage after teardown.
- The camera uses configured pitch, zoom bounds/interpolation and speed, normalizes
  keyboard/edge pan and clamps to authored bounds. Native Enhanced Input remains
  the existing binding mechanism. Human controls must actually reach the gameplay
  interfaces; a passing autonomous match does not replace input verification.
- Selection belongs to the local player: living owned units/structures, click,
  shift toggle, empty clear and reversed-corner marquee. Context commands prefer
  wreck, structure, unit, then ground; empty selection does nothing. Deployment,
  placement and production controls forward one typed operation to authority.
- HUD snapshots capture live economy, selection identity/type/health/orders,
  construction/queue/turret state, previews/refusals and match totals without side
  effects. Infantry has move/attack/reclaim, Command Vehicle move/deploy, other
  combat units move/attack. Unavailable context returns an informative empty view.
  Warning precedence is brownout, Supply cap, blocked Factory exit, production
  refusal, reclaim refusal, then selected order failure. Warnings recover as their
  causes clear. No undisclosed exact display string or private layout is required.
- Native Slate HUD attaches/removes with its local viewport, stays live and renders
  responsive resource/status/selection/actions/result sections at 720p,1080p,4K.
  Collapse unused sections and keep important values readable. No stale snapshot.
- Overlay descriptors carry typed circle/rectangle mode, stable source/faction,
  validity, dimensions and lifetime. Invalid updates cannot damage valid entries;
  repeated identity updates a single handle/actor. Cleanup follows actor lifetime,
  preview cancellation, structure destruction and match resolution while independent
  showcase entries survive. Render real noncolliding/nonnavigational geometry via
  the supplied thin Material and editable HLSL: build rings/subtle fill, green valid
  and red hatched invalid footprints, hollow turret range with directional ticks.
- Presentation observes real command, weapon, construction, production, death,
  reclaim and result events. Deduplicate by typed event identity; rejected requests
  do not poison identity. Bound receipts/keys/effects and release expired actors.
  Accepted effects have configured/clamped lifetime, real raw sound/audio component,
  native noncolliding meshes/materials and typed visual vocabulary. Either raw or
  normalized lifetime in a receipt is accepted; actual effect lifetime must resolve
  defaults/caps. Presentation cannot damage actors, spend money or resolve matches.

## End-to-end and scoring

Required interaction chains (all use existing submitted/supplied counterparts):

| ID | Trigger and required consequence |
|---|---|
| RT-01 | Intervening occupancy/spending invalidates an earlier placement preview; commit has no cost/deposit/registry leak. |
| RT-02 | Completion applies Power/Supply/build area once and enables dependent placement/production. |
| RT-03 | Two Factories share reservation capacity; rejected work cannot partially spend or enqueue. |
| RT-04 | Supply loss leaves the existing army functional; accepted reservations convert; new demand waits for capacity. |
| RT-05 | Brownout preserves queue progress and stops turret fire; restored power resumes without paused-time catch-up; Extractor income continues. |
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

This anchor does not contain the later GDD's secondary unit/structure surface
shader driven by custom primitive data. That feature is not a restoration target.
The complete task means the implemented reference game, not complete current GDD
compliance, a new shader, a human pacing certification or unfinished roadmap work.

The shared full-match verifier starts two real command vehicles on the authored
M3 map and drives BOTH factions through the submitted strategy and gameplay
components. It must observe HQ, income, Factory, units, turrets, attacks, combat
and exactly one resolution. No supplied mock economy or attack implementation
carries the full task. The diagnostic player driver changes only the supplied
Normal profile's MinimumRaidsBeforeHeadquarters to one; both retain a seeded
strategy. Its 15-game-minute bound is an automation budget, not a product pacing
claim. Separate subsystem scenarios cover independent invariants and UI/effects.

CPU rules and rendered requirements are separate mandatory lanes. A missing GPU,
shader/engine crash, missing/duplicate tests or harness compilation error invalidates
reward; it is not a model behavioral failure. Headless-only execution is diagnostic
and cannot produce full success. Complete-game success requires every mandatory
group including the end-to-end runs; group pass fraction is diagnostic only.
Native Windows controls, actual Linux/Harbor controls, mutation sensitivity and
rendered-host execution must be established before publishing difficulty scores.

The full task and both halves deliberately share the same reference and expose
the complementary implementations. Keep them in one dataset partition and do
not reuse solver context between family members during calibration.

## Exact editable paths

- Source/RTS/Private/AI/RTSAIStrategyBootstrap.cpp
- Source/RTS/Private/AI/RTSAIStrategySubsystem.cpp
- Source/RTS/Private/Economy/RTSEconomySubsystem.cpp
- Source/RTS/Private/Match/RTSMatchSubsystem.cpp
- Source/RTS/Private/Production/RTSProductionComponent.cpp
- Source/RTS/Private/Production/RTSProductionSubsystem.cpp
- Source/RTS/Private/Reclaim/RTSReclaimComponent.cpp
- Source/RTS/Private/Reclaim/RTSWreckage.cpp
- Source/RTS/Private/Reclaim/RTSWreckageSubsystem.cpp
- Source/RTS/Private/Structures/RTSStructure.cpp
- Source/RTS/Private/Structures/RTSStructureSubsystem.cpp
