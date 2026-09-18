Restore B below. A is supplied and must remain intact, including its functions in shared controller files.

# Restore the implemented Foundations game

The supplied contractor project is a native C++ two-character puzzle game. Restore
the missing implementation without replacing its module, public classes, ownership
boundaries, authored content, data formats or native engine integration. The task
is one autonomous solve session with local build/test iteration, then an immutable
submission. A matching private implementation is not required.

The complete implemented game here is EntryGate -> LoadingDocks -> the native
completion panel, with movement, story-driven interactions, persistence and HUD.
The separate source-geometry reconstruction pipeline is implemented and required
too. It does not yet have the original source game's interactions wired into its
new map: the playable loop remains in the contractor's preserved prototype maps.
Do not invent Office, a faithful source Outro, three tape slots, missing settings,
new dialogue animation or unfinished roadmap features. This is not a claim of
four-scene Unity parity. Original shaders, licensed-animation adapters, optional
audio and the prototype level/prop assets remain supplied. No raw animation assets
are required; keep the existing primitive character fallback.

## World reconstruction and paired runtime (A)

From the immutable exports in SourceData and Content/Data, recreate native geometry
with Scripts/prepare_entry_terrain.py and prepare_entry_composition.py, followed
by the corresponding build_entry scripts in Unreal Editor. No source checkout,
network, Blender or completed output is necessary. Outputs belong under
/Game/Task0170 and are discarded before evaluation. Keep supplied shader/material
construction. Unity row-major affine matrices act on column vectors in metres;
Unreal uses centimetres and (x,y,z)->(z,x,y). Bake linear transforms while retaining
pivots, inverse-transpose normals, tangent handedness, UVs, colours, material slots
and submesh identity. Support reflections, nonuniform scale and shear; reject
singular/non-affine input and boxes that cannot represent shear without loss.

The pinned terrain definitions produce102 mesh placements,24 solid boxes and8
navigation exclusions. Inactive renderers and trigger colliders are not physical
obstacles. Navigation exclusion must not become a physical wall. The source
composition accounts for166 renderers, with73 additions including6 foliage cards
and9 fact-driven display parts; those additions remain noncolliding and do not
affect navigation after map reload. Native surfaces, authored routes and source
navigation footprint must agree with supplied inputs. Native physical probes use
1mm surface,0.3degree normal and3cm settled root-height tolerance; navigation
boundary probes exclude the captured60cm ambiguous border band.

Read both source starts/facings and82 ordered chain centres. Retain ordinary
CableComponent and authored-curve branches,820cm total rest length,5cm endpoint
spacing and10cm interior spacing. Update the native procedural tube from the
fixed-step constrained/gravity/collision simulation. A missing endpoint must not
crash or invalidate a previously initialized valid cable; reinitialization moves
the points to the new endpoints without creating an additional chain. Constructor
component graphs and defaults remain supplied.

Restore actual native paired movement and input routing: LT is left mouse, RT is
right mouse; simultaneous presses process LT first. First ground press owns the
leader; the second requests following without stealing it. Both held selects
tight following. Leadership transfers only after the0.1s grace has elapsed, not
at equality. Releasing all clears leadership; simultaneous release retains the
tight-follow latch until the next performed press. Held bits alone are not presses.
Outside-viewport input cannot change focus or navigate; an in-viewport empty hit
can focus without navigation. Interaction hits are not ground navigation.

CommandMove is the low-level independent native move for one character. It must
not assign pair following. NavigateCharacter and controller updates perform the
paired orchestration. Reject nonfinite/unreachable/partial ground routes without
discarding an existing interaction assignment. Keep an unchanged active native
move request rather than restarting it each tick. An idle partner follows a
navigating/interaction partner; a new navigator makes an existing navigator yield.
Promote the idle partner to following before clipping the navigator's requested
goal, so the full requested goal is not permanently lost. An interaction-anchored
partner remains anchored. Limit separation along the complete navigation path,
not radial distance; safely handle zero-length segments and exact boundaries.

Use the supplied configuration:500cm/s speed,3000cm/s2 acceleration,400deg/s
rotation,200cm close distance,300-600cm normal following,50-150cm tight following
and800cm path separation. Follow speed interpolates100->configured speed and
acceleration800->configured acceleration over its selected distance band.
Approaching overlap within CloseDistance uses lateral escape, zero stop distance,
and configured speed/acceleration multiplied by clamp(1-distance/CloseDistance).
Preserve the remembered nonzero approach direction and legitimate crossing stop.

Keep both camera profiles. Prototype framing must include both characters over
narrow/wide aspects, smooth consistently across frame rates and snap teleports.
The source profile uses its configured converted basis,500cm vertical half-height,
5000cm distance and fixed5ms focus spring. Advance the spring with the preceding
frame's focus selection, choose current eligible Navigate/Interact focus after
input, and apply actual camera transform after character movement. Dialogue,
ineligible/no selection and both/shared participants preserve the documented focus
semantics; source inactive group zoom must not be enabled. Respect authored
interaction profile priority, cached targets, eased entry/exit and interrupted
transitions. Preserve the supplied locomotion thresholds/timing and frame-event
semantics without adding raw animation dependencies.

## Story, interaction, progression and persistence (B)

