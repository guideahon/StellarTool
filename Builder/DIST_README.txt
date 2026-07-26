STELLAR SOULS BUILDER
=====================

Compila tu propio mod de Stellar Souls respondiendo un cuestionario, en vez de
elegir entre decenas de variantes prebuilt. Genera el pak + el helper (si hace
falta) + una guia de instalacion en tu idioma, todo en un ZIP.

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
   Cerra Stellar Blade antes. Si no marcas nada, se genera solo el ZIP.
6. "Compilar mi mod" -> StellarSouls-Custom-<id>.zip en la carpeta elegida.
7. Si no instalaste directo, segui el INSTALL_<idioma>.txt dentro del ZIP.

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
      "outfitSkinSuit": true,
      "miniBoss": "allRegions",       (off | allRegions | greatDesert)
      "miniBossDensity": "p20",       (p10 | p20 | p33)
      "helperMode": "randomPeriodic",  (last | randomAny | randomPeriodic)
      "helperIntervalSeconds": 30,
      "lang": "es"
    }

QUE GENERA EL ZIP
-----------------
- Paks\                              -> copiar a StellarBlade\SB\Content\Paks\~mods
- ue4ss\Mods\StellarSoulsOutfitRestore\  -> (si elegiste outfit) copiar a
  StellarBlade\SB\Binaries\Win64\ue4ss\Mods\   (requiere UE4SS + CNS)
- INSTALL_<idioma>.txt               -> instrucciones exactas.

El detalle de cada archivo esta en la guia incluida en cada ZIP compilado.
