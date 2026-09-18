# Foundations world and paired movement — GDD

## Your part of the game

Implement the world-generation scripts and native world, movement, navigation, cable and camera systems. The narrative, interaction, inventory, save and HUD systems are supplied. Preserve the supplied counterpart, including its functions inside shared files. Both parts use the same real game objects and state. The full design below explains their integration; the editable-file list defines your work.

## The game

Foundations is a native C++ puzzle game about two connected characters, LT and RT. They move together, approach objects, exchange items and use dialogue choices to solve puzzles. The playable sequence is **EntryGate → LoadingDocks → completion screen**. Players can save, resume, complete the game and start again.

The supplied prototype maps contain this playable sequence. A separate geometry pipeline recreates the source environment from exports; the original interactions are not yet connected to that generated map. Both the playable loop and the geometry pipeline belong to the task's integration checks.

Keep the existing module, public classes, ownership, data formats, engine integration, maps and assets. Original shaders, animation adapters, optional audio and primitive character fallbacks are supplied. New raw animation assets are not required. Office, a source Outro, three tape slots, missing settings and new dialogue animations are outside this task.

## Story, interactions and progression

### Narrative data

`Narrative.json` defines the story: scopes, triggers and their order, conditions, once-only entries, choices, speakers and effects. Implement the loader and interpreter against this data rather than coding a fixed puzzle walkthrough.

Keep all 812 entries and the supplied trigger lists. IDs are signed 32-bit integers. Reject malformed schemas, an incorrect pinned revision, duplicate IDs and invalid references or operations. Keep the two documented legacy SampleScene references valid. A failed load leaves the previous valid database unchanged.

Conditions use inclusive ranges. A missing scope or fact fails the condition. Preserve the distinction between applying an entry and invoking its effects. Dispatch children before parent callbacks, and general callbacks before specific callbacks. Equal-weight ordering follows the exported relation lists. Stop runaway recursive dispatch with a nonempty diagnostic at 128 operations.

### Reaching an interaction

Characters must walk to their assigned, distinct interaction waypoints. Assigning an interaction or teleporting is not arrival. Use the supplied default waypoints and support explicitly configured actors.

Track the initiator, listener and participant presence: `1` means here, `0` means free and `-1` means elsewhere. Movement and following release old interaction ownership. Wait for the other character only when the narrative requests participation. That character must approach normally; queued actions resume when the participants are physically ready. A window action does not automatically require two characters.

### Inventory

Each character has one item slot. A new game starts with LT empty-handed and RT holding the authored gun; restarting returns to this state. Pickup requires the narrative's conditions and a free slot. The current speaker owns the item used by an action. Using it transfers the item into the interaction's local state and empties the hand.

World item visibility follows story facts, including after loading a save. A taken item must disappear from the world, and loading an earlier state must put the appropriate item back.

### Dialogue and control

Use the supplied immediate dialogue-pumping model. Keep authored dialogue, action identity and speaker ownership intact. A private display sentence need not be matched exactly, but the story data is not editable.

Closing dialogue clears pending panel work, returns player control, settles the camera and follows the autosave lifecycle. Pause/resume, hover feedback, speaker and action labels, inventory labels and contextual choices must reflect actual state. Do not add a typewriter animation or expose choices before their conditions are met.

### The two-area puzzle

In EntryGate, implement the rock inspection and pickup, window, chair and gate sequence through the narrative and interaction APIs. Travel to LoadingDocks with global/local facts and inventory intact.

In LoadingDocks, connect the kiosk, bays, pole, item transfers, wedge, gate and floodlights to the valid final exit. World effects update visibility, collision, absolute transforms or hinges, light intensity and output facts. Repeating an effect or encountering a destroyed target must be safe.

Completion settles both real characters, pauses play and shows the completion panel. It must survive save/load, and the player must be able to start a new game. The detailed action tables later in this document describe conditions and consequences; they do not replace the narrative interpreter or permit direct grants of puzzle facts.

## Saving and loading

Keep the native version 1 format: source identity, map, completion, both character positions and yaws, global facts, and interaction-local facts. Encode deterministically with sorted IDs. The source-map extension supports authored scene starts. An older payload without a completed field means an incomplete game.

Validate the whole payload before changing the live game:

| Limit or field | Required handling |
| --- | --- |
| Total payload | Reject more than 1 MiB |
| Facts and interactions | Reject more than 4,096 facts or 256 interactions |
| ID length | Reject more than 128 characters |
| Fact values | Require integral, signed 32-bit values |
| Character poses | Reject nonfinite or out-of-bounds values |
| Content | Reject unknown facts, wrong scopes, invalid held items or malformed data |
| Compatibility | Reject unsupported versions, maps or source identities |

