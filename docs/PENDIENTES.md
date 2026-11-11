# Pendientes y cosas probadas

Estado al 2026-07-26 (v0.3.8). Qué falta, qué se intentó y con qué evidencia.
Para el problema de escritura de mods Zen en detalle:
[ZEN_WRITE_BACK.md](ZEN_WRITE_BACK.md).

---

## 1. Bloqueante técnico abierto

### Medición real de `RowAdded` y arrays vacíos

> Estado vigente: `RowAdded` y arrays escalares con base vacía están
> implementados y cubiertos por tests. Las cifras y el error listados debajo
> son la referencia histórica previa; falta repetir el round-trip real.

Históricamente eran ~63 de los 66 `skipped` del mod de prueba.

- **Estado histórico**: `buildRowFromTemplate` estaba deshabilitado porque
  UAssetAPI rechazaba el uasset. Hoy está habilitado con sanitización recursiva.
- **Error exacto**:
  `System.ArgumentException: Cannot add an empty FString to the name map`
  en `UAssetAPI.UAsset.AddNameReference`.
- **Causa**: la normalización canoniza el FName `None` como `""`; escribir `""`
  en un `NameProperty` explota.
- **Solución implementada**: convertir `""` → `null` recursivamente en cada
  `NameProperty`, incluidos arrays y structs anidados.

La implementación actual habilita filas nuevas, normaliza recursivamente FName
vacíos y sintetiza arrays escalares desde `ArrayType`. Los tests unitarios
pasan; falta repetir el merge real para reemplazar la medición histórica.

### Filas borradas (`RowRemoved`) en mods Zen — resuelto

Se aplican con `rows.removeAt`. El guard bloquea todos los borrados de un mod si
faltan más del 25% de las filas vanilla; así una exportación CUE4Parse truncada
no se convierte silenciosamente en una eliminación masiva.

- **No es ambiguo** (se creía que sí, y era falso). Verificado sobre el mod de
  prueba: la tabla del mod trae **todas** las filas de vanilla más las nuevas
  (SkillCommandTable 1440 → 1503, faltantes 0; EffectTable 4227 → 4227). En UE
  un override de DataTable reemplaza el asset entero, así que una fila ausente
  en la tabla del mod es un borrado real del autor.
- **Salvedad resuelta con guard**: si faltan más del 25% de las filas vanilla,
  no se aplica ningún borrado de ese mod.
- No aparece en el mod de prueba (0 casos), pero va a aparecer con otros.

### Arrays de structs cuya base vanilla está vacía

- **Estado histórico**: eran 3 casos (1 en EffectTable, 2 en
  SkillCommandTable); los escalares ya están implementados.
- **Causa**: si vanilla tiene `[]` no hay elemento de molde del que sacar
  `$type` para los elementos nuevos.
- **Implementado para escalares**: sintetizar el elemento desde el `ArrayType` del propio
  `ArrayPropertyData` (p. ej. `"NameProperty"` → `NamePropertyData`) más los
  campos estándar del wrapper (`ArrayIndex`, `PropertyGuid: null`,
  `IsZero: false`, `PropertyTagFlags: "None"`, `PropertyTagExtensions:
  "NoExtension"`). Si el `ArrayType` es `StructProperty` no alcanza: haría falta
  además el layout del struct.

---

## 2. Sin verificar a mano (features que compilan y arrancan, pero nunca se
usaron de verdad)

Todo esto se validó por compilación, arranque limpio de la app y/o tests
unitarios, **no** ejercitándolo en la UI:

- **Edición masiva** (×N, +, −, clamp… con regex de fila) — 0.3.0.
- **Import / export de TOML** — implementado; falta validación manual completa de
  bundles reales de terceros.
- **Descarga de usmap por versión** (Ajustes → Mappings) — 0.3.0. Nunca se
  ejecutó una descarga real contra el archivo de la comunidad.
- **Traducciones**: las claves nuevas se agregaron a los 10 idiomas por script;
  solo se revisaron en/es a ojo. Se detectó y corrigió un carácter mal escapado
  en coreano, así que puede haber más.


Lo que sí se verificó en vivo (por el usuario): cola de importación, scroll de
conflictos que no salta, scrollbars, botón de analizar, y los **arreglos de
colaterales del outfit** (2026-07-28, los dos activados por defecto):

- `outfit.vanillaRestFX`: contra vanilla puro el FX de campamento aparece, con el
  pak shipped desaparece, y con el arreglo vuelve **sin** perder el swap ni el
  restore. `ActiveShowPath` y `ActiveTargetEffectAliasArray` conviven en la misma
  fila.
- `outfit.vanillaShieldRegenBlock`: con la fila trigger restaurada el swap sigue
  ocurriendo al romper el escudo y vuelven los 4 s sin regeneración.

