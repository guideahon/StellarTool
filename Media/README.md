# Media (Nexus)

Regenerar: `python Media/generate_media.py` (solo requiere Pillow).

Salidas en `Media/generated/` (jpg + png):

| Archivo | Tamaño | Uso en Nexus |
|---|---|---|
| `stellartool_header_1300x372` | 1300×372 | Header/banner de la página del mod |
| `stellartool_cover_1280x720` | 1280×720 | **Imagen de portada** (mock de la UI EasyMerge) |
| `stellartool_gallery_1920x1080` | 1920×1080 | Imagen de galería con features |

Capturas reales recomendadas además de estas (tomar de la app):
EasyMerge con mods cargados, página Cambios con diffs vanilla→mod, Conflictos lado a lado, resultado del Merge OK.

Paleta: alineada con `qml/Theme.qml` (bg `#14161c`, acento `#5aa2ff`, dorado `#e6b455`).
El logo (ramas que se mergean bajo una estrella) coincide con `assets/app_icon`.