Preparing a loaded state does not mutate the active session. Applying it replaces the prior state rather than merging it. Apply facts before actor initialization and consume matching-map spawn state once.

Explicit named test slots are isolated and usable. Quick/manual saves and automatic saves are different slots. Automated checks must not overwrite a player's ordinary automatic save.

## HUD and world hints

Implement the native Slate HUD and real world-space indicators. Show contextual inventory and interaction actions, speaker/choice state, pause, new game, save/load controls, and character, command and hover hints. The callbacks must operate on the same game state as the rest of the systems.

These are rendered requirements. Metadata or a headless state snapshot alone is not enough. A run without a GPU/renderer cannot establish complete verification.

## World generation

### Source data and workflow

Use the immutable exports in `SourceData` and `Content/Data`. Run `Scripts/prepare_entry_terrain.py` and `Scripts/prepare_entry_composition.py`, followed by their corresponding `build_entry` scripts in Unreal Editor. This requires no original checkout, network access, Blender or prebuilt output. Keep the supplied shader and material construction.

Generated content belongs under `/Game/Task0170`. Evaluation discards it and generates a fresh copy, so output assets are not a substitute for the implementation.

### Coordinates, meshes and collision

Unity matrices are row-major affine matrices acting on column vectors in metres. Unreal uses centimetres, with axes mapped from `(x, y, z)` to `(z, x, y)`. Bake linear transforms while keeping pivots, inverse-transpose normals, tangent handedness, UVs, colours, material slots and submesh identity correct.

Support reflections, nonuniform scale and shear. Reject singular or non-affine inputs and boxes that cannot represent a shear without loss. Inactive renderers and trigger colliders are not solid obstacles. Navigation exclusions must not become physical walls.

| Source content | Expected output |
| --- | --- |
| Terrain | 102 mesh placements, 24 solid boxes, 8 navigation exclusions |
| Composition | 166 accounted renderers, including 73 additions |
| Special additions | 6 foliage cards and 9 fact-driven display parts |
| Added composition | Noncolliding and no navigation changes after map reload |

Native surfaces, routes and navigation footprint must match the inputs. Physics checks allow 1 mm of surface error, 0.3 degrees of normal error and 3 cm of settled root-height error. Navigation boundary checks exclude the captured 60 cm ambiguous border band.

## Paired movement and cable

### Cable simulation

Read both source start positions/facings and all 82 ordered chain centres. Retain the ordinary `CableComponent` and authored-curve branches. Use 820 cm total rest length, 5 cm endpoint spacing and 10 cm interior spacing.

Drive the native procedural tube from the fixed-step constraint, gravity and collision simulation. A missing endpoint must not crash or discard an already valid cable. Reinitialization moves its existing points rather than creating another chain. Preserve supplied constructor component graphs and defaults.

### Input and leadership

LT uses the left mouse button; RT uses the right. If both are pressed together, process LT first. The first ground press selects the leader. The second requests following without stealing leadership. Holding both selects tight following.

Leadership transfers only after the 0.1-second grace period, not at exactly that boundary. Releasing both clears leadership; a simultaneous release keeps the tight-follow latch until the next performed press. A held bit is not itself a new press.

Input outside the viewport cannot change focus or navigate. An empty hit inside it can change focus without starting navigation. Hitting an interaction does not count as a ground navigation request.

### Navigation and following

`CommandMove` moves one character independently and must not assign pair-following state. `NavigateCharacter` and controller updates handle the pair. Reject nonfinite goals, unreachable goals and partial ground paths without discarding an existing interaction assignment. Keep an unchanged active native move instead of restarting it each tick.

An idle character follows a moving or interacting partner. A newly commanded navigator makes an existing navigator yield, while a character anchored to an interaction stays anchored. Promote the idle partner to following before clipping the requested goal; otherwise part of the original goal can be lost permanently.

Limit separation along the full navigation path rather than the straight-line distance. Handle zero-length segments and exact boundaries safely.

| Setting | Value |
| --- | --- |
| Movement speed | 500 cm/s |
| Acceleration | 3,000 cm/s² |
| Rotation speed | 400 degrees/s |
| Close distance | 200 cm |
| Normal following band | 300–600 cm |
| Tight following band | 50–150 cm |
| Maximum path separation | 800 cm |

Within the chosen following band, interpolate speed from 100 cm/s to the configured speed and acceleration from 800 cm/s² to the configured acceleration. When approaching overlap inside `CloseDistance`, use lateral escape, zero stop distance and scale speed/acceleration by `clamp(1 - distance / CloseDistance)`. Preserve the last nonzero approach direction and the legitimate crossing-stop behavior.

