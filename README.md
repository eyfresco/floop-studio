# SLOOP Studio — FL Edition (prototipo 1)

Secuenciador de 4 pistas y controlador MIDI para FL Studio, inspirado en SLOOP para el FM-1.
**No produce sonido**: manda notas MIDI a los instrumentos de FL que elijas (FPC, FLEX, Serum, otros VST).
El FM-1 es opcional y funciona como teclado y controlador físico.

![DRUMS](docs/ui-drums.png)

| Pista | Qué hace | Canal MIDI por defecto |
|---|---|---|
| DRUMS | 16 sonidos × 16 pasos (el orden de carriles de SLOOP: kick, kick 2, snare, clap, hat…) en notas GM | 10 |
| SYNTH 1–3 | Cuadrícula de 2 octavas × 16 pasos, acordes permitidos, OCT −/+ para moverte | 1, 2, 3 |

Por pista: **PASOS** (1–16, patrones de largo distinto se desfasan como polirritmo), **GATE**, **VELOCIDAD**, **CANAL MIDI**, **M** (mute) y **LIMPIAR**.
Los pasos siguen la posición de FL (1 paso = semicorchea), así que siempre caen en la rejilla, también después de un loop, un salto o un cambio de tempo.

### Niveles y roll en DRUMS (como en el FM-1)

- **Clic:** pone o quita un golpe (nuevo = NORM, x1).
- **Shift+clic** en un golpe: un nivel más fuerte, GHOST → SOFT → NORM → HARD. Se ve por la intensidad del color; HARD lleva borde blanco. Velocidades: GHOST 35, SOFT 70, NORM = VELOCIDAD de la pista, HARD 127.
- **Alt+clic** en un golpe: roll x1 → x2 → x3 → x4 (golpes repartidos dentro del paso; se ve dividido).

### FM-1 en vivo (como el SLOOP Editor)

El botón **FM-1 EN VIVO** (arriba) conecta SLOOP Studio con el FM-1 por USB y los mantiene sincronizados con el mismo protocolo SysEx que usa el SLOOP Editor web:

- **Al conectar**, SLOOP Studio toma el patrón que tiene el FM-1: los golpes de DRUMS con nivel y roll, las notas de SYNTH 1–3, y el largo y el mute de cada pista. Si ya tenías algo pintado, pide confirmación antes.
- **Plugin → FM-1:** cada cambio que hagas aquí se escribe en el FM-1 al momento.
- **FM-1 → plugin:**
  - Lo que cambies en el equipo (grabar, borrar, editar pasos, cargar un proyecto) aparece aquí.
  - La pista que el FM-1 tiene seleccionada se actualiza al instante.
  - Las otras se releen cada 2,5 s, porque el FM-1 solo avisa de la pista seleccionada.
- **La pista seleccionada también se sincroniza**, como en el SLOOP Editor: elegir una pestaña aquí la selecciona en el FM-1, y elegir una pista en el FM-1 cambia la pestaña aquí. Al conectar, el plugin toma la que tiene el FM-1.
- Si los dos cambian el mismo paso a la vez, gana el plugin.
- El estado se ve en verde/amarillo/rojo sobre la cuadrícula. Si se desconecta, reintenta cada 2 s.
- El enlace es uno solo para todas las instancias de SLOOP Studio y sigue activo aunque cierres la ventana del plugin.

Límites del FM-1 en este modo:
- Máximo **4 notas por paso** en los synths; la 5.ª se rechaza con un aviso.
- Las notas ligadas (TIE) aún no se muestran.
- Se sincronizan los primeros 16 pasos.
- Al escribir un paso de synth desde aquí, sus acentos, slides y niveles por nota quedan en NORM.

Dos secuenciadores, un patrón: el FM-1 sigue tocando su patrón con su sonido, y SLOOP Studio toca el mismo patrón por MIDI hacia tus instrumentos de FL. Usa uno, el otro o ambos (para ambos, falta mandar el reloj de FL al FM-1: pendiente).

Para probarlo sin FL:
- `build/sloop_fm1_probe_artefacts/Release/sloop_fm1_probe` lee el FM-1 una vez.
- `... sloop_fm1_probe vivo 30` muestra en la terminal, durante 30 s, cada cambio que hagas en el equipo. Solo escucha: no escribe nada en el FM-1.

### SYNC FL, ▶ FM-1 y REC

