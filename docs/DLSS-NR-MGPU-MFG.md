# DLSS-NR, MGPU Bridge y MFG en Stellar Blade

## Propósito

Esta nota separa tres tecnologías que suelen aparecer juntas en mods de DLSS,
pero que hacen trabajos distintos:

1. **DLSS-NR** (*Neural Rendering*): procesa la imagen terminada para mejorar
   iluminación, materiales, piel, cabello y tono local.
2. **MGPU Bridge**: mueve ese procesamiento DLSS-NR a una segunda GPU. No es
   SLI y no combina automáticamente la memoria de dos placas.
3. **MFG** (*Multi-Frame Generation*): genera uno o más cuadros intermedios
   para elevar los FPS mostrados. No hace Neural Rendering.

La confusión entre los tres fue la causa de varias pruebas equivocadas en el
equipo con dos RTX 3090.

## Resumen rápido

| Tecnología | Qué hace | GPU adicional | ReShade | ¿Es DLSS-NR? |
|---|---|---:|---:|---:|
| DLSS-NR | Aplica un pase neural a cada cuadro terminado | No | Según la ruta | Sí |
| MGPU Bridge | Ejecuta DLSS-NR en otra GPU y devuelve/presenta el resultado | Sí | Sí | Sí, como transporte |
| MFG | Genera cuadros intermedios a partir de cuadros reales | No necesariamente | No necesariamente | No |

La regla práctica es: **NR mejora el contenido del cuadro; MFG aumenta la
cantidad de cuadros; MGPU Bridge decide en qué GPU se ejecuta NR**.

## 1. DLSS-NR

### Qué es

DLSS-NR no es el reescalado DLSS Super Resolution ni Frame Generation. Es un
pase neural que recibe el frame final —normalmente después del reescalado— y
reconstruye o modifica características visuales de la escena. Su costo depende
mucho de la resolución de salida, aunque el juego renderice internamente a una
resolución menor.