> Trampa de este test: `~mods` se carga **recursivamente**. Dos carpetas de build
> (`compile_mb\`, `stage\Paks\`) habían quedado adentro con el pak de mini-bosses
> y su propia EffectTable, tapando el pak de prueba. Tres rondas dieron el mismo
> resultado por eso. Antes de cualquier A/B in-game: verificar que el pak bajo
> prueba sea el único `.utoc` cargable con esa tabla.
>
> Ya no debería repetirse: `build_custom.check_out_dir` rechaza compilar dentro
> de `~mods` (UI y CLI) e `installer.shadow_paks` lista los que hayan quedado.

---

## 3. Limitaciones conocidas que NO son bugs

- **Import de TOML necesita baseline**: si la fila no existe en vanilla, el merge
  corta con "Fila no existe". Esperado.
- **Export de TOML solo escalares**: arrays/objetos no se exportan como literal.
- **Una tabla con 0 cambios aplicados no se emite**: decisión deliberada, si no
  el pak mergeado (prioridad `zzz_`) pisaría al mod de origen con vanilla.

---

## 4. Cosas que se probaron y se descartaron

| Intento | Resultado | Por qué se descartó |
|---|---|---|
| Reconstrucción de arrays/objetos (1er intento) | crash duro, exit 1 | Guard faltante con array base vacío |
| Idem (2º intento) | exit 0, 0 cambios de más | El bug del Null (ver abajo) lo hacía fallar en silencio |
| Idem (3er intento, con NameMap ya arreglado) | 0 cambios de más | Mismo bug del Null, sin diagnosticar todavía |
| Reintento por tabla (pase completo sin complejos) | funcionaba pero inútil | Costaba un `fromjson`+verify extra sobre tablas de cientos de MB para ganar cero |
| Plantillas por propiedad (`buildPropTemplates`) | sin efecto medible | El fallo real estaba en el Null, no en la elección de plantilla |
| `addPropFromTemplate` (props que vanilla omite) | **se conservó** | No se activó en el mod de prueba (las props existían con `"+0"`), pero es correcto y barato |

**El bug que hizo fracasar tres intentos**: en Qt, `QJsonValue()` y `return {}`
construyen **Null**, no Undefined. Los centinelas de "no se pudo reconstruir"
escribían `null` en la propiedad en vez de saltear el cambio. Al arreglarlo, los
arrays funcionaron en la primera corrida.

**Lección de método**: se perdió mucho tiempo suponiendo la causa. Lo que
destrabó el problema fue instrumentar **qué difería exactamente** en el verify y
**leer el error real** (portapapeles de UAssetGUI). Hacer eso primero.

---

## 5. Higiene del repo

- **Trabajo en curso sin commitear** (del usuario, no tocado):
  `Builder/`, `Release/`, `assets/audio/`, `dist_nexus/`, `package.bat`,
  `qml/pages/BuilderPage.qml`, y claves `builder_ex_*` en `i18n/en.json` y
  `i18n/es.json`.
- **`dist_nexus/`** acumula todos los zips de release (~190 MB c/u). No está en
  git; conviene limpiar los viejos.
- **CHECKS.md** tiene casi todos los ítems sin marcar; nunca se hizo la pasada
  formal de aceptación.
- **Test manual in-game** (`docs/manual-test.md`, exigido por CHECKS Fase 5):
  no existe. Ningún pak mergeado se verificó cargando el juego — solo por
  round-trip de las tablas.

---

## 6. Comunicación pendiente

- Publicar **0.3.8** y avisar que filas nuevas/borradas y arrays con base vacía
  ya se mergean (a **manakaiser** y en el hilo de **MrZzzzzzzzzzzzz**).
- Los textos y el changelog de Nexus están actualizados para 0.3.8 (`Nexus/`).

## 7. Regresión de replacers del skinsuit tras actualización del juego (2026-11-11)

**Conclusión confirmada por prueba del usuario:** el outfit `zYgSkinSuit_v4` funciona
convertido a CNS, pero falla al usarlo como replacer del skinsuit. El replacer
original también crashea por sí solo, sin CNS ni UE4SS, aunque funcionaba antes
de la actualización del juego. El usuario observa el mismo problema con otros
outfits que reemplazan el skinsuit.

Evidencia de los dumps al equipar el skinsuit:

- 16:52:12 y 16:55:40: `EXCEPTION_ACCESS_VIOLATION` leyendo `0x0` en
  `SB-Win64-Shipping.exe+0x29bbe70`, estado `SBGameState`, a los 34–35 s del
  inicio; ambos tienen el hash de stack
  `633EECCD0B4F8FBAEB2CF76A69548A75C06C89E9`.
- En la corrida de 16:55:40 solo estaba habilitado el mod replacer y el dump no
  contiene `UE4SS.dll`; por tanto, UE4SS y CNS no son requisito para esta caída.
- El control `VanillaMesh_Control_Diagnostic` también llegó al mismo hash de
  crash al equipar. El crash anterior al arrancar con CNS/UE4SS tenía otro hash
  (`78621A0C…`) y stack de UE4SS; es un problema distinto y no debe mezclarse
  con el crash al equipar.
- `zYgSkinSuit_v4_CNS_Test.zip` (conversión de la misma entrada mediante el modo
  CNS de Stellar Tool) fue probado por el usuario y funciona.

**Hipótesis de trabajo:** el fallo está en la ruta de reemplazo del asset
vanilla del skinsuit tras el update, o en el asset/container reserializado para
esa ruta; la geometría específica de zYg y la carga de CNS no bastan para
explicarlo. Sin PDB del ejecutable no se ha identificado la función nativa ni
la causa exacta. No volver a recomendar quitar CNS/UE4SS como solución a este
crash de equipamiento; probar el outfit por CNS evita el reemplazo directo.

---

## Cómo medir cualquier cambio en el merge

```bat
StellarTool.exe --headless merge --mod "<mod zen>.pak" --out <dir> --game "<juego>" --no-zip
```

Comparar `applied` / `skipped` por tabla en `<dir>\merge_report.txt` antes y
después. **Matar UAssetGUI primero** (una instancia GUI colgada hace fallar
corridas válidas e invalida la medición — ya pasó una vez con una tanda entera).

Referencia histórica previa a esta implementación con
`zzz_AllSpecialAllStepsBlockMoveFirst_V78_P` (2225 cambios):

```
CharacterTable:        2 applied,  0 skipped
EffectTable:          44 applied,  1 skipped
SkillActiveStepTable: 1937 applied, 0 skipped
SkillCommandTable:    35 applied, 65 skipped   <- 63 filas nuevas + 2 arrays de base vacía
SkillTable:          141 applied,  0 skipped
```