## Cameras and animation timing

Keep both camera profiles. The prototype view includes both characters on narrow and wide screens, smooths consistently across frame rates and snaps on teleports.

The source view uses its converted basis, a 500 cm vertical half-height, a 5,000 cm camera distance and a fixed 5 ms focus spring. Advance the spring using the preceding frame's focus selection. Choose the current eligible Navigate/Interact focus after input, then apply the camera transform after character movement.

Keep the documented focus behavior for dialogue, ineligible or absent selection, and shared/both-character participation. Leave the source's inactive group zoom disabled. Respect authored interaction-profile priority, cached targets, eased entry and exit, and interrupted transitions. Preserve locomotion thresholds, timing and frame events without requiring raw animation assets.

## Integration and completion checks

Both halves must run together through the existing architecture; the supplied counterpart must really execute. Use the generated-world pipeline, subsystem checks, one uninterrupted EntryGate-to-LoadingDocks play/save sequence and the rendered HUD checks. Do not mock the other half or directly grant story facts to skip gameplay.

Reward is the fraction of required checks passed. Full success requires every mandatory gate. Partial credit does not establish a complete game. Missing or duplicate tests, harness crashes and platform failures invalidate the reward.

Work in one solve session with local builds and tests, followed by a fixed submission. Keep evaluator infrastructure and reference implementations outside the solve. These related tasks share a game and must stay in one dataset partition without shared solver context.

## Narrative action reference

Derived mechanically from immutable Narrative.json, not hidden expected outputs.
All criteria in a row must hold in the entry's context. Active choice membership
and physical readiness are required too. Context facts are transient; global and
interaction-scope facts persist. A dialogue entry increments its own scoped
application count; once-only entries become unavailable after application.

| Action / speaker | Exact source criteria | Direct application effects / callbacks | Persistence |
|---|---|---|---|
| `EntryGate.L_do_inspect_rock` (1389448); LT | isLTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `EntryGate.L_do_pick_up_rock` (1389800); LT | I_rock = 1; LT_item = 0; L_inspect_rock_A_1 in [1, 2147483647]; isLTPresent = 1 | invoke on apply: I_rock | Application count: global; effects in each fact's declared scope. |
| `EntryGate.L_do_inspect_kiosk` (1389390); LT | isLTPresent = 1 | inspected_window = 1 | Application count: global; effects in each fact's declared scope. |
| `EntryGate.L_do_break_window` (1389391); LT | window_broken = 0; inspected_window = 1; LT_item = I_rock; isLTPresent = 1 | window_broken = 1; invoke on apply: LT_item | Application count: global; effects in each fact's declared scope. |
| `EntryGate.L_do_take_chair` (1389392); LT | window_broken = 1; LT_item = 0; I_chair = 1; isLTPresent = 1 | invoke on apply: I_chair | Application count: global; effects in each fact's declared scope. |
| `EntryGate.L_do_inspect_gate` (1389385); LT | I_chair = 0; isLTPresent = 1 | tried_climbing = 1 | Application count: global; effects in each fact's declared scope. |
| `EntryGate.L_do_place_chair` (1389386); LT | LT_item = I_chair; tried_climbing = 1; isLTPresent = 1 | invoke on apply: LT_item | Application count: global; effects in each fact's declared scope. |
| `EntryGate.L_do_jump_over` (1389398); LT | I_chair = 1; isLTPresent = 1 | invoke on apply: enter | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_inspect_kiosk` (1388912); LT | isLTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_kiosk_knob` (1388938); LT | kiosk_door_opened = 0; L_inspect_kiosk_closed in [1, 2147483647]; isLTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_open_gate` (1389727); LT | kiosk_saw_controls = 1; kiosk_tried_open_gate = 0; isLTPresent = 1 | kiosk_tried_open_gate = 1 | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_inspect_bay_2` (1389011); LT | isLTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_ask_bay_2_box_dark_away` (1389200); LT | bay_2_L_saw_box = 1; bay_2_looking_for_light in [-2147483648, 0]; isRTPresent in [-2147483648, 0]; isLTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.R_do_inspect_pole` (1389002); RT | isRTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.R_do_find_lights` (1389776); RT | noticed_pole_lights = 1; kiosk_door_opened = 1; bay_2_lit = 0; isRTPresent = 1 | looked_for_light_switch = 1 | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_inspect_bay_1` (1389007); LT | isLTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_pickup_wedge` (1389661); LT | I_wedge = 1; L_inspect_bay_1_wedge in [1, 2147483647]; isLTPresent = 1 | invoke on apply: I_wedge | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_insert_wedge` (1389663); LT | LT_item = I_wedge; kiosk_tried_open_gate = 1; looked_for_light_switch = 1; isLTPresent = 1 | Continue authored response dispatch | Application count: global; effects in each fact's declared scope. |
| `LoadingDocs.L_do_open_bay_2` (1389665); LT | bay_L_identified = 1; isRTPresent in [-2147483648, 0]; isLTPresent = 1 | invoke on apply: enter | Application count: global; effects in each fact's declared scope. |

