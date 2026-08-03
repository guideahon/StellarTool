StellarToolLive
===============

Bridge de UE4SS que instala Stellar Tool desde la pagina "Live". Codigo propio
de Stellar Tool, sin componentes de terceros.

Que hace
--------
Aplica en vivo, mientras el juego corre:
  - FOV de camara (40-170 grados)
  - Multiplicador de velocidad de movimiento
  - Multiplicador de fuerza de salto

Como funciona
-------------
Solo reflection de UE4SS: resuelve las propiedades por nombre sobre el
PlayerController vivo (PlayerCameraManager y CharacterMovementComponent). No
usa offsets ni firmas de memoria, asi que no queda atado a una version puntual
del juego: si una property no existe, esa funcion queda apagada y el resto
sigue andando.

Lo que NO hace: hooks, key binds, lectura de UObjects en background, escritura
del save, inventario, dinero, equipamiento, progresion.

Requisitos
----------
UE4SS instalado por separado en SB/Binaries/Win64/ue4ss.

Archivos de estado
------------------
live_request.txt   lo escribe Stellar Tool  (seq, fov, speed, jump)
live_status.txt    lo escribe este bridge   (beat, ready, seq, valores vivos)

Ambos se publican de forma atomica (.tmp + rename). Borrar live_request.txt
deja el bridge en idle sin tocar el juego.

Desinstalar
-----------
Boton "Desinstalar bridge" en la pagina Live, o borrar esta carpeta entera.
Los valores vuelven a los del juego al reiniciarlo.
