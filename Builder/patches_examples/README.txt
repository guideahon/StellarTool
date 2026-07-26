PATCHES TOML PROPIOS (opcional, avanzado)
=========================================

El Builder puede aplicar patches declarativos TOML a las tablas del mod, ademas
de las opciones de la UI. Formato inspirado en automod (jpabscale,
nexusmods.com/stellarblade/mods/987) — credito en Settings > Shoutouts.

COMO USAR
---------
1. Crea una carpeta con uno o mas archivos <Tabla>.toml (uno por tabla del juego:
   CharacterTable.toml, EffectTable.toml, SkillTable.toml, ...).
2. En la pestana "Crear mod", campo "Patches TOML propios", elegi esa carpeta.
3. Compila. El Builder aplica cada patch sobre la tabla correspondiente del pak.

FORMATO
-------
Una seccion [NombreDeFila] y debajo Propiedad = valor. Solo modifica propiedades
que YA existen (no crea filas ni propiedades). Ejemplo (CharacterTable.toml):

    [Player]
    MaxBurstGauge = 1800
    AttackSpeed = 1.3
    ShieldRegenPerSecond = 120.0

    [M_HedgeBoarBrute]
    MaxHP = 120000

TIPOS: numeros (int/float), strings entre comillas, true/false. Los strings se
agregan al NameMap automaticamente.

NOTA: solo aplica en builds con Mini-Boss activado por ahora (la CharacterTable es
editable en ese path). Valida in-game: cambiar propiedades puede tener efectos
inesperados. Ver ejemplos en esta carpeta.
