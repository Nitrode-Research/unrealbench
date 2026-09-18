# Spectator room public interface

`Network/SpectatorRoom.h` declares the callable room, connection, delivery and replay types. The starter provides inert implementations. Implement the task behavior behind these operations; storage layout, caches and object allocation are implementation choices.

`Create(slot, organizer, secret, delay)` creates an authority room with delay from 0 through 600. `Recover(slot)` restores that room. The slot is a persistent identity. `SetClock` supplies elapsed 60 Hz room ticks for callers with an external clock; normal operation must advance room time through pauses, travel and downtime.

Authenticate through the owning `URoomConnection` with identity, secret and alternating content path/revision strings. `InspectContent` and `InspectRequiredContent` inspect installed content. Reauthentication with corrected content retries access. Submit `FRoomCommand` through the owning connection. Mutations carry unique `Nonce` values; `RequestNonce` correlates the response. `Membership` identifies an acknowledged join generation. `observe` is read-only. Its response echoes a supplied `Nonce` as `RequestNonce` without consuming it or changing room state.

| Operation | Arguments |
| --- | --- |
| character | Value is a primary character asset path; Assignment is own seat assignment |
| stage | Value is a primary stage asset path |
| lock, unlock, combat-pause, combat-resume, start | Organizer commands |
| ready | Assignment is own current seat assignment; Match is the latest match identity, empty before the first match |
| queue, withdraw | Own spectator queue membership |
| offer | Value is a queued identity; Number is vacant seat 0 or 1 |
| accept, decline | Assignment is the Offer identity returned in own delivery |
| input | Match, Assignment, Number for the next accepted frame, Value containing decimal input bits |
| select-match | Match is a retained timeline |
| seek | Match is selected timeline, Number is requested frame |
| pause, resume, live | Own playback mode |
| export | Match is retained timeline |

`LastDelivery` contains the caller's status, identities, assignment, offer, mode, cursor, released edge, locks, pause, acknowledgement, roster, required content and released gameplay/outcome/export. `OnTransportDelivery` exposes the complete decoded application payload received by that connection. `OnDelivery` reports validated presentation. Input acknowledgements may omit gameplay; omission leaves the current presentation unchanged.

`DecodeConfirmedInputs` decodes a nonempty `FRoomFrame` into its match identity, both complete confirmed input sequences and expected gameplay description, returning false on invalid data. This is also the semantic decoding operation used by playback. The input sequences end at the represented frame. No wire encoding is prescribed.

`BindBattle` associates the actual initialized battle. `PlayFrame` presents a confirmed frame in a matching initialized battle. `PresentDelivery`, `PresentationBattle` and `PresentationTexture` provide the selected match's actual gameplay and image. Timeline changes reproduce the selected stage, characters and recorded state while preserving the room connection. World and component allocation identities are unrestricted.

`SavedReplays` lists exported identities, `ReadReplay` inspects an artifact, and `PlaySavedReplay` opens it through normal replay playback. Artifact bytes must detect corruption and remain usable after restart.

`ServerPredictionPacket` and `ClientPredictionPacket` carry fighter prediction traffic. `PredictionFrame`, `PredictionConfirmedFrame` and `PredictionRollbacks` report actual native simulation progress and corrections. Only current authenticated opposite seats in the same match and assignments may exchange fighter traffic.

`URoomPanel` exposes editable controls named `Identity`, `Secret`, `Slot`, `Delay`, `Address`, `Value`, `Frame` and `Match`; Secret is a password field. Buttons use `Action_` followed by the operation name. Additional operations are create, authenticate, host, connect, leave, retry-content, list-replays and play-replay. `RoomStatus` displays the returned status. Host starts a listen endpoint; connect uses Address and preserves credentials through travel. Replay playback uses the identity in Value. The local player receives usable controls automatically, including after travel.

`URoomConnection::AuthorityDelivery` observes the actual outgoing authority delivery before transport. It is read-only and reports the same correlated application payload; it grants no additional command authority.

A successful start establishes the new match identity, its start time and a reproducible initialized frame 0. Travel or live battle-world setup may still be pending; success does not merely acknowledge a queued start. If a fighter departs or authority recovery occurs after that success and before any input pair is confirmed, retain an interrupted match whose terminal frame is 0 and whose confirmed input history is empty. It remains viewable and exportable under the ordinary retention, content validation and release rules: frame 0 uses the original match start time, and the outcome uses the specified interruption time. Preparing a start may remain pending until a reproducible frame 0 is established; no particular initialization or storage sequence is required.
