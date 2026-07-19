# Media (Nexus)

Regenerar: `python Media/generate_media.py` (solo requiere Pillow).

Salidas en `Media/generated/` (jpg + png):

| Archivo | Tamaño | Uso en Nexus |
|---|---|---|
| `stellartool_header_1300x372` | 1300×372 | Header/banner (gráfico: logo + texto) |
| `stellartool_gallery_1920x1080` | 1920×1080 | Galería de features (gráfico) |
| `stellartool_screenshot_easymerge.png` | 1180×760 | **Captura REAL** de la app (portada) |

El header y la galería son gráficos promocionales (logo + texto), no capturas.
La portada y cualquier otra imagen de la app deben ser **capturas reales** —
nunca mocks. Sumar capturas reales de: EasyMerge con mods cargados, Cambios con
diffs vanilla→mod, Conflictos lado a lado, Merge OK.

Paleta: alineada con `qml/Theme.qml` (bg `#14161c`, acento `#5aa2ff`, dorado `#e6b455`).
El logo (ramas que se mergean bajo una estrella) coincide con `assets/app_icon`.
