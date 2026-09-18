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
