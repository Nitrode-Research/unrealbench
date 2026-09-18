# Relay public interface

The executable sample entry is `RelaySampleSlots 1 2 3 1 2 3 true`, a Blueprint-callable console command on the game instance. Its six numbers select ordered team slots; 1 is Vanguard with 10000 maximum health, 2 is Heavy with 12000, and 3 is Light with 9000. The last value chooses lethal training. The command travels to the engine Entry map with the native sample game mode. In that match, Left/Right move, A strikes, S throws, 1/2/3 issue relay commands, and T restarts training. Sample character assets are native class default objects with stable script paths, so saved roster references can resolve in a fresh process.

`SetInitialRelayResource(IsP1, Amount)` configures 0–200 resource before the first gameplay frame and returns true when accepted. Existing `SetTeamCooldown(IsP1, zeroBasedSlot, Frames)` configures a fighter cooldown.

Select `EBattleFormat::Relay` in `UNightSkyGameInstance::BattleData` and place exactly three `UPrimaryCharaData` assets into each ordered PlayerList. Order is selection slot 1–3 and remains fixed through handoff. The normal battle map and `MatchInit` start the selected roster. `IsTraining=true` enables training; relay training permits lethal damage.

`ANightSkyPlayerController::RelayButton(Slot, Held)` is a Blueprint-callable and console command binding. Call with true on press and false on release. The normal runner input word uses `RelaySlot1`, `RelaySlot2`, and `RelaySlot3`, also published as `INP_Relay1`–`INP_Relay3`. Ordinary input flags retain their existing meanings. Feed these words to `UpdateGameState(P1, P2, Resimulating)` through local, GGPO or replay runners.

`ConfigureRelayMoves(SynchronizedMove, FollowupMove)` supplies character state tags. Every selected fighter must implement the required authored state. Missing authored moves reject eligibility before payment. Native sample content in `Fixtures/RelayFixture` publishes `State.Relay.Synchronized`, `State.Relay.Followup` and `State.Relay.Projectile`.

`GetRelaySlots(IsP1)` returns ordered identities (the character's `CharaName`), main/visible roles, current and recoverable health, fighter cooldown and current eligibility. `RelayEligibility(IsP1, Slot)` returns a semantic rejection reason. `GetRelayStatus(IsP1)` returns the current named gameplay `Stage`, its `ElapsedFrames`, team `Resource` and last `Rejection`. `ElapsedFrames` is 0 on the frame a stage is entered (the acceptance frame reports `Entry` with 0) and increases by one on each later gameplay frame. These describe gameplay independently of internal scheduling. `CurrentWinSide` reports the result. Existing actor position, current state, AttackOwner, Player, health and combo observations remain available.

The native `URelayOverlay` paints the current slot and resource values. Camera targets include visible fighters. 

Use ordinary `INP_ResetTraining` and paired `INP_Rematch` inputs for player-facing reset. `RoundInit` is also public. Reset suppresses a relay button held on its input frame until release. `SaveGameState`/`LoadGameState` and existing replay record/playback APIs retain the same contract.

## Rollback

Relay state must survive rollback. The engine checkpoints a battle with `SaveGameState` and restores
it with `LoadGameState`, and the tests call both directly: a checkpoint is taken, relay traffic runs,
and the checkpoint is restored repeatedly. After a restore the battle must continue exactly as it
would have from the checkpoint, including every field the relay rules read.

What is captured is fixed by the engine, not by the field's type:

- `FBattleState` is copied as raw bytes between its `BattleStateSync` and `BattleStateSyncEnd`
  members (`SizeOfBattleState`). A member placed between those markers is copied bitwise, so it must
  be trivially copyable: a `TArray`, `TMap`, `FString` or pointer placed there is restored as a stale
  address. A member placed after `BattleStateSyncEnd` is not rolled back at all and keeps whatever
  value it had at restore time, which is why the engine's own gauge arrays sit there.
- Battle objects and players are captured the same way, between `ObjSync`/`ObjSyncEnd` and
  `PlayerSync`/`PlayerSyncEnd` (`SizeOfBattleObject`, `SizeOfPlayerObject`). The matching log structs
  are checked against those sizes at compile time, so a new member between the markers must be added
  to both.
- Anything outside those regions is restored only if the implementation restores it explicitly.
  `SaveForRollbackBP`/`LoadForRollbackBP` serialize `UPROPERTY(SaveGame)` fields and handle
  containers correctly; state kept on the game state actor itself is not rolled back unless the
  implementation saves and restores it.

Restoring a checkpoint must not leave dangling allocations or double-free them. A relay
implementation that violates this usually does not fail an assertion: it corrupts the heap and the
editor crashes later, somewhere unrelated to the mistake.

## Native sample move data

The native sample fighter has a visible cube and fixed hurt box 90000 by 200000, centered at height 100000. Its melee hit box is 350000 by 200000, centered 180000 units forward and 100000 high. Ordinary A strike starts an authored 20-frame playback, active at remaining playback ages 17–14, for 500 base damage, 3 hitstop and 8 hitstun.

Synchronized strike has 400 base damage, 3 hitstop, 18 hitstun, 80% forced proration and 25% recoverable damage. It is active at move playback ages 2–4 and ends at age 10. Slot 2 also launches the authored projectile at move entry. The projectile moves 6000 units per ordinary playback frame, has 300 base damage and a 90-frame maximum lifetime. Follow-up uses the same melee timing with 700 base damage. These are ordinary combat states and collision data. They contain no relay scheduling, eligibility, resource, role, recovery or interruption logic.


Ordinary sample button C, keyboard D, requests a six-frame defensive burst with a sixty-gameplay-frame reuse interval. It is available from standing or hit reaction. Native authored states support Blueprint and C++ initialization through `UState::Init`.

`RemoteInputDeliveryGate` optionally receives batches of real remote input changes awaiting gameplay delivery. Each `FRemoteInputChange` gives the input frame and newly pressed button bits. Returning false retains that pending batch without delivering or discarding it; it is offered again until allowed. An empty gate leaves ordinary delivery unchanged. `ConfirmedInputFrame()` reports the last frame whose actual inputs from both peers are known. These public controls and observations may be supplied by any compatible transport; packet encoding and runner classes are unrestricted.

Super freeze is observed through `BattleState.SuperFreezeDuration` and `SuperFreezeSelfDuration`; a frame is frozen when either counter is nonzero as the frame begins, before the engine decrements it, so a counter set to N freezes the next N frames.

KO of a current relay participant also cancels that relay. KO of another exposed fighter does not cancel it unless the KO ends the match.
