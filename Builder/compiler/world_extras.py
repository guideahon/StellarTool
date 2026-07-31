"""world_extras - tweaks de mundo/economia sobre tablas fuera del combate.

Cubre las tablas que el pak de combate/outfit no toca y que hasta ahora no tenian
ningun control en el Builder: tienda, drops, progresion y pesca. Cada funcion muta
el doc de su tabla in place y devuelve cuantas propiedades cambio.

Todos los controles son PORCENTAJES sobre el valor vanilla (100 = vanilla), asi el
boton "Vanilla" de la UI es simplemente volver a 100 y no hace falta guardar los
valores originales en ningun lado.

- shop_prices(ShopItemTable): escala MoneyItemCount1..4 y sus versiones con
  descuento. 50 = mitad de precio.
- drop_rates(RewardGroupTable): escala la probabilidad de las filas RandomEach
  (DropRate es basis points sobre 10000) y, aparte, las cantidades. Las filas
  RandomWeight NO se tocan: ahi DropRate es un peso relativo dentro del grupo, y
  multiplicar todos los pesos por igual no cambia nada.
- sp_exp(SPLevelTable): escala RequiredSPExp de cada nivel de SP.
- upgrade_costs(CharacterLevelTable): escala RequiredItemAmount1/2 de cada mejora.
- fishing(ItemFishTable): escala Stamina (menos = pez mas facil) y FightingTime
  (mas = mas tiempo para pelearlo).
"""
from __future__ import annotations

# Basis points de la probabilidad por item en las filas RandomEach.
DROP_RATE_MAX = 10000


def _rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def _prop(row, name):
    return next((p for p in row["Value"] if p["Name"] == name), None)


def _get(row, name):
    p = _prop(row, name)
    return p.get("Value") if p else None


def _set(row, name, value):
    p = _prop(row, name)
    if p is None or p.get("Value") == value:
        return False
    p["Value"] = value
    p["IsZero"] = value in (0, 0.0, None)
    return True


def _scale(value, percent, minimum=0, maximum=None):
    """value * percent/100 redondeado. minimum solo aplica si el original era >0
    y el porcentaje no es 0: bajar el precio no puede volver gratis un item por
    redondeo, pero pedir 0% si es una eleccion explicita."""
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        return value
    pct = float(percent)
    scaled = int(round(value * pct / 100.0))
    if pct > 0:
        scaled = max(minimum, scaled)
    if maximum is not None:
        scaled = min(maximum, scaled)
    return scaled


_PRICE_FIELDS = [f"{prefix}MoneyItemCount{i}"
                 for i in range(1, 5)
                 for prefix in ("", "Discount_")]


def shop_prices(shop_item_doc, price_percent=50) -> int:
    n = 0
    for row in _rows(shop_item_doc):
        for field in _PRICE_FIELDS:
            value = _get(row, field)
            scaled = _scale(value, price_percent, minimum=1)
            if scaled != value and _set(row, field, scaled):
                n += 1
    return n


def drop_rates(reward_group_doc, chance_percent=200, count_percent=100) -> int:
    n = 0
    for row in _rows(reward_group_doc):
        if _get(row, "DropType") == "ESBRewardGroupDrop_RandomEach":
            rate = _get(row, "DropRate")
            scaled = _scale(rate, chance_percent, minimum=1, maximum=DROP_RATE_MAX)
            if scaled != rate and _set(row, "DropRate", scaled):
                n += 1
        for field in ("ItemMinCount", "ItemMaxCount"):
            count = _get(row, field)
            scaled = _scale(count, count_percent, minimum=1)
            if scaled != count and _set(row, field, scaled):
                n += 1
    return n


def sp_exp(sp_level_doc, exp_percent=50) -> int:
    n = 0
    for row in _rows(sp_level_doc):
        value = _get(row, "RequiredSPExp")
        scaled = _scale(value, exp_percent, minimum=1)
        if scaled != value and _set(row, "RequiredSPExp", scaled):
            n += 1
    return n


def upgrade_costs(character_level_doc, cost_percent=50) -> int:
    n = 0
    for row in _rows(character_level_doc):
        for field in ("RequiredItemAmount1", "RequiredItemAmount2"):
            value = _get(row, field)
            scaled = _scale(value, cost_percent, minimum=1)
            if scaled != value and _set(row, field, scaled):
                n += 1
    return n


def fishing(item_fish_doc, stamina_percent=50, fighting_time_percent=200) -> int:
    n = 0
    for row in _rows(item_fish_doc):
        for field, percent in (("Stamina", stamina_percent),
                               ("FightingTime", fighting_time_percent)):
            value = _get(row, field)
            scaled = _scale(value, percent, minimum=1)
            if scaled != value and _set(row, field, scaled):
                n += 1
    return n


# id de respuesta -> (tabla, funcion). El orden es el de la UI.
WORLD_EXTRAS = {
    "shopPrices": ("ShopItemTable", "shop_prices"),
    "dropRates": ("RewardGroupTable", "drop_rates"),
    "spExp": ("SPLevelTable", "sp_exp"),
    "upgradeCosts": ("CharacterLevelTable", "upgrade_costs"),
    "fishing": ("ItemFishTable", "fishing"),
}

# Valores por defecto de cada control (100 = vanilla en todos).
DEFAULT_VALUES = {
    "shop_prices": {"price_percent": 50},
    "drop_rates": {"chance_percent": 200, "count_percent": 100},
    "sp_exp": {"exp_percent": 50},
    "upgrade_costs": {"cost_percent": 50},
    "fishing": {"stamina_percent": 50, "fighting_time_percent": 200},
}


def tables_for(extras) -> set:
    """Tablas que necesita la seleccion (para saber que paks tocar)."""
    return {WORLD_EXTRAS[e][0] for e in (extras or []) if e in WORLD_EXTRAS}


def values_for(fn_name, world_values) -> dict:
    """Valores del control, completando con los defaults."""
    values = dict(DEFAULT_VALUES.get(fn_name, {}))
    values.update((world_values or {}).get(fn_name, {}) or {})
    return values


def apply_world_extras(doc, table, extras, world_values=None) -> dict:
    """Aplica sobre `doc` los extras de `table` que esten seleccionados."""
    import sys
    module = sys.modules[__name__]
    report = {}
    for extra in extras or []:
        spec = WORLD_EXTRAS.get(extra)
        if not spec or spec[0] != table:
            continue
        fn_name = spec[1]
        report[extra] = getattr(module, fn_name)(
            doc, **values_for(fn_name, world_values))
    return report