Rebuild the data-driven narrative loader and interpreter, not hard-coded puzzle
answers. Narrative.json is authoritative for scopes, trigger ordering, criteria,
once-only entries, choices, current speaker and apply/invoke effects. Preserve
812 entries and the validated trigger lists. IDs are signed32-bit; reject malformed
schemas, wrong pinned revision, invalid references/operations and duplicate IDs.
The two documented legacy SampleScene references remain accepted. Criteria use
inclusive ranges, absent/missing-scope facts fail, apply and invoke are distinct,
children dispatch before parent callbacks, general callback precedes specific,
and runaway recursive dispatch fails with a nonempty diagnostic at128 operations.
No exact wording is required. Database loading must not partially replace a valid
database when validation fails. Source equal-weight ordering follows the exported
relation lists, not alphabetical order or an invented stable-sort convention.

Use actual actor approach and distinct configured waypoints. Readiness is physical
arrival, not assigning an interaction or teleporting a character. Native default
waypoints are supplied; tests also explicitly configure synthetic actors. Preserve
initiator/listener and presence1(here),0(free),-1(elsewhere). Moving/following must
release old ownership. A fresh run has LT empty and RT holding the authored gun;
restart restores this. One item slot per character; inspection/criteria gate
pickup, occupied slots reject pickup, current speaker owns the item, and use moves
the item to local interaction state and empties the hand. World item visibility
tracks the facts and reverses correctly after restore.

Wait for the partner only when the authored narrative requests participation.
Do not infer that every window action needs both characters. A requested partner
must approach normally; resume queued actions after readiness. Preserve the
current contractor's immediate dialogue-pumping model rather than implementing
unfinished typewriter presentation. Closing releases the panel's pending work,
returns control, settles camera focus and uses autosave lifecycle. Pause/resume,
hover feedback, speaker/action/inventory labels and contextual choices must remain
connected to actual state. Text need not match a private sentence verbatim, but
authored dialogue text, action identity and speaker ownership are immutable.

Complete the real EntryGate rock/inspection/pickup/window/chair/gate sequence, then
travel to LoadingDocks with global/local facts and inventory intact. Complete the
docks kiosk, bay, pole, item transfer, wedge/gate/floodlight and valid final-exit
sequence through the same story APIs. Do not expose gated actions early or add
special-case replies for the evaluator's route. Native world effects update
visibility/collision, absolute transforms/hinge, light intensity and output facts;
repeated cycles and dead targets must be safe. At completion, settle both real
characters, pause and show the completion panel. Save/reload completion and allow
a new run. No evaluator may directly grant these facts as a substitute for play.

Preserve native version1 save semantics: pinned source identity, map, completion,
two positions/yaws, global facts and interaction-local facts. Encode deterministically
with sorted IDs; validate before mutation. Reject payloads over1MiB, more than4096
facts or256 interactions, IDs longer than128characters, wrong scope, unknown facts,
invalid held items, nonintegral/out-of-int32 facts, nonfinite/out-of-bounds poses,
unsupported versions/maps/source and malformed data. Missing old completed field
defaults false. The current source-map extension supports authored scene starts.
Staging restore must not mutate the live session; apply replaces rather than merges,
facts apply before actor initialization, and matching-map spawn state is consumed
once. Explicit named test slots are usable and isolated. Quick/manual and auto
slots differ; automation must not write a player's ordinary automatic save slot.

Rebuild the existing native Slate HUD and world hints: contextual inventory,
interaction action callbacks, pause/new-run/save/load controls and world-space
character/command/hover indicators. HUD remains an implementation requirement, but
rendered HUD scoring is excluded under ue-0172-valid-fixtures-v3; reward does not require
a GPU or renderer. A and B share the same integration route; their supplied
counterpart must actually execute, not be mocked.

## Boundaries and verification

Edit only the listed implementation files and marked restoration bodies/hunks.
Headers, component constructors, build metadata, authored maps/assets/config,
Narrative.json and captured inputs are supplied architecture/data. The other half
is supplied and must remain intact. Do not alter evaluator infrastructure or read
reference solutions. Generated outputs do not count as submitted implementations.
All three related tasks belong in one dataset partition and must be evaluated
without cross-task solver context.

The evaluator requires fresh source-world generation, subsystem diagnostics, an
uninterrupted real two-area gameplay/save sequence. Scoring revision
ue-0172-valid-fixtures-v3 awards the fraction of 45 checks passed (44 preparation/headless plus source preservation);
rendered HUD is excluded from both the denominator and mandatory gates.
Missing/duplicate tests, harness crashes or platform failures invalidate reward.
Full scoring success requires all 45 checks; partial credit does not establish
a complete game or assess rendered HUD quality.


# Public action/precondition/effect table

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

## Native inventory/participant callbacks and route consequences

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
| Open identified lit Bay2 | LT present, RT absent as this action requires, bay identified by source chain | Native completed flag, both characters settled, paused completion panel; no extra hidden participant requirement | Yes; explicit save/reload restores completion |

This table does not replace the complete narrative. Process response-trigger lists
and their conditions, including alternate eligible choices. An empty direct-effect
cell does not mean no effect: the authored response chain runs through ordinary
Apply/Invoke callbacks. Do not hard-code these route IDs in production.


Scoring revision `ue-0172-valid-fixtures-v3`: 45 equally weighted checks (5 preparation, 39 headless, and 1 supplied-code preservation check). Changes to protected C++ source or supplied functions, including formatting/comments, fail only the preservation check and do not block behavioral testing. Immutable data, scripts, build settings, and execution validity remain required. HUD rendering is excluded. Existing 44-check saved-report grades remain historical; rejected submissions require native reevaluation.


Valid synthetic story and initialized session fixtures; typed save-corruption probes do not require the private players wire key. Source preservation remains one of 45 checks, HUD is excluded. Existing grades retain their historical evaluator revisions.
