"""Hourly energy from recorded power (left-step integration) or energy meters."""
import math

POWER_UNITS = {"W": 0.001, "kW": 1, "mW": 0.000001, "MW": 1000}
ENERGY_UNITS = {"Wh": 0.001, "kWh": 1, "MWh": 1000, "J": 1 / 3600000, "kJ": 1 / 3600, "MJ": 1 / 3.6}
ROLES = {name: "hmh dashboard energia " + name for name in ("ingresso", "produzione", "immissione", "carica", "scarica")}

def hourly_energy(states, edges, end, unit, state_class=""):
    """Return kWh and coverage; unavailable segments remain missing, never zero."""
    values = [0.0] * (len(edges) - 1)
    coverage = [0.0] * len(values)
    power = unit in POWER_UNITS
    factor = (POWER_UNITS if power else ENERGY_UNITS).get(unit)
    if factor is None: return [None] * len(values)
    samples = []
    for state in states:
        try:
            value = float(state.state)
            if not math.isfinite(value) or value < 0: value = None
        except (ValueError, TypeError): value = None
        samples.append((state.last_updated.timestamp(), value))
    samples.sort(key=lambda s: s[0])
    stop = end.timestamp()
    for i, (start, value) in enumerate(samples):
        if value is None: continue
        finish, following = samples[i + 1] if i + 1 < len(samples) else (stop, None)
        finish = min(stop, finish)
        if finish <= start: continue
        if not power:
            if following is None: continue
            delta = following - value
            if delta < 0:
                if state_class == "total_increasing": delta = following
                else: continue
        for j in range(len(values)):
            left, right = max(start, edges[j]), min(finish, edges[j + 1], stop)
            if right <= left: continue
            seconds = right - left
            values[j] += (value * seconds / 3600 if power else delta * seconds / (finish - start)) * factor
            coverage[j] += seconds
    result = []
    for j, value in enumerate(values):
        expected = max(0, min(edges[j + 1], stop) - edges[j])
        result.append(round(value, 5) if expected > 0 and coverage[j] >= expected - 1 else None)
    return result

def sum_series(series, count):
    return [round(sum(row[i] for row in series), 5) if series and all(row[i] is not None for row in series) else None for i in range(count)]