## Player actions and their consequences

| Trigger | Participants / inventory | Required consequence | Survives travel/save? |
|---|---|---|---|
| Fresh session / restart | LT empty; RT holds Gun | Independent one-slot ownership; world inputs seed once | Inventory and initialized facts do |
| Inspect then pick up rock | LT physically present, LT empty, local rock present, inspection response applied | Item invocation transfers rock to LT; local item becomes absent and world rock hides | Yes |
| Break window | LT present holding rock, kiosk inspected, window intact; RT is not additionally required | Use callback empties LT and places consumed item in interaction; broken-window global updates visible variants | Yes |
| Take chair | LT present/empty, window broken, local chair present | LT receives chair, kiosk chair display disappears | Yes |
| Place chair at gate | LT present with chair, gate inspection applied | LT empties, gate's local chair fact becomes present and chair displays | Yes |
| Climb gate | LT present, gate chair present | Authored exit event travels to LoadingDocks; neither teleport nor direct grants of subsequent puzzle facts | Global/local facts and inventory continue |
| Kiosk knob / controls | LT present; closed-door inspection gates knob action | Authored dialogue responses open the kiosk, reveal controls, and enable the gate attempt; repeated inspection uses current facts | Yes |
| Ask partner at dark Bay2 | LT present, RT absent/free as authored, inspected box, no established light-search state | Source callOther callback requests actual RT approach; action pump waits for physical readiness then resumes | Persistent story effects yes; pending approach is runtime only |
| Inspect pole, then find light switch at kiosk | RT present at each action; pole observation, kiosk open and bay dark gate switch search | Source response sets observations and looked-for-switch state; do not bypass movement or story criteria | Yes |
| Inspect Bay1 / pick wedge | LT present; wedge present and inspection response applied; inventory transfer still requires empty LT hand | Wedge moves from local state to LT, display hides | Yes |
| Insert wedge at gate | LT present holding wedge; gate attempt and light-switch search done | Use callback empties LT and transfers wedge to gate; actual world-effect adapter powers floodlights and bay-lit fact | Yes |
| Open identified lit Bay2 | LT present, RT absent as this action requires, bay identified by source chain | Native completed flag, both characters settled, paused completion panel; no extra hidden participant requirement | Yes; explicit save/reload keeps completion |

This table does not replace the complete narrative. Process response-trigger lists
and their conditions, including alternate eligible choices. An empty direct-effect
cell does not mean no effect: the authored response chain runs through ordinary
Apply/Invoke callbacks. Do not hard-code these route IDs in production.

## Files you may change

Only the following files are editable. Keep supplied constructors, assets, settings and files outside this list intact. Where a file contains supplied functions, change only the marked implementation bodies. The GDD is a design reference.

- `Scripts/build_entry_composition.py`
- `Scripts/build_entry_terrain.py`
- `Scripts/prepare_entry_composition.py`
- `Scripts/prepare_entry_terrain.py`
- `Scripts/scene_coordinates.py`
- `Source/foundations_ue/Editor/LinkSceneMeshTools.cpp`
- `Source/foundations_ue/Gameplay/LinkChain.cpp`
- `Source/foundations_ue/Gameplay/LinkCharacter.cpp`
- `Source/foundations_ue/Gameplay/LinkGameMode.cpp`
- `Source/foundations_ue/Gameplay/LinkPlayerController.cpp`
- `Source/foundations_ue/Gameplay/LinkSceneLayout.cpp`
- `Source/foundations_ue/Gameplay/LinkSourceItemDisplay.cpp`
- `Source/foundations_ue/Rules/LinkCamera.cpp`
- `Source/foundations_ue/Rules/LinkCameraResponse.cpp`
- `Source/foundations_ue/Rules/LinkCommands.cpp`
- `Source/foundations_ue/Rules/LinkInteractionCamera.cpp`
- `Source/foundations_ue/Rules/LinkLocomotion.h`
- `Source/foundations_ue/Rules/LinkNavigation.cpp`
- `Source/foundations_ue/Rules/LinkSceneCoordinates.h`
- `Source/foundations_ue/Rules/LinkSeededChain.h`