- **SYNC FL:** manda el tempo y el PLAY/STOP de FL al FM-1 como reloj MIDI, y pone **SYNC = USB** en el FM-1 sin tocar el equipo. Al apagarlo, detiene el FM-1 y devuelve SYNC a como estaba. No hace falta activar *Send master sync* en FL.
  - Con FL parado, el FM-1 sigue el tempo pero no avanza.
  - Con PLAY en FL, el FM-1 arranca **en la primera barra de compás**: al instante si FL empieza en una, o en la siguiente (aviso *FM-1 entra en el próximo compás*). SLOOP no entiende posiciones de canción, solo START desde su inicio.
  - Si FL vuelve atrás en un loop, el FM-1 se reinicia con él.
  - Funciona aunque FM-1 EN VIVO esté apagado.
- **▶ FM-1** (con FM-1 EN VIVO): el PLAY/STOP del FM-1, como su botón. Un plugin no puede arrancar el transporte de FL; para eso está el PLAY de FL con SYNC FL.
- **REC:**
  - Con FM-1 EN VIVO es el REC del FM-1, como su botón, y se ilumina con el estado del equipo. Si el FM-1 está tocando su canción (modo canción), no deja grabar y el plugin lo avisa.
  - Sin enlace graba en SLOOP Studio lo que tocas en el FM-1, como antes.

### El PLAY del FM-1 controla FL (y al revés)

- **FL → FM-1:** con **SYNC FL**, el PLAY y STOP de FL arrancan y detienen el FM-1.
- **FM-1 → FL:** con **FM-1 EN VIVO** y **PLAY del FM-1 → FL** activados (abajo, en PATRÓN), el PLAY y STOP del FM-1 arrancan y detienen FL.
  - Un plugin no puede mover el transporte de FL, así que lo hace un **script de controlador** de FL: el plugin crea un puerto MIDI virtual **"SLOOP Studio"** y por él manda CC 118 (play) / CC 119 (stop) en el canal 16.
  - Solo avisa cuando el FM-1 cambió por su cuenta: si FL ya está así (su propio PLAY lo arrancó), no manda nada, así que no hay eco.

Configurarlo una vez en FL:
1. El script ya está instalado en `Documentos/Image-Line/FL Studio/Settings/Hardware/SLOOP Studio/device_SLOOP_Studio.py` (copia en `Scripts/FL/`).
2. Con SLOOP Studio cargado, ve a **Options → MIDI Settings → Input**: selecciona **SLOOP Studio**, en *Controller type* elige **SLOOP Studio** y pulsa **Enable**. Si no aparece, usa *Rescan devices*.
3. Activa **SYNC FL** para que el FM-1 vuelva a alinearse con el compás de FL.

Límites:
- FL arranca hasta ~¼ s después, porque el plugin lee el estado del FM-1 4 veces por segundo.
- El FM-1 empieza en cuanto pulsas su PLAY y, con SYNC FL, se realinea al compás de FL al arrancar este.

### SONIDOS: samples y kits para el FM-1 (USR1–USR4)

La pestaña **SONIDOS** (junto a PATRÓN) hace lo mismo que las páginas *Samples* y *Drum kit* del SLOOP Editor: lleva tus sonidos a los 4 espacios del FM-1, de unos 7,4 s cada uno.

![SONIDOS](docs/ui-sonidos.png)

- **INSTRUMENTO:** hasta 16 archivos (WAV, AIFF, MP3, FLAC…). Cada uno suena en su nota y las teclas de en medio tocan el más cercano.
  - La nota se toma del nombre (`PIANO_C4.wav`) o se ajusta con − / + (Shift: una octava).
  - En el FM-1: una pista de synth con el motor SAMPLE y SET = USRn.
- **KIT DE BATERÍA:** 16 pads, uno por tecla blanca (KICK en F3 … COWBELL en G5).
  - Si sueltas varios archivos, se reparten por su nombre (kick, bd, snare, hh, open, crash…).
  - Si el kit no cabe, los sonidos más largos se recortan con un fundido corto.
  - Con *Usar en DRUMS al enviar*, la pista DRUMS pasa a tocar KIT = USRn.
- **ENVIAR A USRn:** borra ese espacio en el FM-1 y sube los sonidos. Antes muestra qué hay en ese espacio y pide confirmación.
  - **LEER FM-1** actualiza la lista de lo que hay en cada espacio (solo lectura).
  - Mientras sube, el enlace en vivo se pausa y luego sigue solo.
- El formato es el de SLOOP, byte por byte: mono, 22050 Hz, IMA ADPCM. `sloop_sample_tests` lo compara con la salida del propio `sampleio.py` de SLOOP.

