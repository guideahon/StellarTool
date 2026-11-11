# Plugin CPS para Stellar Souls

`CPSPlugin/` es un complemento de runtime para el Builder de Stellar Souls. No
convierte la aplicación Qt en un plugin ni reemplaza el Builder de escritorio:
permite ajustar, durante una sesión, los cambios que CPS puede aplicar con
seguridad a escalares de DataTables que el juego ya cargó.

## Requisitos y compilación

- Windows x64, Visual Studio Build Tools 2022 y CMake 3.20 o posterior.
- CPS SDK local en `CPS-SDK/` junto al repo (no se redistribuye).
- CPS 1.2.0 o posterior instalado en Stellar Blade.

Desde la raíz del repositorio:

```bat
CPSPlugin\build.bat
```

El paquete sale en `build\CPSPlugin\Release\stellar_tool_gameplay.cps`.
Copialo manualmente a `SB\Binaries\Win64\CPS\Plugins`, inicia el juego y abre
CPS con F10. La página se llama **Gameplay/Jugabilidad**. Compilar no modifica
ni despliega archivos del juego.

## Controles runtime disponibles

Los ajustes empiezan apagados. Los cambios se aplican en el hilo del juego y se
guardan en `CPS/Configs/StellarToolGameplay/settings.json`.

### Personaje y QoL

- Vida/escudo/reducción de daño, regeneración de escudo, velocidad de ataque y
  capacidad Beta/Burst/Tachy.
- Cantidad de munición/consumibles, opción de pilas individuales `ammo100x`,
  regeneración de vida, curación al golpear y poder de pesca.
- Ganancia Beta reducida y capacidades Beta/Burst de 400/800.

### Habilidades y efectos

- Valores de enfriamiento/costo de las 18 habilidades Beta/Burst de la tuning
  pública 1.2.19.
- Daño Railgun x2, ventana de parry/esquive perfecto graduable, un uso adicional
  de esquive aéreo y enfriamiento de 4 segundos para tres ataques de embestida.
- Consumo Tachy reducido, efectos positivos de equipo graduables y curación del
  Tumbler graduable.

### Mundo y progresión

- Precios de tienda, requisitos SP, costos de mejoras EVE y atributos de pesca
  como porcentaje del valor original.
- Cantidades mínima/máxima de objetos en `RewardGroupTable` como porcentaje del
  original. Esto no incluye la probabilidad del drop: el plugin no interpreta
  el enum `DropType` para distinguir `RandomEach` de `RandomWeight`.

Cada grupo tiene su propio toggle y sliders. Al apagarlo, CPS intenta restaurar
los datos capturados. El plugin solo restaura un campo si aún conserva el último
valor que escribió; si otro mod o sistema lo cambió, lo deja intacto y explica
el conflicto en `CPS.log`. El botón **Disable all and restore captured values**
desactiva todos los controles portados.

## Opciones que siguen en el Builder de escritorio

| Opción del Builder | Motivo por el que no se migra al runtime actual |
|---|---|
| Perfiles de combate Full/First Run y perfiles de daño por Beta/Burst, dron, dash, EVE y enemigos | Componen muchas filas/propiedades de varias tablas; algunos valores son diferencias contra un baseline empaquetado. Aplicar solo una fracción sería distinto al mod generado. |
| Perfect dodge como alias y tuning de vulnerabilidad enemiga | El primero requiere escribir un `FName`/referencia; el segundo necesita la clasificación de bosses y el conjunto exacto de valores del preset. |
| Reducir recuperación Burst tras esquive perfecto | Limpia arreglos de efectos y condiciones de habilidades. |
| SkinSuit al romperse, modos helper CNS/vanilla y restauración en QTE/campamento | Cambia arreglos de aliases/efectos y depende de helper UE4SS o eventos de equipo que CPS no proporciona. |
| Mini-bosses, densidad/dificultad, variedad enemiga y bosses del mundo | Clona filas, cambia referencias/recompensas y edita tablas de spawns. |
| Daño por caída/muerte ambiental, recuperación automática de barras y recuperación gradual | Cambia acciones, enums, condiciones y arreglos de `EffectTable`. |
| Harder enemies/bosses, perfiles challenge y Hardcore | La selección depende de enums/clasificación de actor y combina Character, Skill, Difficulty y Reward tables. |
| Probabilidad de drops | Requiere leer `DropType` como enum para evitar alterar los pesos relativos de los grupos `RandomWeight`. Las cantidades sí están portadas. |
| Movesets, variantes de assets y outfits | Requieren assets Zen/IoStore y empaquetado en `.pak`, no un cambio de valor runtime. |

Las opciones de esta tabla siguen disponibles por el flujo de build estático y
headless. La página CPS muestra esta limitación en el juego para evitar que se
confunda la cobertura parcial con una selección que no hace nada.

## Implementación y límites de seguridad

El plugin busca de forma única las DataTables `CharacterTable`, `SkillTable`,
`EffectTable`, `ShopItemTable`, `RewardGroupTable`, `SPLevelTable`,
`CharacterLevelTable` e `ItemFishTable`. Las escrituras se limitan a propiedades
`IntProperty`/`FloatProperty` del tamaño esperado. No parsea `.uasset`, no altera
archivos del juego y no crea filas.

Los snapshots identifican tabla + fila + propiedad y conservan el tipo original.
Cada dos segundos se vuelven a buscar tablas cargadas; por eso una tabla que se
cargue al entrar a una zona puede recibir el ajuste más tarde. Una tabla con
objetos homónimos se omite para evitar escribir en la instancia equivocada.
Al descargar el plugin, su callback de CPS intenta restaurar los snapshots en
el hilo del juego.

## Prueba manual en juego

1. Instala el `.cps` compilado y confirma en CPS > Plugins que **Stellar Tool
   Gameplay** aparece como cargado.
2. Abre la página y activa un solo ajuste. Comprueba el estado y los mensajes de
   CPS Logs.
3. Verifica en el juego el comportamiento; algunas tablas solo vuelven a leerse
   tras cargar una zona o reaparecer el personaje.
4. Apaga el ajuste y confirma la restauración; prueba también **Disable all**.
5. Repite con otras opciones de a una y reporta `CPS.log` si un campo falta o el
   juego no refleja el cambio.

El agente compila y valida el empaquetado; solo el usuario puede confirmar el
comportamiento dentro de su instalación real.
