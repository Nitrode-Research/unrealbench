# Blind review of rendered room gameplay

`NightSky.Room.RenderedDVR` saves BMP images read directly from the published `PresentationTexture()`. The independent ordinary battle uses its own camera and 960 by 540 render target. Candidate resolution, camera pose, pixel alignment, Niagara assets, particle counts and desired-age controls do not determine image equivalence.

The test preserves native gameplay checks and actual sound callbacks. It captures the destination and continuation at frames 15, 16, 20, 30, 31, 40 and 100. Both ordinary and presented sequences must visibly contain readable gameplay and effects, and at least two observably different effect phases. The sequences must depict equivalent gameplay and effect phase at corresponding samples. The duplicate sequence contains the destination before repeated delivery and two later samples separated by engine updates. It must preserve gameplay and effect phase without a restart, disappearance or new duplicate effect. Review ignores unrelated UI motion and rendering variation.

The evaluator copies actual image bytes into a trusted review directory under their SHA-256 names. A request contains only these images in chronological sequence, semantic field definitions, and a hash binding the entire request. It omits private group names, frame numbers, triggering actions, expected values and reference/candidate labels. The order of the two sequences depends only on their image hashes. The reviewer must open every supplied image, read the actual visible meaning, and describe concrete evidence with sample indices. An image that hides the relevant gameplay or makes the phase unreadable cannot establish equivalence. Different framing, crop, resolution and rendering methods are valid when the requested facts remain readable.

Set `OT_ROOM_RENDER_REVIEWS` to a trusted grader-owned directory outside candidate-write access. Only that directory's request JSON and BMP files belong in the reviewer's packet. Do not give the reviewer the private manifest, implementation, triggering-action logs, result comparisons, or ordinary/presented filenames. All requests are published before a single 90-second wait. There is no automatic image classifier or assumed external model service. Missing, malformed, changed or unreadable evidence fails. The default local directory supports capture and offline review; grading requires trusted review storage and an independent reader.

After inspecting the actual images, record one field-bound observation:

```sh
python3 room_render_review_cli.py --request /trusted/reviews/HASH.request.json \
  --reviews /trusted/reviews --actual true \
  --evidence 'At indices 0 and 1 the effect remains beside the same projectile; its shape and decay stage remain visibly consistent.'
```

`--actual` accepts `true`, `false`, or `unreadable`. Describe the observed evidence rather than copying the example. Existing verdicts cannot be overwritten by the CLI. The evaluator compares the independently observed boolean with its private expected value only after review. A completed capture can be reviewed without rerunning Unreal:

```sh
python3 room_render_review.py /path/Project.uproject /path/re-evaluation \
  --observations /path/RoomRendered-ID/private-manifest.json \
  --reviews /trusted/reviews --wait-seconds 0
```

These finite samples test visible phase restoration, continuation and repeated delivery. They do not prove all possible effects or renderers correct, nor admit every conceivable implementation.

Retained-stage checks capture each original authority at frame 40 before teardown. Standalone reference worlds receive the same authored alternate-stage scenery when map travel has not already supplied it, after combat has finished. This reference-only setup never inspects the candidate presentation. The checks then compare the retained published image with that reference. A positive control requires visible scenery differences between the two authored stages. Each retained image must show the same stage and gameplay as its original reference. The check no longer counts candidate mesh actors, assumes camera projection, or compares a fixed pixel crop. Stage comparison compares visible world regions, permits matching empty scenery and does not require any particular off-camera landmark. Public selection and exported configuration checks retain the selected match, stage and characters. Native readiness uses independently recorded frame, position and health, without reading mutable setup data. Images must show readable gameplay in a compatible visible setting. If the chosen framing makes two stage implementations observably identical, this image check does not claim to distinguish their hidden stage identity.
