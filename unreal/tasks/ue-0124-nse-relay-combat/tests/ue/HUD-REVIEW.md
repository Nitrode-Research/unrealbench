# Graphical HUD evidence

The rendered driver accepts a compatible text observation automatically. All other HUD layouts use screenshot observations from a trusted independent image reviewer. Neither path requires `URelayOverlay` or `PaintedText`.

Run the normal rendered test on EC2. When automatic combat and camera checks pass but text evidence cannot express the HUD, the driver creates `hud-review-input/` and returns `visual_review_pending`. The Unreal latent command logs that path and waits until its original deadline. No review means no pass.

Give the reviewer only `hud-review-input/`, which contains screenshots and `hud-review-packet.json`. Do not give it `hud-evaluator-state.json`, CSV logs, test source, automatic text diagnostics, solution source, or expected answers. The packet supplies field meanings, selected maximum health for meter interpretation, and neutral action context. The reviewer must inspect the images and return observed fields using the supplied response shape. It must not return a pass/fail assessment or infer absent values. Unreadable or absent evidence is `null`.

Review observations must include every frame, image SHA-256, both teams, all three immutable slots, and visible evidence descriptions. Numeric text is compared exactly. Graphical meters report their observed fill fraction and measured full-scale pixel length. The evaluator allows one pixel of quantization; absent meters and empty/nonempty contradictions fail. Roles, eligibility, and rejection meanings are compared independently of labels or layout.

Save the trusted observations outside `hud-review-input/`, then run on the evidence host:

```sh
python3 /path/to/tests/relay_rendered_sample.py \
  /path/to/NightSkyEngine.uproject /path/to/Automation/Relay/capture-directory \
  --review-only --review-result /path/to/trusted-observations.json
```

This command checks the original screenshot hashes and automatic-check result, evaluates the independent observations against evaluator-only state, and atomically updates `result.json`. The waiting Unreal command consumes that result. If the original automation deadline already expired, the reviewed artifact remains usable for an external adjudicator, but it does not retroactively turn the expired Unreal test into a pass. Capture and review must complete within the automation deadline for that registration to pass.

The reviewer is part of the trusted evaluation process. Candidate output must never be accepted as the review response. The helper tests verify valid observations and rejection of wrong image hashes, missing fields, wrong numeric values, invalid fractions and absent meters.
