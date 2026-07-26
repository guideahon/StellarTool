# Escribir de vuelta cambios de mods Zen — problema, intentos y estado

Bitácora del problema más largo del proyecto: por qué muchos cambios de mods
Zen "se mostraban pero no se aplicaban", qué se probó, qué funcionó y qué no.
Escrito para no re-descubrir nada de esto.

## El problema

Los mods de Stellar Blade en Nexus vienen como contenedores **Zen/IoStore**
(`.ucas/.utoc`). `retoc` no puede revertirlos a legacy, así que se leen con
**CUE4Parse**, que entrega un JSON *normalizado* (valores pelados, sin metadata
de serialización).

Pero para **escribir** hay que producir el JSON *crudo* de UAssetGUI/UAssetAPI,
que es otra cosa: cada propiedad es un objeto con `$type`, `ArrayIndex`,
`PropertyGuid`, etc. El merge parte del JSON crudo de la tabla vanilla y aplica
los cambios encima.

Cuando los dos lados no coinciden, el cambio se saltea (`skipped` en
`merge_report.txt`). Al principio se salteaba **todo lo que no fuera número**.

Marcador que se usó siempre para medir, sobre un mod Zen real
(`zzz_AllSpecialAllStepsBlockMoveFirst_V78_P`, 2225 cambios):

| Versión | skipped totales | Nota |
|---|---|---|
| 0.3.0 | 140 | solo números |
| 0.3.1 | 140 | + strings/enums |
| 0.3.6 | 73 | + fix del float cero |
| actual | 66 | + arrays/objetos |

## Herramienta clave: UAssetGUI reporta por el portapapeles

**Nada de esto se pudo diagnosticar hasta descubrir esto.** UAssetGUI no
imprime sus excepciones: las **copia al portapapeles** y sale con código 0 sin
generar el archivo. Todo fallo se veía como un error mudo.

Desde 0.3.2 la tool recupera ese texto y lo guarda en
`%LOCALAPPDATA%/StellarTool/logs/`. Para depurar a mano: correr `fromjson` y
**leer el portapapeles**.

## Causas encontradas (todas confirmadas con el error exacto)

### 1. FNames nuevos fuera del NameMap
```
DummyFNameSerializationException: Attempt to serialize dummy FName 'X'
 - this name was never added to the NameMap.
```
UAssetAPI trata todo FName ausente del `NameMap` del asset como "dummy" y
explota al escribir. → `registerFNames` agrega los que falten de las filas
tocadas. Agregar de más es inocuo (UAssetGUI recalcula
`NamesReferencedFromExportDataCount`).

### 2. El float cero es el STRING `"+0"`
UAssetGUI serializa el cero flotante como `"+0"`/`"-0"`. El diff lo normaliza a
`0`, así que al escribir un número encima los tipos no coincidían y el cambio se
rechazaba. **Era la causa dominante**: bloqueaba a todo mod que activa algo que
en vanilla vale cero (HP drain, bonuses). → `isFloatZeroString`.

Impacto medido: 140 → 73 skipped. `CharacterTable` pasó de "0 aplicados, tabla
descartada" a mergearse entera; `SkillTable` de 58 skipped a 0.

### 3. `QJsonValue()` es Null, no Undefined  ← el bug que trabó todo lo demás
Los centinelas de "no se pudo reconstruir" usaban `return {}` / `QJsonValue()`,
que en Qt construyen **Null**. El chequeo `isUndefined()` daba false, así que en
vez de saltear el cambio **se escribía `null`** en la propiedad.

Se manifestaba como un verify que no cerraba, con una diferencia desconcertante:
```
PROP ChainEffectAliasArray
  want={"Name":"ChainEffectAliasArray","Value":""}    <- null normalizado
  got ={"Name":"ChainEffectAliasArray","Value":[]}
```
Este bug hizo fracasar **dos intentos previos** de habilitar arrays y filas
nuevas, y mandó la investigación por caminos equivocados (se sospechó del
serializador de UAssetAPI). → devolver `QJsonValue(QJsonValue::Undefined)`
explícito.

### 4. El usmap se pasa por ruta, no por nombre
`fromjson` resuelve un *nombre* de mapping sólo si el `.usmap` está en
`%LOCALAPPDATA%/UAssetGUI/Mappings`; si no, las mappings quedan en null y no se
escribe nada. Pasar la ruta del archivo evita la dependencia.

### 5. Escribir el valor normalizado tal cual
Meter el array limpio directo en la propiedad produce:
```
Could not cast or convert from System.String to PropertyData
Path 'Exports[0].Table.Data[53].Value[101].Value[0]'
```
Es decir: strings pelados donde va un objeto `PropertyData`. → `fillTemplate`
reconstruye la forma cruda usando el valor real como plantilla (wrappers,
`$type`, reindexado de `ArrayIndex`).

