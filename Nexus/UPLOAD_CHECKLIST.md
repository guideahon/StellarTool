# Checklist de subida a Nexus

Antes de cada release en Nexus Mods:

1. **Zip de release**: correr `package.bat NOPAUSE` → genera `dist\StellarTool-<version>.zip`
   (exe + runtime Qt + `tools\` con repak/retoc/UAssetGUI/usmap, sin oo2core). Probar en
   una máquina/carpeta limpia que abre y mergea.
   - Verificar licencias de redistribución de los binarios bundled (repak/retoc: MIT;
     UAssetGUI: MIT). El `.usmap` es de la comunidad: enlazar la fuente en la
     descripción en lugar de redistribuir si hay dudas.
   - NO incluir `oo2core_*.dll` (Oodle, propietaria). retoc la descarga solo si
     hace falta.
2. **Category**: Utilities / Tools.
3. **Short description**: `upload_texts/00_short_description.txt` (límite 250 chars).
4. **Description**: pegar `Nexus_Description_BBCode.txt`.
5. **File description**: `upload_texts/file_1_Stellar_Tool.txt`.
6. **Media**: capturas de las 4 páginas (Mods con 2+ mods cargados, Cambios con
   diffs vanilla→mod visibles, Conflictos lado a lado, Merge con resultado OK).
   Idealmente un GIF corto del flujo completo.
7. **Permissions**: open source MIT, enlazar el repo. Permitir uso libre con crédito.
8. **Changelog**: sincronizar con los tags/commits del repo.
9. Actualizar `Nexus_Description*.md/txt` en el repo si la descripción cambia.