## Grabar el audio del FM-1 en FL

Por USB, el FM-1 también es una **entrada de audio** (*Felucca*, estéreo, 44,1 o 48 kHz, sin driver):

1. **Configuración de Audio MIDI** (Mac): crea un **Dispositivo agregado** con *Felucca* (entrada) y tus parlantes o interfaz (salida). En Mac, FL usa un solo dispositivo de audio para entrada y salida, y el FM-1 solo tiene entrada.
2. **FL → Options → Audio Settings:** elige ese dispositivo agregado.
3. **Mixer:** en un insert libre, elige la entrada *Felucca*, arma la grabación, dale REC en FL y toca.

Con **SYNC FL** activado, el FM-1 toca a tiempo con FL, así que lo grabado cae en su lugar. Puede haber unos milisegundos de retraso por USB: mueve el clip o usa la compensación de latencia de FL.

## Instalar

Ya está compilado e instalado en esta Mac:

- `~/Library/Audio/Plug-Ins/VST3/SLOOP Studio.vst3`
- `~/Library/Audio/Plug-Ins/Components/SLOOP Studio.component` (AU)

En FL Studio: **Options → Manage plugins → Find installed plugins** (o *Find more plugins*). Aparece como generador **SLOOP Studio**.

Para recompilar después de un cambio:

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j8
```

(La primera vez necesita JUCE en `external/JUCE`: `git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE external/JUCE`.)

## Conectarlo en FL Studio

FL da **un solo puerto MIDI de salida por plugin**, y el instrumento que escucha ese puerto recibe todos los canales.
Por eso hay dos maneras de usarlo:

> **Probado en FL Studio 2026 (Mac), 16 nov 2026:** SLOOP Studio (VST3) → **Surge XT** por el puerto 0 suena y sigue el transporte; mutear DRUMS en el plugin silencia la salida.
> Los instrumentos **nativos** de FL (FPC, FLEX, 3x Osc…) **no tienen puerto de entrada MIDI**: el enrutamiento por puertos solo llega a plugins VST/AU/CLAP (Serum, Surge XT, Vital, Kontakt…).
> Para FPC o FLEX hay que usar **Patcher** (cablear la salida MIDI de SLOOP Studio a FPC dentro de Patcher). Eso **aún no está probado**.

### A. Una instancia → un instrumento VST (para empezar)

1. Agrega **SLOOP Studio** al Channel Rack.
2. Abre el engrane del wrapper → pestaña de ajustes → sección MIDI → **Output port**. Gira la rueda del mouse sobre el campo: pasa de `---` a `0`.
3. Agrega un instrumento VST/AU/CLAP (p. ej. Surge XT, Serum). En su wrapper, **Input port = 0** (el mismo número).
4. Pinta un ritmo en DRUMS y dale **Play** en FL.

### B. Cada pista a un instrumento distinto

Todas las instancias de SLOOP Studio **comparten el mismo patrón**: edites en la que edites, cambia en todas.
Cada una decide qué manda con **SALIDA DE ESTA INSTANCIA** (arriba a la derecha).

| Instancia | Salida | Output port | Instrumento | Input port |
|---|---|---|---|---|
| SLOOP Studio #1 | Solo DRUMS | 1 | batería VST (o FPC vía Patcher) | 1 |
| SLOOP Studio #2 | Solo SYNTH 1 | 2 | Serum | 2 |
| SLOOP Studio #3 | Solo SYNTH 2 | 3 | Surge XT | 3 |
| SLOOP Studio #4 | Solo SYNTH 3 | 4 | Vital | 4 |

La pestaña de una pista que no sale de esa instancia dice *no sale aquí*.

### El FM-1 como controlador

1. Conecta el FM-1 por USB. En **Options → MIDI Settings → Input**, activa su puerto (se llama *SLOOP*, *Felucca* o *FM-1*), con *Controller type: generic*.
2. FL manda el teclado al canal seleccionado del Channel Rack. Selecciona la instancia de SLOOP Studio que **sale** por la pista que quieres tocar.
3. Lo que tocas suena en la **pista seleccionada** (la pestaña resaltada), en su canal:
   - En DRUMS, cada tecla blanca es un sonido, desde F3 = KICK, como en SLOOP. Las negras tocan la blanca de su izquierda.
   - Las perillas (CC), el pitch bend y la presión van al canal de la pista seleccionada.
4. **REC** + Play en FL: lo que tocas se graba en el paso más cercano.

La luz **MIDI IN** parpadea cuando llegan notas y **PLAY FL** se enciende con el transporte de FL.

## Qué está verificado

- Pruebas del motor (`Tests/CoreTests.cpp`): pasos exactos en la rejilla con cualquier tamaño de buffer, gate, acordes, inicio a mitad de compás, loops, stop, mute, salida por pista, largo de patrón, cambio de tempo y guardado del estado.
- `auval` de Apple: **AU VALIDATION SUCCEEDED**.
- `sloop_host_check` carga el **VST3 compilado** como lo haría un host, toca un compás y comprueba el MIDI: 4 kicks, 4 hats y el acorde, con el kick en la muestra 0. Una instancia *Solo DRUMS* no deja salir el synth.
- `sloop_ui_snapshot` dibuja la interfaz (las imágenes de `docs/`).

- **En FL Studio 2026:** el plugin carga como VST3, sigue el transporte de FL (luz PLAY FL y cursor de pasos) y su MIDI llega a Surge XT por el puerto 0.

**Aún no probado:** FPC o FLEX a través de Patcher, el modo de varias instancias con puertos distintos, y el FM-1 conectado.

- **Con un FM-1 real (SLOOP 2.8.5, puerto Felucca), 16 nov 2026:**
  - Lectura: el patrón completo en 0,6 s, igual al que muestra el SLOOP Editor.
  - Modo vivo: 10 s conectado sin errores.
  - Escritura: OPEN HAT HARD x3 escrito, verificado y restaurado (`sloop_fm1_probe escritura`).

- **Reloj con el FM-1 real** (`sloop_fm1_probe reloj`): FL parado = FM-1 en STOP a 120 BPM; PLAY = FM-1 en PLAY a 120 BPM; STOP = FM-1 en STOP; SYNC restaurado al final.
- **▶ FM-1 con el FM-1 real** (`sloop_fm1_probe transporte`): PLAY → STOP → PLAY, y el FM-1 quedó como estaba.
- **REC con el FM-1 real** (`sloop_fm1_probe transporte`): REC encendido y apagado, y el estado se refleja en el plugin.
- **Samples con el FM-1 real:** el formato coincide byte por byte con `sampleio.py` de SLOOP. Se subió un tono de prueba a USR2 (`sloop_fm1_probe subir 2 PRUEBA …`) y el FM-1 lo aceptó (CRC correcto); USR1, USR3 y USR4 no se tocaron.
- **Envío a la FM-1 por la API MIDI 1.0 de macOS:** JUCE 8 envía por la API MIDI 2.0 de CoreMIDI, y macOS la convierte para el puerto USB-MIDI 1.0 del FM-1. En ese camino, los SysEx largos de una subida llegaban dañados (el FM-1 los rechazaba por CRC). Por eso en Mac el plugin envía con `MIDISend`, como Chrome con el editor web (`Source/MacMidiOut.cpp`). Después del cambio se repitieron, con el FM-1, las pruebas de escritura de pasos, pista seleccionada y reloj, y todas pasaron.
- **Puente PLAY con el FM-1 real** (`sloop_fm1_probe puente`, escuchando el puerto desde otro programa): FM-1 STOP → PLAY = CC 118; PLAY → STOP = CC 119; sin eco cuando "FL" ya estaba igual. El script de FL compila; **falta probarlo dentro de FL**.
- **Pista seleccionada con el FM-1 real** (`sloop_fm1_probe pista`): pestaña SYNTH 1 → el FM-1 selecciona SYNTH 1; de vuelta a DRUMS → el FM-1 vuelve a DRUMS.

**Aún no probado:** el botón FM-1 EN VIVO dentro de FL (el enlace es el mismo código que usa `sloop_fm1_probe vivo`), y una edición hecha en el FM-1 mientras el enlace está activo.

## Pendiente (siguientes prototipos)

- Mapear las perillas y botones del FM-1 a la interfaz: elegir pista, mute, REC, OCT, y modo REMOTE de SLOOP.
- Notas por paso: velocidad o acento, ratchets, microtiming y parameter locks, como en SLOOP.
- Secciones A–D, modo canción, módulo FX y nota editable por carril de batería (para kits de FPC que no usan GM).
- Interfaz más cercana al hardware.

## Licencia

El código de este proyecto se publica como GPL-3.0. Reutiliza ideas y la tabla de carriles de batería de [SLOOP](https://github.com/isod89/sloop-fm1) (GPL-3.0).
JUCE 8 se distribuye bajo AGPLv3 o licencia comercial. Para uso personal no hay problema; para distribuir el plugin hay que cumplir una de las dos.
