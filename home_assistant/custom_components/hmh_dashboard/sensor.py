"""Publish areas, their real registry icons, and labeled entities to ESPHome."""
from __future__ import annotations

from datetime import timedelta, datetime, time
from functools import partial
import asyncio
import logging
from homeassistant.components.recorder import get_instance
from homeassistant.components.recorder import history
from homeassistant.util import dt as dt_util
from .energy import POWER_UNITS, ENERGY_UNITS, ROLES, hourly_energy, sum_series
import math
import unicodedata
from homeassistant.components.climate import ClimateEntityFeature
from homeassistant.components.cover import CoverEntityFeature
from homeassistant.components.fan import FanEntityFeature
from homeassistant.components.alarm_control_panel import AlarmControlPanelEntityFeature
import json
from pathlib import Path

import voluptuous as vol
from homeassistant.components.sensor import PLATFORM_SCHEMA, SensorEntity
from homeassistant.core import callback
from homeassistant.helpers import area_registry as ar
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import label_registry as lr
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.event import async_track_time_interval

CONF_LABEL = "label"
PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({vol.Optional(CONF_LABEL, default="HMH Dashboard"): cv.string})
DOMAINS = {"light", "switch", "input_boolean", "binary_sensor", "button", "sensor", "climate", "cover", "fan", "weather", "number", "input_number", "alarm_control_panel"}
WEATHER_CONDITIONS = {"sunny": "Sereno", "clear-night": "Notte serena", "cloudy": "Nuvoloso",
    "partlycloudy": "Parzialmente nuvoloso", "rainy": "Pioggia", "pouring": "Pioggia intensa",
    "snowy": "Neve", "snowy-rainy": "Neve e pioggia", "fog": "Nebbia",
    "hail": "Grandine", "lightning": "Temporale", "lightning-rainy": "Temporale con pioggia",
    "windy": "Ventoso", "windy-variant": "Nuvoloso e ventoso", "exceptional": "Meteo eccezionale"}

WEATHER_ICONS = {"sunny": "mdi:weather-sunny", "clear-night": "mdi:weather-night",
         "rainy": "mdi:weather-rainy", "pouring": "mdi:weather-pouring",
         "snowy": "mdi:weather-snowy", "fog": "mdi:weather-fog",
         "partlycloudy": "mdi:weather-partly-cloudy", "lightning": "mdi:weather-lightning"}

COLOR_MODES = {"rgb", "rgbw", "rgbww", "hs", "xy"}



def _history_points(states, start, end):
    """Bounded min/max sampling with explicit unavailable gaps."""
    duration = max(1, (end - start).total_seconds())
    buckets = [[] for _ in range(38)]
    for state in states:
        offset = max(0, min(duration, (state.last_updated - start).total_seconds()))
        try:
            value = float(state.state)
            if not math.isfinite(value): value = None
        except (ValueError, TypeError): value = None
        point = [int(offset), round(value, 4) if value is not None else None]
        buckets[min(37, int(offset * 38 / duration))].append(point)
    result = []
    for bucket in buckets:
        if not bucket: continue
        valid = [point for point in bucket if point[1] is not None]
        chosen = [bucket[0], bucket[-1]]
        if valid: chosen += [min(valid, key=lambda p: p[1]), max(valid, key=lambda p: p[1])]
        gap = next((point for point in bucket if point[1] is None), None)
        if gap: chosen.append(gap)
        for point in sorted(chosen, key=lambda p: p[0]):
            if not result or point != result[-1]: result.append(point)
    return result[:200]

async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    """Set up the read-only catalog from YAML."""
    # Small local resource, loaded off the event loop.
    def read_icons():
        return json.loads(Path(__file__).with_name("mdi_icons.json").read_text(encoding="utf-8"))
    icons = await hass.async_add_executor_job(read_icons)
    async_add_entities([DashboardCatalog(hass, config[CONF_LABEL], icons)], True)


