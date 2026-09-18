# Blind review of room displays

WidgetControls and EndpointWidgets capture PNGs of the actual mounted Slate window that receives input. Required status checkpoints in the lifecycle and recovery scenarios capture the receiving player's panel. The capture helper calls the stock FWidgetRenderer DrawWindow overload, reads the render target, and saves PNG bytes. Custom-painted controls provide the same pixel evidence as ordinary text widgets. Reflected text remains diagnostic and cannot substitute for these images.

The evaluator copies each PNG into the trusted review directory as SHA256.png and verifies any preexisting hash-named file contains identical bytes. Blind requests point only to this retained copy; later mutation of the original capture cannot change review evidence.

The private observation rows contain an image path, a semantic field and its expected value. The blind request contains only the PNG path and SHA-256, the field definition, the requested JSON value type and a request hash binding all reviewer-visible schema, field, rubric, instructions, value type and pixels. Only the absolute image path is excluded, so immutable pixels can move to the reader's host. It contains no expected value, triggering action, frame ledger or public-state data. The evaluator publishes all requests before its global wait of 90 seconds. Set OT_ROOM_UI_REVIEWS to a grader-owned directory outside candidate-write access. Missing, unreadable, malformed or mismatched observations fail.

The reader reports the value actually visible in the image. Boolean fields cover locks, combat pause, buffering, integrity, recovery, password masking and operation availability. Role and mode use the neutral enum values named in the request. Cursor and bounds return integer frame counts at 60 Hz; unambiguous displayed timecode or elapsed-time units may be converted. Room, match and assignment identities use the identifying label actually displayed. Friendly names and abbreviations are valid. The evaluator checks readable presence and distinction between different public identities. The same identity may use abbreviated and full labels at different checkpoints. It does not prescribe GUID spelling. Editable request fields and action labels alone are not evidence of current status.

Disabled controls can communicate that a requested operation is unavailable. The export-pending and replay-request-unavailable fields accept this when the visible context communicates the relevant state; they do not require dispatch of an unavailable action. Password masking is captured after entry and before submission. Its image reading asks whether the identifiable visible secret-entry control exposes entered contents. Blank echo suppression, a constant placeholder and concealment glyphs are valid; field labels or placeholder text alone are not secret disclosure. The image need not prove that a value was entered or retained. Actual input, room creation and authentication checks establish behavior separately and remain unchanged. An absent or visually indeterminate control is unreadable.

Record a reading using the CLI. Actual values are JSON booleans, integers or strings, or the literal unreadable:

```sh
python3 room_ui_review_cli.py --request /trusted/reviews/HASH.FIELD.request.json \
  --reviews /trusted/reviews --actual '20' --evidence 'The playback cursor reads 20.'
python3 room_ui_review_cli.py --request /trusted/reviews/HASH.FIELD.request.json \
  --reviews /trusted/reviews --actual '"paused"' --evidence 'The selected view says Paused.'
```

The CLI validates the canonical rubric, complete image/request binding and value type before atomically writing a verdict. It refuses to overwrite an existing observation. The evaluator verifies the image hash again when loading the verdict. A readable=false observation does not pass even when its value happens to match the expected value.

Native widget runs save RoomUIDisplays JSON and PNG files under Saved/Automation. Endpoint runs save display-observations.json. Lifecycle and recovery runs save status-display-observations.json. These private manifests include expected values and must stay outside the reader's packet. PNG names contain only random identifiers. The native room-display events include worker role, PID, PNG path and capture duration for TEST-only runtime measurement.

For re-evaluation, preserve the original captured bytes and run room_ui_review.py with --observations pointing to the private manifest, --reviews pointing to the trusted verdict directory and --wait-seconds 0. A supplemental verdict for an immutable capture does not erase an earlier missing-review failure. Legacy text-only rows remain readable by the tool for old artifacts, but the new required checkpoints emit image rows.
