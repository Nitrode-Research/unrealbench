# Persistent spectator rooms

Provide a persistent NightSkyEngine room for two fighters, an authenticated organizer and three independent spectators, with player-facing selection and spectator controls. Viewers can join an ongoing battle, navigate delayed playback, restart/reconnect, and remain through selection, stage travel and rematches. Spectators cannot control fighters, alter results or stall combat.

Room, match and seat assignments have distinct identities. Frame 0 is initialized state; frame n follows n confirmed input pairs. The organizer chooses immutable delay D from 0–600 ticks at creation. Ticks are elapsed 1/60 seconds, including pauses, travel and downtime. Gameplay becomes available D ticks after durable confirmation; frame 0 uses match start. Outcomes use the deciding frame's confirmation for normal completion, accepted disconnect time for fighter interruption, or durable recovery time for authority interruption. Repeated events never reset these times or the terminal frame's original confirmation. Before release show buffering. No spectator input data, presentation, result, metadata or export may reveal unreleased gameplay; membership/selection notices may be immediate.

Retain the current and two latest finished matches, including interruptions. Viewers seek within 0 through the released edge, pause, resume one frame per 60 Hz playback tick, or follow live at that edge. Join starts there. Rejected requests leave the view unchanged. Reconnect restores retained match/cursor/mode; otherwise show history unavailable until a match is chosen. Match boundaries do not switch selected timelines; expiration stops playback. Seeking restores gameplay without skipped sounds or repeated effects.

Between matches fighters select characters and the organizer selects the stage. Only the organizer locks/unlocks selection, pauses/resumes combat, offers vacant seats to queued spectators, and starts when both current fighters are ready. Locked changes reject. Combat pause stops simulation and gameplay clocks while room controls and delayed viewing continue. Queue membership is unique and withdrawable. Offer acceptance transfers a seat once; decline preserves spectator role. Departure, reassignment or match start expires offers. Stale/duplicate/wrong-owner inputs cannot affect combat. Fighter departure interrupts at the last confirmed frame and vacates the seat; no mid-match promotion occurs.

Validate character, stage and gameplay-content revisions before viewing or playing. Missing/mismatched content gives a recoverable diagnostic without disrupting healthy clients. Invalid fighter content blocks readiness/start; correction and retry restore access. Transfers resume without gaps or duplicates. Display role, identities, mode, cursor/bounds, buffering, locks, pause, integrity and recovery status. Divergent history stops that viewer with an integrity error.

Export complete retained matches only after terminal frame and outcome release; earlier requests report pending. Include pre-join history, configuration/content revisions, participants, confirmed inputs and completed/interrupted outcome. Saved replays survive restart, detect corruption and reproduce offline. Authority restart restores acknowledged membership, locks, queue and completed history; an active match becomes interrupted. Reauthentication restores reserved roles without duplicate owners, and subsequent matches receive new identities.

A successful start establishes the new match identity, its start time and a reproducible initialized frame 0. Travel or live battle-world setup may still be pending; success does not merely acknowledge a queued start. If a fighter departs or authority recovery occurs after that success and before any input pair is confirmed, retain an interrupted match whose terminal frame is 0 and whose confirmed input history is empty. It remains viewable and exportable under the ordinary retention, content validation and release rules: frame 0 uses the original match start time, and the outcome uses the specified interruption time. Preparing a start may remain pending until a reproducible frame 0 is established; no particular initialization or storage sequence is required.

## Editable paths

You may add or modify files within these paths:

- `Plugins/NightSkyEngine/Source`
- `Source`

## Protected paths

Do not change these paths, including when nested within an editable path:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Fixtures`
