# Independent HUD review for task124

The scored `NightSkyIntegration.Relay.RenderedSample` test first checks combat,
camera and rendered scene evidence. A compatible textual HUD observation can
complete the HUD checks automatically. If that text cannot establish the HUD
semantics, the existing driver requests independent screenshot observations.
There is no built-in review provider, API-key setting or automatic reviewer.
Arrange a trusted human or independently operated image reviewer before starting
the verifier. The candidate must not author its own review response.

Capture and review share the original **360-second scenario deadline**, measured
from launch of the scenario Python driver, not from creation of the review packet.
Capture can consume much of this time. Reviewing 19 screenshots manually within
the remaining time may be impractical; prepare the review process in advance.
Do not extend the deadline or replace assertions to obtain a pass.

## Submit observations during the running verifier

The commands below run on the Docker host. Set `verifier` to the container name
for this run's separate task124 verifier (identify it with `docker ps`). Keep the
normal benchmark run active while using a second terminal. These commands require
local Docker access, not AWS or a private service.

Watch for the blind request in the live container:

```sh
docker exec "$verifier" find /project/Saved/Automation/Relay \
  -path '*/hud-review-input/hud-review-packet.json'
```

The directory may not exist until this scenario starts. Set `capture` to the
enclosing scenario directory from this run, for example
`/project/Saved/Automation/Relay/relay_rendered_sample_<GUID>`. Confirm that
`docker exec "$verifier" cat "$capture/result.json"` reports
`"status": "visual_review_pending"` and `"automatic_checks_passed": true`.
The packet is ready at that point. Do not rely on the Unreal info message arriving
in the host log early enough for review.

Copy only the blind packet and images to a fresh local directory:

```sh
docker cp "${verifier}:${capture}/hud-review-input" ./hud-review-input
```

Give the reviewer only `hud-review-input/`. Do not provide evaluator state, CSV
files, text diagnostics, test or solution source, or expected answers. Follow
`hud-review-packet.json`'s instructions and response shape. Every one of its 19
frames needs its exact image SHA-256, a description of visible evidence, both
teams and all three immutable selection slots. Report observed team resource and
rejection meaning, and each slot's role, health, recoverable health, cooldown and
eligibility. Numeric text is a number. A graphical meter is an object such as
`{"fraction": 0.5, "resolution_pixels": 120}`, using its measured full-scale
pixel length. Use `null` for absent or unreadable evidence; never infer it from
expected gameplay. The response contains observations, not a pass/fail verdict.

Save the independent response outside the blind directory as
`trusted-observations.json`. Make it readable by the verifier's `ue4` user, then:

```sh
docker cp ./trusted-observations.json \
  "${verifier}:/tmp/trusted-hud-review.json"
docker exec --user ue4 "$verifier" python3 \
  /tests/ue/relay_rendered_sample.py \
  /project/NightSkyEngine.uproject "$capture" \
  --review-only --review-result /tmp/trusted-hud-review.json
```

The shipped helper checks the original image hashes, the automatic-check result
and every observation against evaluator-only state. It atomically updates
`result.json`; the waiting Unreal test consumes that result. Finish before the
original deadline. A later successful review cannot retroactively pass the expired
Unreal test.

## Results and limitations

The exact Unreal error `HUD image review deadline expired; evidence: ...` means
the required independent review did not finish in time. Task124's entrypoint
withholds both the overall and rendered-suite reward files, writes
`reward_valid: false`, `reward: null`, and
`status: "independent_review_unavailable"` to `verification.json`, and exits 2.
Existing logs and score JSON remain diagnostics, not a qualified benchmark score.
The earlier pending-review info message alone does not invalidate a run that
subsequently completes review. A completed review observing incorrect HUD values
remains an ordinary scored failure.

The existing helper uses `visual_review_failed` both for incorrect observations
and for malformed, unreadable or missing response files supplied to
`--review-result`. This narrow guard does not distinguish those cases. Validate
response transport and shape before submission; inspect the helper's error when
review fails. Do not invoke review-only with a missing response just to acknowledge
the request.

Scenario images and review results currently live under `/project/Saved`, which
the standard submission artifact excludes. They are not automatically preserved
after container teardown. Copy the blind packet, response and relevant evaluation
records while the verifier is alive if an external adjudicator needs them. Keep
evaluator-only records separate from the reviewer's blind input. This document
does not add an evidence exporter or change regrade behavior.

For a fresh import with `scripts/package_website_nse.py`, retain a trusted copy of
this task's `tests/task124_verify.py` outside the destination first and pass it as
`--task124-review-wrapper PATH`. The importer refuses an existing destination and
requires that explicit wrapper when importing task124, so regeneration cannot
silently discard the review guard.
