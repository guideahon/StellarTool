# Plugin CPS para Stellar Tool

El proyecto `CPSPlugin/` crea un plugin complementario para el menú CPS. No
convierte la aplicación Qt en una DLL ni reemplaza Stellar Tool desktop: permite
cambiar en juego un primer grupo de valores numéricos del jugador.

## Requisitos y compilación

- Windows x64 y Visual Studio Build Tools 2022 o posterior.
- CMake 3.20 o posterior.
- CPS SDK local en `CPS-SDK/` junto al proyecto. El SDK lo aporta el usuario y
  no se incorpora al paquete desktop.
- CPS 1.2.0 o posterior instalado en Stellar Blade.

Desde la raíz del repo, ejecutar:

```bat
CPSPlugin\build.bat
```

El resultado es `build\CPSPlugin\Release\stellar_tool_gameplay.cps`. También se
puede indicar otra ubicación del SDK al configurar CMake con
`-DCPS_SDK_DIR="C:\ruta\CPS-SDK"`.

Para probarlo, copiar el `.cps` a `SB\Binaries\Win64\CPS\Plugins`, iniciar el
juego y abrir el menú CPS con F10. La página aparece como **Gameplay** en inglés
o **Jugabilidad** en español. El plugin no se despliega automáticamente ni
modifica el juego al compilarse.

## Controles incluidos

- Vida máxima, escudo máximo y reducción de daño mientras EVE tiene escudo.
- Regeneración de escudo fuera y dentro de combate.
- Velocidad de ataque.
- Capacidad de las barras Beta, Burst y Tachy.
- Tamaño de pilas de munición y consumibles.
- Regeneración pasiva de vida, curación al golpear y poder de pesca.

Cada grupo tiene un toggle y sus valores usan los mismos campos que las opciones
correspondientes del Builder. Las selecciones y valores quedan en
`CPS/Configs/StellarToolGameplay/settings.json`. El botón **Disable all and
restore captured values** (o su traducción) desactiva los grupos y restaura los
valores que el plugin guardó al aplicarlos.

El plugin cambia la tabla cargada en memoria; el momento en que un cambio afecte
al personaje depende de cuándo Stellar Blade vuelva a consultar ese dato.

## Seguridad y alcance

Solo se busca un `CharacterTable` único y su fila `Player`. Antes de cada cambio
se comprueban el tipo y tamaño numérico de la propiedad. Los arrays, structs,
enums, efectos, filas, assets, spawns, equipamiento y outfits no se modifican.

Al restaurar, el plugin compara el valor actual con el último que escribió. Si
otro plugin lo cambió, no sobrescribe ese cambio y deja el detalle en `CPS.log`.
Al descargar el plugin, CPS ejecuta su callback de limpieza en el hilo de juego
y el plugin intenta restaurar los campos que controla. Si el juego o la tabla no
están disponibles, el resultado queda registrado en ese mismo log.

El código compila, pero la primera validación dentro del juego le corresponde al
usuario: la presencia y el layout de la tabla pueden variar según la versión del
juego. Si CPS muestra el plugin como fallido, revisar la entrada
`Stellar Tool Gameplay` en la página Logs de CPS antes de volver a activarlo.

## Desarrollo y licencias

La integración enlaza los helpers `cps_sdk_ue` y `cps_sdk_log` del SDK. El
paquete `.cps` incluye las licencias/notas exigidas por el SDK; el ejecutable
`cps_pack.exe` y el resto del SDK no se redistribuyen.