## Qué está resuelto

Para mods Zen se escriben y verifican:

- **Números**, incluido sobre el `"+0"` de vanilla.
- **Strings y enums**, re-prefijando el namespace del enum y mapeando
  `None` ↔ nombre real.
- **Propiedades que vanilla no serializa** (valen el default): se copia su forma
  de otra fila que sí la tenga (`addPropFromTemplate`).
- **Arrays y objetos**: se reconstruyen con `fillTemplate` a partir del valor
  vanilla como molde. Medido: `SkillCommandTable` 28 → 35 aplicados.

Red de seguridad: cada tabla se verifica con round-trip real
(`fromjson` → `tojson` → comparar normalizado). Si no cierra, la tabla se
excluye del merge en vez de escribirse mal.

## Qué NO está resuelto

### Filas enteras nuevas (`RowAdded`)

Se reconstruyen correctamente (`buildRowFromTemplate`, usando otra fila de la
tabla como plantilla — todas comparten el struct), pero UAssetAPI rechaza el
resultado:
```
System.ArgumentException: Cannot add an empty FString to the name map
   at UAssetAPI.UAsset.AddNameReference(FString name, ...)
```
La normalización canoniza el FName `None` como `""`, y escribir `""` en un
`NameProperty` dispara eso. Se corrigió el caso de las propiedades de primer
nivel (devolver `null`), **pero el error persiste**: quedan FNames vacíos en
algún punto más profundo que no se localizó.

Impacto: ~63 de los 66 skipped restantes en el mod de prueba son filas nuevas.
**Es el próximo objetivo obvio.** El siguiente paso concreto: instrumentar
`fillTemplate` para volcar cada string vacío que se escriba junto con el `$type`
de su propiedad, y ver dónde queda uno sin convertir a `null`.

### Filas quitadas (`RowRemoved`)

Se saltean, pero **no por una limitación real**: borrar una fila no requiere
reconstruir nada. Sólo está bloqueado por el rechazo en bloque de todo lo que no
sea `Modified`.

Se creyó que era ambiguo ("¿el mod la borró o simplemente no la trae?") y **es
falso**: un override de DataTable en UE reemplaza el asset entero, así que la
tabla del mod viene completa. Verificado sobre el mod de prueba —
SkillCommandTable 1440 filas vanilla → 1503 del mod, **0 faltantes**;
EffectTable 4227 → 4227. Una fila ausente sería un borrado real.

Salvedad al habilitarlo: si CUE4Parse fallara al exportar filas, se borrarían por
error. Conviene un guard de cordura antes de aplicar borrados.

### Arrays con base vacía
Si vanilla tiene `[]` y el mod agrega elementos, no hay elemento de molde del
cual sacar `$type`. Se saltea. Se podría sintetizar desde el `ArrayType` del
propio `ArrayPropertyData` (`"NameProperty"`), no intentado.

## Callejones sin salida / notas

- **El asset vanilla referencia ~325 FName que no están en su propio NameMap** y
  aun así se escribe bien. O sea que UAssetAPI resuelve nombres por otra vía
  además del NameMap local. Nunca se caracterizó; llevó a sospechar
  equivocadamente del NameMap cuando el problema real era el bug del Null.
- **Agregar nombres de más al NameMap es inocuo** (probado: un nombre no
  referenciado no rompe nada).
- **Instancias colgadas de UAssetGUI arruinan las mediciones.** Es una app GUI:
  si queda una abierta (p. ej. al ejecutarla sin argumentos), las corridas
  siguientes fallan **incluso con entradas válidas**. Una tanda entera de
  resultados resultó inválida por esto. Matar el proceso antes de cada prueba.
- **`qWarning`/stderr no se capturan** desde el modo headless (la app es
  `WIN32_EXECUTABLE`). Para depurar hay que escribir a archivo.
- **Una tabla con 0 cambios aplicados no debe empaquetarse**: el pak mergeado
  carga con prioridad máxima (`zzz_`), así que una copia de vanilla **pisa al mod
  de origen** y el usuario ve que "la tabla desapareció".
- Habilitar reconstrucciones "por las dudas" tiene costo real: un pase extra de
  `fromjson`+verify sobre tablas de cientos de MB son minutos. Si una mejora no
  mueve `applied`/`skipped`, no va.

## Cómo medir un cambio en esta zona

```bat
StellarTool.exe --headless merge --mod "<mod zen>.pak" --out <dir> --game "<juego>" --no-zip
```
Comparar `applied` / `skipped` por tabla en `<dir>\merge_report.txt` antes y
después. Matar UAssetGUI primero. Si algo falla, el motivo real está en el
portapapeles y en `%LOCALAPPDATA%/StellarTool/logs/`.
