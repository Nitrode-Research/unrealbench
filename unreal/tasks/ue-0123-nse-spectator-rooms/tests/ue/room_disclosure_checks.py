"""Disclosure checks for a scenario that has not completed or interrupted its match."""


def active_delivery_disclosure_errors(delivery, tick, delay, confirmed_ticks,
                                      expected_pairs):
    errors = []
    frame = delivery.get("frame", -1)
    pair = (delivery.get("input1"), delivery.get("input2"))
    if delivery.get("replay") or delivery.get("export_bytes"):
        errors.append("active match disclosed export bytes")
    if delivery.get("outcome"):
        errors.append("active match disclosed an outcome")
    if frame < 0:
        if delivery.get("gameplay"):
            errors.append("buffered delivery disclosed gameplay bytes")
        return errors
    if (tick is None or frame not in confirmed_ticks
            or confirmed_ticks[frame] + delay > tick):
        errors.append("selected frame precedes its confirmed release boundary")
    if delivery.get("gameplay") and frame > 0:
        if frame not in expected_pairs or pair != expected_pairs[frame]:
            errors.append("direct input pair differs from independently submitted selected frame")
    return errors