En Stellar Blade el juego es DirectX 12 y trae DLSS nativo. La implementación
comunitaria de DLSS 5/NR se engancha a esa ruta; no convierte a DLSS-NR en una
función oficial del juego. La guía específica indica que Stellar Blade no tiene
soporte oficial de DLSS 5 y que se trata de una integración comunitaria
([guía de Stellar Blade en Nexus Mods](https://www.nexusmods.com/stellarblade/mods/3706?tab=description)).

### Rutas habituales

#### OptiScaler / DLSS-NR

OptiScaler funciona como proxy de DirectX y puede cargar el runtime de NR. Una
instalación típica deja archivos como:

```text
StellarBlade\SB\Binaries\Win64\dxgi.dll
StellarBlade\SB\Binaries\Win64\OptiScaler.ini
StellarBlade\SB\Binaries\Win64\nvngx_dlssnr.dll
StellarBlade\SB\Binaries\Win64\nvngx.dll_dlssnr.dll
StellarBlade\SB\Binaries\Win64\OptiScaler\
```

El parámetro `WorkingScale` puede reducir el costo, pero también reduce el
detalle que ve el modelo. El costo aproximado del pase escala con el cuadrado
de esa escala; no es un interruptor de “modo liviano” gratuito.

#### ReShade + RenoDX/DLSS-NR

La ruta alternativa usa la edición de ReShade con soporte completo de add-ons,
un add-on neural y `nvngx_dlssnr.dll`. Suele dejar, entre otros:

```text
dxgi.dll
ReShade.ini
ReShade.log
renodx-dlss5.addon64     (o el add-on neural elegido)
nvngx_dlssnr.dll
```

No se deben cargar simultáneamente dos consumidores de NGX/NR, por ejemplo
OptiScaler-DLSSNR y un add-on RenoDX que también intercepte la misma función.
Pueden pisar el mismo `dxgi.dll` o enganchar la misma llamada de NGX.

### RTX 3090

El runtime oficial de DLSS-NR está orientado a RTX 50. Para RTX 20/30/40 se
han usado DLL comunitarias parcheadas que eliminan o sortean la compuerta de
hardware. Eso no garantiza que cada versión del modelo funcione de forma
estable en Ampere.

En las pruebas de este equipo, DLSS-NR sí llegó a mostrar actividad real en el
log de OptiScaler sobre una RTX 3090, pero después el juego terminó con
`DXGI_ERROR_DEVICE_HUNG`/`DXGI_ERROR_DEVICE_REMOVED`. Al apagar NR, el proceso
de Stellar Blade permaneció funcionando. Esto demuestra que la ruta se estaba
cargando; no demuestra que sea estable en este juego o con ese runtime.

## 2. MGPU Bridge

### Qué hace

MGPU Bridge es un add-on de ReShade diseñado para que:

1. La GPU primaria renderice Stellar Blade normalmente.
2. El add-on capture el frame terminado.
3. Cree un dispositivo D3D12 en la segunda GPU.
4. Copie el frame entre adaptadores.
5. Ejecute DLSS-NR en la segunda GPU.
6. Presente el resultado desde esa segunda GPU.

El proyecto se describe expresamente como **no-SLI**: no divide el renderizado
del frame ni suma las VRAM. Es una etapa de postprocesado terminal que se puede
ejecutar en otro dispositivo
([README de Neural Coprocessor/MGPU Bridge](https://github.com/maohgad-web/Neural-coprocessor)).

### Requisitos importantes

La configuración soportada por el proyecto incluye:

- Dos GPUs.
- Dos monitores extendidos, uno conectado a cada GPU.
- ReShade 6.8.0 o posterior con soporte completo de add-ons.
- Juego DirectX 12.
- Un runtime y un add-on DLSS-NR compatibles con la GPU secundaria.
- Evitar otros add-ons que sean consumidores de NGX al mismo tiempo.

El README del proyecto lista RTX 50 como requisito principal y RTX 40 con una
DLL parcheada como compatibilidad confirmada. No lista RTX 3090 como hardware
validado. Las pruebas publicadas fueron hechas principalmente con dos RTX 5060
Ti; por eso no se puede extrapolar el resultado directamente a dos RTX 3090.

La instalación reciente del proyecto usa una estructura distinta de la ruta
OptiScaler:

```text
StellarBlade\SB\Binaries\Win64\<add-on MGPU Bridge>.addon64
StellarBlade\SB\Binaries\Win64\mgpu\nvngx_dlssnr.dll
StellarBlade\SB\Binaries\Win64\reshade-shaders\Shaders\mgpu_depth_tap.fx
StellarBlade\SB\Binaries\Win64\mgpu.ini
```

Desde la versión 0.2.0, las instrucciones del proyecto indican que
`nvngx_dlssnr.dll` va dentro de `mgpu\`, no junto al ejecutable. Dejarla junto
al ejecutable puede hacer que algunos juegos la carguen por su cuenta y
provoquen un crash.

### Monitores y latencia

El modo soportado presenta el resultado en una ventana/swapchain de la segunda
GPU. Por eso se necesitan dos monitores extendidos, uno por tarjeta. Con un
solo monitor existe un workaround experimental, pero no es la configuración
medida ni recomendada por el proyecto. El frame además cruza de una GPU a la
otra, por lo que puede aumentar la latencia.

### ¿Serviría con dos RTX 3090?

**Conceptualmente, sí**: dos RTX 3090 son el tipo de configuración de dos
adaptadores que MGPU Bridge intenta aprovechar. La segunda 3090 podría quedar
dedicada al pase NR mientras la primera renderiza.

**Prácticamente, todavía no está confirmado** para esta combinación:

- El proyecto no publica una prueba con 3090.
- El runtime DLSS-NR para Ampere debe ser comunitario/parcheado.
- No hay una prueba publicada con MGPU Bridge específicamente en Stellar Blade.
- Hay que cumplir la topología de dos monitores o aceptar el workaround
  experimental.
- Si falla la inicialización del runtime, moverlo a otra GPU no lo arregla.

Por lo tanto, las dos placas no hacen que el mod funcione automáticamente. Lo
que ofrecen es una ruta experimental para sacar el costo de NR de la GPU que
renderiza el juego.

## 3. MFG y “MFG Bridge”

### Qué es MFG

MFG (*Multi-Frame Generation*) usa cuadros reales, movimiento y otros datos del
motor para insertar varios cuadros generados entre ellos. Puede dar un contador
de FPS mucho más alto, pero no equivale a más cuadros reales renderizados y no
aplica el pase de iluminación/materiales de DLSS-NR.

En RTX 3090 suelen aparecer mods o unlockers comunitarios como:

```text
dlssg_sm86\dlssg_sm86.dll
dlssg_sm86\dlssg_sm86.ini
renodx-mfgunlock.addon64
```

Los nombres exactos dependen del proyecto. Estos archivos pertenecen a la ruta
de Frame Generation/MFG; no son la implementación de MGPU Bridge para mover
DLSS-NR a la segunda GPU.

### Qué no hace MFG

MFG no:

- activa DLSS-NR;
- combina dos RTX 3090;
- suma 48 GB de VRAM para Stellar Blade;
- corrige una DLL `nvngx_dlssnr.dll` incompatible;
- convierte un crash de NR en una instalación estable.

Puede coexistir con NR en algunos juegos, pero añade otra capa de hooks,
buffers y sincronización. Durante el diagnóstico de NR conviene dejarlo
desactivado y probarlo después, una variable por vez.

## 4. Diferencia con los archivos descargados

En `D:\Descargas\MFG Bridge` quedaron tres archivos comprimidos titulados
“Nvidia Mfg Bridge” y un archivo `renodx-mfgunlock.addon64`. Por sus nombres,
corresponden a la familia MFG/Frame Generation; no prueban que se haya
descargado el add-on `MGPU Bridge`/`Neural Coprocessor` para postprocesar NR en
la segunda GPU.

La similitud de nombres es desafortunada:

| Nombre | Función probable | Sustituye a DLSS-NR | Usa segunda GPU para NR |
|---|---|---:|---:|
| `renodx-mfgunlock.addon64` | Desbloqueo de MFG/FG | No | No |
| `Nvidia Mfg Bridge V0.x` | Puente relacionado con MFG/FG | No | No por el solo nombre |
| `file-mgpu_bridge...addon64` | MGPU Bridge/Neural Coprocessor | No: necesita el runtime NR | Sí |
| `nvngx_dlssnr.dll` | Runtime/modelo de DLSS-NR | Es el componente neural | Se ejecuta donde lo cargue el add-on |
| `dxgi.dll` de OptiScaler/ReShade | Proxy/hook | No por sí solo | Depende de lo que cargue |

Antes de instalar cualquier archivo con “Bridge” en el nombre hay que mirar
qué función anuncia el README del proyecto y qué add-on contiene. “MFG Bridge”
y “MGPU Bridge” no son sinónimos.

## 5. Estado de Stellar Blade en este equipo

Después de las pruebas, el juego quedó sin los artefactos de NR/ReShade que se
habían instalado en `SB\Binaries\Win64`:

- `dxgi.dll` de ReShade/OptiScaler: retirado y respaldado.
- `ReShade.ini`: retirado y respaldado.
- `ReShade.log`: retirado y respaldado.
- `ReShadePreset.ini`: retirado y respaldado.
- `nvngx_dlssnr.dll`: no quedó instalado.
- Carpeta `OptiScaler`: no quedó instalada.
- Add-ons RenoDX/MGPU: no quedaron instalados.
- `dwmapi.dll`: se conservó porque pertenece al cargador UE4SS/mod loader.

La instalación de MGPU Bridge, si se decide probar, debe hacerse como una ruta
separada y reversible. No conviene combinarla con OptiScaler-DLSSNR ni con el
MFG unlocker en el primer intento.

## 6. Orden recomendado para una prueba futura

1. Confirmar que hay dos monitores extendidos y que cada GPU puede manejar uno.
2. Elegir una sola ruta neural: MGPU Bridge + ReShade, sin OptiScaler.
3. Usar una versión explícita del add-on MGPU Bridge y verificar su README y
   hash antes de copiarla.
4. Colocar `nvngx_dlssnr.dll` en `mgpu\`, no junto al ejecutable, si esa es la
   regla de la versión instalada.
5. Dejar MFG/Frame Generation desactivado durante el primer arranque.
6. Activar DLSS o DLAA en Stellar Blade para que el motor produzca sus datos
   nativos de movimiento y profundidad.
7. Comprobar `ReShade.log` y el log de MGPU; la evidencia debe ser que el
   bridge se arma y entrega frames, no solamente que aparece un checkbox.
8. Recién después probar MFG, una variable por vez.
9. Si aparece `DXGI_ERROR_DEVICE_HUNG` o `DXGI_ERROR_DEVICE_REMOVED`, retirar
   el bridge completo y restaurar el respaldo antes de cambiar otra cosa.

## Conclusión

Para el objetivo de **DLSS-NR en Stellar Blade con dos RTX 3090**, MGPU Bridge
es la idea técnicamente relevante. MFG es otra función y los archivos MFG
Bridge descargados no la reemplazan. La idea es prometedora, pero la
combinación 2×3090 + MGPU Bridge + Stellar Blade sigue siendo experimental y no
está validada por el proyecto que publicó las pruebas.
