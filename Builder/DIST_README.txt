STELLAR SOULS BUILDER
=====================

Compila tu propio mod de Stellar Souls respondiendo un cuestionario, en vez de
elegir entre decenas de variantes prebuilt. Genera un ZIP para los PAKs y, si hace
falta, otro ZIP para el helper UE4SS, además de las guías en tu idioma.

QUE INCLUYE
-----------
- StellarTool\StellarTool.exe   -> app con interfaz grafica (pestana "Crear mod").
- Builder\                       -> motor de compilacion (Python) + fuentes (vendor).

REQUISITOS
----------
- Windows 10/11.
- Python 3.10+ instalado y en el PATH (para el motor de compilacion).
- No necesita el juego para compilar (las tablas base vienen incluidas en
  Builder\vendor). Si tu antivirus marca los .exe de tools, es un falso positivo.

USO - INTERFAZ GRAFICA
----------------------
1. Ejecuta StellarTool\StellarTool.exe
2. Abri la pestana "Crear mod" (icono de herramienta).
3. La app detecta tu Stellar Blade automaticamente (Steam).
4. Responde: perfil de combate, outfit, mini-bosses, helper, idioma, carpeta.
5. Opcional (si detecto el juego): marca "Instalar el mod en ~mods" y/o
   "Instalar y activar el helper" para que la app lo instale sola (edita mods.txt).
    Cerra Stellar Blade antes. Si no marcas nada, se generan los ZIP necesarios.
6. "Compilar mi mod" -> `StellarSouls-Custom-<id>-Mod.zip` y, si hace falta,
   `StellarSouls-Custom-<id>-Helper.zip` en la carpeta elegida.
7. Para Vortex, importa los ZIPs por separado, activa el helper en UE4SS Load Order
   y desplegá. El ZIP del helper contiene `NombreDelHelper\Scripts\*.lua` en la
   raíz para que Vortex lo reconozca como mod de script UE4SS.

HISTORIAL / PLANTILLAS
----------------------
Cada mod que compilas queda guardado. En "Configuraciones anteriores" podes:
- "Usar de plantilla": carga esa config para editar y armar una parecida.
- "Re-exportar ZIP": recupera el zip (o lo recompila si lo borraste).

APARIENCIA Y MUSICA
-------------------
- Settings -> Apariencia: modo Oscuro / Claro.
- Boton "Old school music" (arriba a la derecha en "Crear mod"): pone un chiptune
  estilo keygen (generado para este tool, CC0). Se agradece en Settings -> Shoutouts.

USO - LINEA DE COMANDOS
-----------------------
Cuestionario interactivo (10 idiomas):
    python Builder\compiler\questionnaire.py --lang es

Directo con respuestas:
    python Builder\compiler\build_custom.py --out <carpeta> --answers @respuestas.json

respuestas.json:
    {
      "combatProfile": "full",        (none | full | firstRun)
      "outfitMode": "helper",         (off | helper | noHelperAlpha)
      "miniBoss": "allRegions",       (off | allRegions | greatDesert)
      "miniBossDensity": "p20",       (p10 | p20 | p33)
      "helperMode": "randomPeriodic",  (last | randomAny | randomPeriodic | lastNoCns)
      "helperIntervalSeconds": 30,
      "lang": "es"
    }

QUE GENERA
-----------------
- `*-Mod.zip` contiene los PAKs para Vortex o `StellarBlade\SB\Content\Paks\~mods`.
- `*-Helper.zip` contiene el script UE4SS separado para Vortex o
  `StellarBlade\SB\Binaries\Win64\ue4ss\Mods` (cuando la selección requiere helper).
- El ZIP de helper se importa como un mod independiente; activá el helper en
  **UE4SS Load Order** antes de desplegar.
- `INSTALL_<idioma>.txt` y la guía `*-Helper-INSTALL.txt` detallan los pasos.

Las instrucciones completas están en la guía del ZIP del mod y en el archivo
`*-Helper-INSTALL.txt` que acompaña al ZIP del helper.
