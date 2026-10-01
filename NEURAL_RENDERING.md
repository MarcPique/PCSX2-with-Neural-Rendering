# PCSX2 Neural 0.1.1 — Windows x64

Fork experimental de PCSX2 v2.9.93 con Neural / ReShade en los ajustes nativos.
Proyecto independiente: no es una versión oficial de PCSX2 ni una integración oficial de NVIDIA.

[Descargar EXE autoextraíble o ZIP](https://github.com/MarcPique/PCSX2-with-Neural-Rendering/releases) · [Código fuente](https://github.com/MarcPique/PCSX2-with-Neural-Rendering) · [Validación](docs/neural-rendering/VALIDACION.md)

## Preparación rápida

1. Extrae **toda** la carpeta del ZIP o del EXE autoextraíble. Ejecuta `pcsx2-qt.exe` dentro de ella. El EXE de descarga extrae el paquete completo; las DLL que acompañan al emulador son necesarias.
2. Configura tu BIOS, juegos y mando con el asistente habitual de PCSX2.
3. Abre **Ajustes → Neural / ReShade** y pulsa **Seleccionar Vulkan**. Si tienes un renderizador específico por juego, selecciona Vulkan también en las propiedades de ese juego.
4. Marca **Activar Neural / ReShade**. Si faltan componentes, se descargan automáticamente desde sus autores. El botón **Descargar / reparar componentes** permite repetir la instalación. Para reparar componentes ya cargados, desactiva antes la casilla; PCSX2 puede permanecer abierto.
5. Elige **Equilibrado** y pulsa **Aplicar sin reiniciar**. Después puedes probar **Suave**, **Detalle** o **Cinematográfico**.

Los valores numéricos se ajustan con **14 sliders y etiquetas de valor**; no hace falta escribir números. Hay controles de intensidad, tono, estructura, piel, blanco de referencia, color, transferencia, movimiento y tiempos del Feeder, además de selectores para los modos.

También puedes abrir esta página directamente desde el menú superior **Ajustes → Neural / ReShade** o desde el menú desplegable del botón de ajustes de la barra de herramientas durante una partida.

## Activar y desactivar durante una partida

La casilla principal se aplica directamente. Los demás controles y presets se aplican con **Aplicar sin reiniciar**. El emulador conserva el estado del GS y recrea el dispositivo gráfico; puede haber una pausa breve mientras se cargan o descargan los efectos. No es necesario cerrar PCSX2 ni reiniciar el juego.

El menú de ReShade está bloqueado tanto por teclado como por mando y por su API. Home no abre ese menú. Los ajustes se hacen aquí, también cuando los efectos están activados. Al desactivar la casilla se retiran las capas de esta integración del dispositivo que se vuelve a crear.

Los ajustes neurales son globales para esta carpeta portable. Se guardan en `ReShade.ini`, `ReShadePreset.ini`, `dlss5-feed.cfg` y `neural-rendering.json`. Se conservan las opciones desconocidas y se crean copias `.pcsx2-backup` al escribir archivos existentes. Un fallo al aplicar intenta restaurar la configuración anterior.

## Compatibilidad y diagnóstico

La cadena usa Vulkan, ReShade 6.8.0, Lumenite, DLSS5-Feeder y el componente comunitario RenoDX DLSS5. La evaluación neural requiere una GPU NVIDIA compatible con los runtimes descargados. La validación sintética se realizó con una RTX 4090. Tener las DLL presentes o poder cargar ReShade **no confirma** evaluación neural.

En **Diagnóstico → Actualizar diagnóstico**, busca en el registro de la sesión `inline feature 18 evaluation succeeded`. Los registros pueden conservar datos de una sesión anterior; la página lo indica. La calidad, profundidad, movimiento y coste de GPU dependen del juego. No se ha certificado compatibilidad visual con juegos PS2 en esta release.

Los presets ofrecen distintos valores, pero no garantizan diferencias visibles en todos los juegos ni en todos los modelos. El modo Feeder **Solo transporte** no evalúa el modelo; para neural usa **DLSS + neural**. Los presets rápidos preparan ese modo y la cadena Lumenite → Feeder.

## Contenido y actualización

El paquete incluye el emulador, sus dependencias, traducciones, el bloqueo del menú y el descargador. Los componentes neurales de terceros se descargan localmente; no se redistribuyen en esta release. El descargador conserva licencias, procedencia y hashes. No se incluyen BIOS, juegos ni datos del usuario.

El actualizador oficial se deshabilita en este fork para que no sustituya el ejecutable y elimine estas opciones. Descarga sus actualizaciones desde el repositorio de este fork. Tu instalación habitual de PCSX2 puede mantenerse en otra carpeta.

Código del emulador bajo GPL-3.0-or-later; cada dependencia conserva su licencia en `THIRD-PARTY-NOTICES`. Desarrollo asistido por IA. Los cambios no se han presentado como contribución al proyecto oficial.

## Compilar

Sigue la [guía oficial de PCSX2](https://pcsx2.net/docs/advanced/building/) y usa una compilación fuera del árbol de fuentes. Esta release usa MSVC 14.51, CMake/Ninja, Qt 6.11.2 y el paquete oficial de dependencias con SHA256 `18842cc10521a1be4227d703e6ce4c1ce7bd364544bba479239e584cb2a1cdee`.

En una consola de herramientas x64 de Visual Studio:

```powershell
cmake -S . -B ../build-pcsx2-neural -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/deps -DDISABLE_ADVANCE_SIMD=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
cmake --build ../build-pcsx2-neural --target pcsx2-qt --parallel 8
cmake -S tools/neural-rendering/overlay-guard -B ../build-neural-guard -A x64
cmake --build ../build-neural-guard --config Release
ctest --test-dir ../build-neural-guard -C Release --output-on-failure
```

`tools/neural-rendering/package-windows.ps1` despliega Qt, copia las DLL importadas y las cargadas dinámicamente, añade las licencias y genera el ZIP/EXE. Sus parámetros exigen directorios explícitos para el build, dependencias, guard, avisos, CRT redistribuible y dumpbin. `collect-notices.py` obtiene los avisos originales de los archivos de dependencias fijados por el script oficial, verificando sus SHA256.
