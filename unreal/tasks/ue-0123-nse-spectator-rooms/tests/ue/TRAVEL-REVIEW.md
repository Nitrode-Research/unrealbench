# Explicit same-map travel readiness

GGPOPair deliberately invokes the fixture's native ServerTravel from Entry to Entry. The old readiness predicate accepted any subsequent membership event naming Entry, including an old-world heartbeat before travel executed. The protected trace shows command 354 returning from the original authority world at 16848414.2537, then seek 355 executing in the original client world at 16848414.3583; later new-world observations remain on frame 32. This demonstrates a setup race, not a content rejection.

The fixture counts engine PostLoadMapWithWorld callbacks belonging to its own game instance. Before explicit travel, the driver snapshots each process's count. It waits for that count to advance, the expected map, an owning channel and acknowledged membership before submitting the existing seek to frame 33. Actual travel, durable membership equality and exact seek assertions remain, with the same 60-second travel/30-second operation bounds. No presentation-world allocation identity is required.

Python compilation and 11 host-only scheduling tests pass. Engine callback compilation and the focused GGPOPair run require protected EC2 validation.