class DashboardCatalog(SensorEntity):
    _attr_name = "HA Dashboard Catalog"
    _attr_unique_id = "hmh_dashboard_catalog"
    _attr_icon = "mdi:tablet-dashboard"
    _attr_should_poll = False

    def __init__(self, hass, label, icons):
        self._hass = hass
        self._label = label
        self._icons = icons
        self._selected_entities = set()
        self._cancel_timer = None
        self._energy_payload = "json:{}"
        self._energy_lock = asyncio.Lock()
        self._energy_generation = 0
        self._history_payload = "json:{}"
        self._history_lock = asyncio.Lock()
        self._history_generation = 0
        self._attr_native_value = 0
        self._attr_extra_state_attributes = {}

    async def async_update(self):
        self._build()

    async def async_added_to_hass(self):
        await super().async_added_to_hass()
        for event_type in ("area_registry_updated", "entity_registry_updated",
                           "device_registry_updated", "label_registry_updated"):
            self.async_on_remove(self._hass.bus.async_listen(event_type, self._registry_changed))
        self.async_on_remove(self._hass.bus.async_listen("state_changed", self._state_changed))
        self.async_on_remove(self._hass.bus.async_listen("esphome.hmh_dashboard_history", self._history_requested))
        self.async_on_remove(self._hass.bus.async_listen("esphome.hmh_dashboard_energy", self._energy_requested))
        self.async_on_remove(async_track_time_interval(self._hass, self._periodic, timedelta(minutes=1)))
        self.async_on_remove(self._cancel_pending)
        self._refresh()

    async def _history_requested(self, event):
        entity = str(event.data.get("entity_id", ""))
        request = str(event.data.get("request_id", ""))[:96]
        try: hours = int(event.data.get("hours", 24))
        except (TypeError, ValueError): return
        if entity not in self._selected_entities or not entity.startswith("sensor.") or hours not in {1, 24, 168} or not request: return
        self._history_generation += 1
        generation = self._history_generation
        async with self._history_lock:
            if generation != self._history_generation: return
            end = dt_util.utcnow();start = end - timedelta(hours=hours)
            response = {"id": entity, "request": request, "hours": hours, "duration": hours * 3600,
                        "start": dt_util.as_local(start).strftime("%d/%m %H:%M"),
                        "end": dt_util.as_local(end).strftime("%d/%m %H:%M"), "points": []}
            try:
                result = await get_instance(self._hass).async_add_executor_job(partial(
                    history.get_significant_states, self._hass, start, end, [entity],
                    include_start_time_state=True, significant_changes_only=True,
                    minimal_response=False, no_attributes=True))
                response["points"] = _history_points(result.get(entity, []), start, end)
                if not any(point[1] is not None for point in response["points"]): response["error"] = "Nessun dato numerico nel periodo"
            except Exception:
                logging.getLogger(__name__).exception("HA Dashboard history query failed")
                response["error"] = "Storico non disponibile: verifica Recorder"
            if generation != self._history_generation: return
            self._history_payload = "json:" + json.dumps(response, ensure_ascii=False, separators=(",", ":"))
            self._attr_extra_state_attributes = {**self._attr_extra_state_attributes, "storico": self._history_payload}
            self.async_write_ha_state()

    async def _energy_requested(self, event):
        request = str(event.data.get("request_id", ""))[:96]
        try: offset = int(event.data.get("day", 0))
        except (TypeError, ValueError): return
        if not request or offset not in range(-30, 1): return
        self._energy_generation += 1
        generation = self._energy_generation
        async with self._energy_lock:
            if generation != self._energy_generation: return
            now = dt_util.now()
            date = now.date() + timedelta(days=offset)
            start = dt_util.as_utc(datetime.combine(date, time.min, tzinfo=now.tzinfo))
            day_end = dt_util.as_utc(datetime.combine(date + timedelta(days=1), time.min, tzinfo=now.tzinfo))
            end = min(dt_util.utcnow(), day_end)
            edges = [start.timestamp() + 3600 * i for i in range(int((day_end-start).total_seconds()/3600)+1)]
            count = len(edges)-1
            response = {"request": request, "date": date.strftime("%d/%m/%Y"), "hours": [dt_util.as_local(datetime.fromtimestamp(t, start.tzinfo)).strftime("%H") for t in edges[:-1]], "series": [], "active": math.ceil((end-start).total_seconds()/3600)}
            labels = lr.async_get(self._hass)
            ids = {role: {entry.label_id for entry in labels.labels.values() if entry.name.casefold().strip() == name} for role, name in ROLES.items()}
            registry = er.async_get(self._hass)
            items = []
            for entry in registry.entities.values():
                if entry.disabled_by is not None or not entry.entity_id.startswith("sensor."): continue
                state = self._hass.states.get(entry.entity_id)
                if state is None: continue
                unit = state.attributes.get("unit_of_measurement", "")
                roles = [role for role in ROLES if entry.labels & ids[role]]
                if len(roles) > 1:
                    response["error"] = "Un sensore ha più label Energia: assegna un solo ruolo"
                    break
                role = roles[0] if roles else "device"
                if not roles and (entry.entity_id not in self._selected_entities or unit not in POWER_UNITS): continue
                if unit not in POWER_UNITS and unit not in ENERGY_UNITS: continue
                items.append((entry.entity_id, state.name, role, unit, state.attributes.get("state_class", "")))
            try:
                if len(items) > 64: response["error"] = "Massimo 64 sensori energetici"
                if not response.get("error"):
                    role_values = {role: [] for role in ROLES}
                    for entity, name, role, unit, state_class in items:
                        if generation != self._energy_generation: return
                        def query(entity=entity, unit=unit, state_class=state_class):
                            result = history.get_significant_states(self._hass, start, end, [entity], include_start_time_state=True, significant_changes_only=True, minimal_response=False, no_attributes=True)
                            return hourly_energy(result.get(entity, []), edges, end, unit, state_class)
                        values = await get_instance(self._hass).async_add_executor_job(query)
                        if role == "device": response["series"].append({"id": entity, "name": name, "values": values})
                        else: role_values[role].append(values)
                    totals = {role: sum_series(rows, count) for role, rows in role_values.items() if rows}
                    response["flows"] = totals
                    # Production without export, or only one battery direction, is incomplete.
                    complete = "ingresso" in totals and ("produzione" not in totals or "immissione" in totals) and (("carica" in totals) == ("scarica" in totals))
                    if complete:
                        house = []
                        for i in range(count):
                            parts = [row[i] for row in totals.values()]
                            house.append(round(max(0, sum(totals.get(role, [0]*count)[i] * sign for role, sign in (("ingresso",1),("produzione",1),("scarica",1),("immissione",-1),("carica",-1)))),5) if all(v is not None for v in parts) else None)
                        response["house"] = house
                        device_total = sum_series([item["values"] for item in response["series"]], count) if response["series"] else [0]*count
                        residual = [round(max(0,h-d),5) if h is not None and d is not None else None for h,d in zip(house,device_total)]
                        response["series"].append({"id":"@unmonitored", "name":"Consumi non monitorati", "values":residual})
                        response["overlap"] = any(h is not None and d is not None and d > h + 0.01 for h,d in zip(house,device_total))
                    response["note"] = "kWh stimati dallo storico · N/D: dati incompleti" if complete else "kWh stimati · Bilancio impianto non disponibile o incompleto"
                    if response.get("overlap"): response["note"] = "Dispositivi oltre il totale: verifica sensori sovrapposti o bilancio"
                    if not items: response["error"] = "Nessun sensore di potenza HMH Dashboard o label Energia"
            except Exception:
                logging.getLogger(__name__).exception("HMH energy query failed")
                response["error"] = "Energia non disponibile: verifica Recorder"
            if generation != self._energy_generation: return
            self._energy_payload = "json:" + json.dumps(response, ensure_ascii=False, separators=(",", ":"))
            self._attr_extra_state_attributes = {**self._attr_extra_state_attributes, "energia": self._energy_payload}
            self.async_write_ha_state()

    @callback
    def _cancel_pending(self):
        if self._cancel_timer is not None:
            self._cancel_timer()
            self._cancel_timer = None

    @callback
    def _queue_refresh(self):
        # Collapse bursts of registry/state events into one catalog update.
        if self._cancel_timer is None:
            handle = self._hass.loop.call_later(0.75, self._refresh)
            self._cancel_timer = handle.cancel

    @callback
    def _registry_changed(self, event):
        self._queue_refresh()

    @callback
    def _state_changed(self, event):
        if event.data.get("entity_id") in self._selected_entities or str(event.data.get("entity_id", "")).startswith("weather."):
            self._queue_refresh()

    @callback
    def _periodic(self, now):
        self._queue_refresh()

    @callback
    def _refresh(self):
        self._cancel_pending()
        self._build()
        self.async_write_ha_state()

    @callback
    def _build(self):
        areas = ar.async_get(self._hass)
        devices = dr.async_get(self._hass)
        entities = er.async_get(self._hass)
        labels = lr.async_get(self._hass)
        # Match only display names, uniformly and case-insensitively.
        label_ids = {entry.label_id for entry in labels.labels.values()
                     if entry.name.casefold().strip() == "hmh dashboard"}
        chip_ids = {entry.label_id for entry in labels.labels.values()
                    if entry.name.casefold().strip() == "hmh dashboard chip"}
        area_items = []
        for area in areas.areas.values():
            icon = area.icon or "mdi:home-outline"
            codepoint = self._icons.get(icon, self._icons["mdi:home-outline"])
            area_items.append({"id": area.id, "name": unicodedata.normalize("NFC", area.name), "icon": icon,
                               "icon_glyph": chr(int(codepoint, 16)),
                               "icon_supported": icon in self._icons})
        area_items.sort(key=lambda item: (item["name"].casefold(), item["id"]))
        entity_items = []
        selected = set()
        excluded_labeled = []
        debug_ids = ("sensor.zhimi_mb3_3424_pm25",
                     "sensor.zhimi_mb3_3424_temperature")
        debug_registry = {}
        for debug_id in debug_ids:
            debug_entry = entities.async_get(debug_id)
            debug_registry[debug_id] = {
                "nel_registro": debug_entry is not None,
                "label_ids": sorted(debug_entry.labels) if debug_entry else [],
                "label_match": bool(debug_entry.labels & label_ids) if debug_entry else False,
                "disabled": str(debug_entry.disabled_by) if debug_entry else None,
                "area_id": debug_entry.area_id if debug_entry else None,
                "device_id": debug_entry.device_id if debug_entry else None,
                "stato_presente": self._hass.states.get(debug_id) is not None,
            }
        for entry in entities.entities.values():
            domain = entry.entity_id.split(".", 1)[0]
            is_chip = domain in DOMAINS and bool(entry.labels & chip_ids)
            if not (entry.labels & label_ids or is_chip):
                continue
            if domain not in DOMAINS:
                excluded_labeled.append({"id": entry.entity_id, "reason": "unsupported_domain"})
                continue
            if entry.disabled_by is not None:
                excluded_labeled.append({"id": entry.entity_id, "reason": "disabled"})
                continue
            if domain == "weather" and not is_chip:
                excluded_labeled.append({"id": entry.entity_id, "reason": "weather_not_chip"})
                continue
            selected.add(entry.entity_id)
            area_id = entry.area_id
            if not area_id and entry.device_id and (device := devices.async_get(entry.device_id)):
                area_id = device.area_id
            if not area_id or area_id not in areas.areas:
                excluded_labeled.append({"id": entry.entity_id, "reason": "no_valid_area"})
                continue
            device = devices.async_get(entry.device_id) if entry.device_id else None
            state = self._hass.states.get(entry.entity_id)
            attrs = state.attributes if state is not None else {}
            unit = attrs.get("unit_of_measurement") or ""
            device_class = attrs.get("device_class") or ""
            if domain == "sensor":
                # Numerical metadata keeps offline sensors in the catalog too.
                inferred = {"°C": "temperature", "°F": "temperature", "K": "temperature",
                            "W": "power", "kW": "power", "mW": "power", "MW": "power",
                            "Wh": "energy", "kWh": "energy", "MWh": "energy",
                            "J": "energy", "kJ": "energy", "MJ": "energy", "GJ": "energy",
                            "A": "current", "mA": "current", "V": "voltage", "mV": "voltage"}
                if not device_class:
                    device_class = inferred.get(unit, "")
                try:
                    numeric = state is not None and math.isfinite(float(state.state))
                except (TypeError, ValueError):
                    numeric = False
                if not is_chip and device_class in {"enum", "date", "timestamp"}:
                    continue
                if not is_chip and not (numeric or unit or device_class or attrs.get("state_class")):
                    continue
            modes = set(attrs.get("supported_color_modes") or ())
            rgb = attrs.get("rgb_color") or [255, 255, 255]
            if not isinstance(rgb, (list, tuple)) or len(rgb) < 3:
                rgb = [255, 255, 255]
            def number(key, default=None):
                try:
                    value = float(attrs.get(key))
                    return value if math.isfinite(value) else default
                except (ValueError, TypeError):
                    return default
            features = int(attrs.get("supported_features") or 0)
            climate_unit = attrs.get("temperature_unit") or self._hass.config.units.temperature_unit
            entity_items.append({
                "id": entry.entity_id, "area": area_id, "_device": entry.device_id,
                "device": entry.device_id or "", "chip": is_chip,
                "condition": WEATHER_CONDITIONS.get(state.state, state.state) if domain == "weather" and state else "",
                "glyph": chr(int(self._icons.get(WEATHER_ICONS.get(state.state, "mdi:weather-cloudy"), self._icons["mdi:home-outline"]), 16)) if domain == "weather" and state else "",
                "device_name": (device.name_by_user or device.name or entry.device_id) if device else "",
                "name": attrs.get("friendly_name") or entry.name or entry.original_name or entry.entity_id,
                "state": state.state if state is not None else "unavailable",
                "device_class": device_class,
                "unit": climate_unit if domain in {"climate", "weather"} else unit,
                "number_min": number("min"), "number_max": number("max"), "number_step": number("step"),
                "alarm_modes": [action for flag, action in [
                    (AlarmControlPanelEntityFeature.ARM_HOME, "alarm_arm_home"),
                    (AlarmControlPanelEntityFeature.ARM_AWAY, "alarm_arm_away"),
                    (AlarmControlPanelEntityFeature.ARM_NIGHT, "alarm_arm_night"),
                    (AlarmControlPanelEntityFeature.ARM_VACATION, "alarm_arm_vacation"),
                    (AlarmControlPanelEntityFeature.ARM_CUSTOM_BYPASS, "alarm_arm_custom_bypass")]
                    if domain == "alarm_control_panel" and features & flag],
                "code_format": attrs.get("code_format") or "",
                "code_arm_required": bool(attrs.get("code_arm_required", True)),
                "code_disarm_required": bool(attrs.get("code_disarm_required", bool(attrs.get("code_format")))),
                "percentage": number("percentage"),
                "speed_supported": domain == "fan" and bool(features & FanEntityFeature.SET_SPEED),
                "position": number("current_position"),
                "cover_open": domain == "cover" and bool(features & CoverEntityFeature.OPEN),
                "cover_close": domain == "cover" and bool(features & CoverEntityFeature.CLOSE),
                "cover_stop": domain == "cover" and bool(features & CoverEntityFeature.STOP),
                "current_temperature": number("current_temperature"),
                "temperature": number("temperature"),
                "target_temp_low": number("target_temp_low"),
                "target_temp_high": number("target_temp_high"),
                "min_temp": number("min_temp", 7 if climate_unit == "°C" else 45), "max_temp": number("max_temp", 35 if climate_unit == "°C" else 95),
                "target_temp_step": number("target_temp_step", 0.5 if climate_unit == "°C" else 1),
                "target_supported": domain == "climate" and bool(features & ClimateEntityFeature.TARGET_TEMPERATURE),
                "range_supported": domain == "climate" and bool(features & ClimateEntityFeature.TARGET_TEMPERATURE_RANGE),
                "hvac_action": attrs.get("hvac_action") or "",
                "hvac_modes": list(attrs.get("hvac_modes") or []) if domain == "climate" else [],
                "brightness": int(attrs.get("brightness") or 0),
                "rgb": list(rgb[:3]),
                "brightness_supported": domain == "light" and bool(modes - {"onoff", "unknown"}),
                "color_supported": domain == "light" and bool(modes & COLOR_MODES),
            })
        # Purifiers are fans with a PM2.5 sensor on the same registered device.
        for item in entity_items:
            if not item["id"].startswith("fan.") or item.get("chip"): continue
            entry = entities.entities.get(item["id"])
            state = self._hass.states.get(item["id"])
            attrs = state.attributes if state else {}
            candidates = []
            if entry.device_id:
                for sensor in entities.entities.values():
                    if sensor.disabled_by is not None or sensor.device_id != entry.device_id or not sensor.entity_id.startswith("sensor."): continue
                    ss = self._hass.states.get(sensor.entity_id)
                    sa = ss.attributes if ss else {}
                    identity = (sensor.entity_id + " " + (sa.get("friendly_name") or sensor.original_name or "")).casefold().replace("_", "").replace(" ", "")
                    if sa.get("device_class") == "pm25" or "pm2.5" in identity or "pm25" in identity:
                        candidates.append((sensor.entity_id, ss, sa))
            name = item["name"].casefold()
            purifier = bool(candidates) or attrs.get("icon") == "mdi:air-purifier" or any(word in name for word in ("purifier", "purificatore", "purificador"))
            if not purifier: continue
            item["purifier"] = True
            if len(candidates) == 1:
                eid, pm_state, pm_attrs = candidates[0]
                item["pm25_state"] = pm_state.state if pm_state else "unavailable"
                item["pm25_unit"] = pm_attrs.get("unit_of_measurement") or "µg/m³"
                # PM2.5 remains a normal catalog entity ONLY if it has the
                # HMH Dashboard label. The composite renderer owns deduplication.
                # Do not add unlabelled candidates to selected/catalog.

        # Pair only unambiguous, dashboard-labelled entities from the same HA device and area.
        grouped = {}
        for item in entity_items:
            if item.get("_device"):
                grouped.setdefault((item["_device"], item["area"]), []).append(item)
        for group in grouped.values():
            switches = [item for item in group if item["id"].startswith("switch.")]
            powers = [item for item in group if item["id"].startswith("sensor.") and item["device_class"] == "power" and not item.get("chip")]
            if len(switches) == 1 and len(powers) == 1:
                switch, power = switches[0], powers[0]
                switch.update(power_state=power["state"], power_unit=power["unit"], power_entity=power["id"])
                # Keep the labelled power sensor in the catalog as its own entity.
                # The switch may show its reading too, but grouping/rendering
                # must not silently discard a user-selected HA entity.
        # A registered device is one group, even across entity domains/categories.
        device_groups = {}
        for item in entity_items:
            if item.get("device") and not item.get("chip"):
                device_groups.setdefault(item["device"], []).append(item)
        def group_section(item):
            domain = item["id"].split(".", 1)[0]
            if domain == "alarm_control_panel": return 10
            if item.get("purifier"): return 9
            if domain == "light": return 0
            if domain in {"switch", "input_boolean"}: return 1
            if domain == "climate": return 2
            if domain == "cover": return 4
            if domain == "fan": return 7
            if domain == "button": return 6
            if domain == "binary_sensor" and item["device_class"] in {"door", "window", "opening", "garage_door"}: return 8
            if domain == "sensor" and item["device_class"] in {"power", "energy"}: return 5
            return 3
        priority = {10:-1, 9:0, 2:1, 4:2, 0:3, 7:4, 1:5, 3:6, 5:7, 8:8, 6:9}
        for device_id, members in device_groups.items():
            if len(members) < 2: continue
            section = min((group_section(item) for item in members), key=lambda section: priority[section])
            device = devices.async_get(device_id)
            area_id = device.area_id if device and device.area_id in areas.areas else members[0]["area"]
            for item in members:
                item["group_section"] = section
                item["area"] = area_id
        # Battery entities follow the same explicit-label rule as all other
        # dashboard entities. Do not add unlabelled battery sensors from devices.
        # Explicitly selected standalone battery sensors also appear in the battery page.
        for item in entity_items:
            if item["device_class"] != "battery" or item.get("battery_entity"): continue
            domain = item["id"].split(".", 1)[0]
            if domain not in {"sensor", "binary_sensor"}: continue
            if domain == "sensor" and item.get("unit") != "%": continue
            level = None
            if domain == "sensor":
                try:
                    value = float(item["state"])
                    if math.isfinite(value) and 0 <= value <= 100: level = value
                except (ValueError, TypeError): pass
            item.update({"has_battery": True, "battery_entity": True,
                "battery_level": level, "battery_known": level is not None or domain == "binary_sensor" and item["state"] in {"on", "off"},
                "battery_low": domain == "binary_sensor" and item["state"] == "on"})
        # Send only fields used by the entity's card: avoid climate/light metadata on sensors.
        common = {"id", "area", "name", "state", "device_class", "chip", "device", "device_name", "group_section", "has_battery", "battery_level", "battery_known", "battery_low", "battery_only", "battery_entity"}
        specific = {
            "alarm_control_panel": {"alarm_modes", "code_format", "code_arm_required", "code_disarm_required"},
            "number": {"unit", "number_min", "number_max", "number_step"},
            "input_number": {"unit", "number_min", "number_max", "number_step"},
            "weather": {"temperature", "unit", "condition", "glyph"},
            "sensor": {"unit", "device", "device_name", "chip"},
            "binary_sensor": {"device", "device_name", "chip"},
            "fan": {"percentage", "speed_supported", "purifier", "pm25_state", "pm25_unit"},
            "switch": {"power_state", "power_unit", "power_entity"},
            "light": {"brightness", "rgb", "brightness_supported", "color_supported"},
            "cover": {"position", "cover_open", "cover_close", "cover_stop"},
            "climate": {"unit", "current_temperature", "temperature", "target_temp_low", "target_temp_high",
                        "min_temp", "max_temp", "target_temp_step", "target_supported", "range_supported", "hvac_action", "hvac_modes"},
        }
        entity_items = [{key: value for key, value in item.items()
                         if key in common or key in specific.get(item["id"].split(".", 1)[0], set())}
                        for item in entity_items]
        for item in entity_items:
            for key in ("name", "device_name"):
                if key in item: item[key] = unicodedata.normalize("NFC", item[key])
        # Remove only known room/device names at the edges of a chip label.
        for item in entity_items:
            if not item.get("chip"): continue
            entry = entities.entities.get(item["id"])
            device = devices.async_get(entry.device_id) if entry and entry.device_id else None
            area = areas.areas.get(item["area"])
            prefixes = [area.name if area else "", device.name_by_user if device else "",
                        device.name if device else ""]
            label = item["name"]
            for _ in range(4):
                previous = label
                for prefix in sorted(filter(None, prefixes), key=len, reverse=True):
                    prefix = unicodedata.normalize("NFC", prefix).strip()
                    folded, match = label.casefold(), prefix.casefold()
                    separators = " -–—_:|/()"
                    if folded.startswith(match) and len(label) > len(prefix) and label[len(prefix)] in separators:
                        candidate = label[len(prefix):].strip(separators)
                        if candidate: label = candidate
                    elif folded.endswith(match) and len(label) > len(prefix) and label[-len(prefix)-1] in separators:
                        candidate = label[:-len(prefix)].strip(separators)
                        if candidate: label = candidate
                if label == previous: break
            item["name"] = label
        entity_items.sort(key=lambda item: (item["name"].casefold(), item["id"]))
        # Weather is discovered independently of room/display labels.
        weather_states = sorted(self._hass.states.async_all("weather"), key=lambda state: state.entity_id)
        available = [state for state in weather_states if state.state not in {"unknown", "unavailable"}]
        weather = {}
        if available:
            state = available[0]
            selected.add(state.entity_id)
            conditions = WEATHER_CONDITIONS
            details = []
            for key, prefix, unit in [("temperature", "", state.attributes.get("temperature_unit", "°C")),
                                      ("humidity", "Umidità ", "%"),
                                      ("wind_speed", "Vento ", state.attributes.get("wind_speed_unit", ""))]:
                value = state.attributes.get(key)
                if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
                    details.append(f"{prefix}{value:.1f} {unit}".strip())
            icons = WEATHER_ICONS
            code = self._icons.get(icons.get(state.state, "mdi:weather-cloudy"))
            weather = {"text": conditions.get(state.state, state.state), "details": " · ".join(details),
                       "glyph": chr(int(code, 16)) if code else "", "id": state.entity_id}
        self._selected_entities = selected
        self._attr_native_value = len(area_items)
        self._attr_extra_state_attributes = {
            "catalogo": "json:" + json.dumps({"areas": area_items}, ensure_ascii=False, separators=(",", ":")),
            "entita": "json:" + json.dumps({"entities": entity_items, "weather": weather}, ensure_ascii=False, separators=(",", ":")),
            "display_label": self._label,
            "diagnostica_catalogo": {"pubblicate": len(entity_items),
                "escluse_etichettate": excluded_labeled,
                "pm25_cucina_presente": any(item["id"] == "sensor.zhimi_mb3_3424_pm25"
                                           for item in entity_items),
                "label_ids_configurati": sorted(label_ids),
                "verifica_sensori": debug_registry},
            "storico": self._history_payload,
            "energia": self._energy_payload,
        }
