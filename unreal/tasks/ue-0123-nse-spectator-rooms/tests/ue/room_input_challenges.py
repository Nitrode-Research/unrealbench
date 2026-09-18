"""Bounded direct-copy disclosure witness, with a stated accidental-match bound."""
import math
import secrets

CHALLENGE_FRAMES = range(21, 53)
INPUT_DOMAIN_ONE = (0, 8, 32, 40)
INPUT_DOMAIN_TWO = (0, 4, 32, 36)


def make_initial_inputs():
    pairs = [[8 | (32 if frame % 20 < 8 else 0) | (64 if frame in (20, 120) else 0),
              4 if frame % 17 < 4 else 0] for frame in range(1, 161)]
    for frame in CHALLENGE_FRAMES:
        pairs[frame - 1] = [secrets.choice(INPUT_DOMAIN_ONE), secrets.choice(INPUT_DOMAIN_TWO)]
    return pairs


def binomial_tail(trials, minimum, probability):
    return sum(math.comb(trials, hits) * probability ** hits * (1 - probability) ** (trials - hits)
               for hits in range(minimum, trials + 1))


def direct_copy_evidence(rows):
    if len(rows) != len(CHALLENGE_FRAMES):
        raise ValueError("Direct-copy witness needs all 32 predetermined challenge frames")
    if [row["frame"] for row in rows] != list(CHALLENGE_FRAMES):
        raise ValueError("Direct-copy witness frames are incomplete or repeated")
    if any(row["submitted"][0] not in INPUT_DOMAIN_ONE or row["submitted"][1] not in INPUT_DOMAIN_TWO
           for row in rows):
        raise ValueError("Challenge pair is outside the declared independent domains")
    pair_hits = sum(tuple(row["observed"]) == tuple(row["submitted"]) for row in rows)
    one_hits = sum(row["observed"][0] == row["submitted"][0] for row in rows)
    two_hits = sum(row["observed"][1] == row["submitted"][1] for row in rows)
    # Under independence from hidden random values, arbitrary changing sentinels
    # have the same collision bound. No one-off coincidence or variation fails.
    bound = binomial_tail(32, 24, 1 / 16) + 2 * binomial_tail(32, 31, 1 / 4)
    return dict(suspected_direct_copy=pair_hits >= 24 or one_hits >= 31 or two_hits >= 31,
                pairs=32, pair_hits=pair_hits, player1_hits=one_hits, player2_hits=two_hits,
                accidental_match_bound=bound,
                assumption="Each hidden input component is chosen independently and uniformly from its four-value domain; observations do not depend on those hidden values.")
