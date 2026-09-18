# Replay takeover with independent branch recording

While watching a valid NightSkyEngine replay locally—for example, during training—let the user take control of P1 or P2 and save the continuation as a separate playable replay. Support takeover at the current position or a requested position reached without intermediate presentation. Position K means K recorded input pairs have run; takeover changes frame K before it runs. For a source of L >= 0 pairs with compatible version and available assets, accept integer positions 0 through L. Invalid positions/sides, repeated takeover, and seeking after takeover reject without changing playback, ownership or recordings.

Before K, both sides use recorded inputs. From K, only the selected side uses its local controller; the other uses the source until L, then no input at all (`INP_None`, zero — not the named `INP_Neutral` flag). Preserve the battle at K, including buffered combat inputs and subsequent random outcomes. After takeover, strip rematch/training-reset commands from both sides' effective inputs, including inputs still fed from the source, and never record them in the branch's input pairs; preserve ordinary held combat buttons. Ordinary playback stops in-world at L and remains available for takeover. Branch play continues until finish or match end. Pause advances neither battle nor recording; finishing is idempotent, saves once, and stops advancement. Fresh sessions clear takeover ownership.

Save the original initialization and input prefix, the effective input pairs after K, and K/side metadata under an unused replay identity. Never replace an existing save: if no unused identity is available, finishing rejects instead of overwriting. Preserve the source in memory and on disk. The saved branch must play independently without the source. Catch-up produces the same destination and subsequent gameplay as ordinary playback, displays only the destination, resumes normal presentation, and emits no skipped or repeated visuals, camera/HUD updates, collision debug drawing or audio. The destination presentation it restores includes the collision debug view when that view is enabled. Effects already alive before a seek remain at the destination, aged as in ordinary playback; only effects that would have started in skipped frames are omitted.

Edit only files inside the task's editable scope. An attempt that changes anything outside it is rejected without being scored.

## Editable paths

You may add or modify files within these paths:

- `Source`
- `Config`
- `Content`
- `Plugins`
- `NightSkyEngine.uproject`

## Protected paths

Do not change these paths, including when nested within an editable path:

- `Plugins/NightSkyEngine/Source/NightSkyEngine/Fixtures`
- `REPLAY_INTERFACE.md`
