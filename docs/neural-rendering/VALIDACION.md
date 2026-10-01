# Validación de PCSX2 Neural 0.1.0

## Alcance comprobado

- Compilación Release x64 de PCSX2 con los controles nativos y la transacción de recreación del GS.
- Pruebas de configuración: escritura atómica, conservación de claves, validación de manifiestos/rutas Unicode, rechazo de componentes incompletos y preparación repetida del entorno en el mismo proceso.
- Prueba Qt sin ventana visible: 14 sliders, ausencia de campos numéricos editables, persistencia de valores, precisión conservada en valores no modificados y cuatro presets. Usa un controlador simulado para aislar la interfaz; no prueba el núcleo de emulación.
- Prueba del add-on: rechaza abrir el menú por teclado, mando o API y permite cerrarlo.
- Paquete portable: 59 binarios PE con dependencias resueltas y CRT local. Arranque `-testconfig` desde una extracción limpia del ZIP, sin rutas del entorno de desarrollo: salida 0, sin interfaz visible ni audio. El paquete público se revisa para excluir configuraciones personales y componentes neurales descargados.
- Prueba con Vulkan real, ventana oculta y sin audio, en una NVIDIA RTX 4090: **OFF → ON → OFF → ON → OFF** en un único proceso. Se presentan fotogramas sintéticos a 624×441, se verifica la descarga del guard después de destruir Vulkan y se exige evaluación neural exitosa en ambos ciclos ON. También se verifica que se recargan intensidad 0.6/1.0 y estilo 1/2 entre ellos.

El ciclo final completó los cinco pasos y terminó normalmente. Los dos ciclos ON registraron `inline feature 18 evaluation succeeded`. Esto demuestra carga, evaluación, recarga de parámetros y descarga del runtime en ese equipo; no demuestra calidad de imagen, rendimiento o compatibilidad de juegos PS2.

## Corrección de ciclo de vida

Se desactivan los hooks gráficos D3D/OpenGL de ReShade para este proceso y se utiliza su capa Vulkan explícita. Así el dispositivo D3D12 privado de Feeder no mantiene una referencia adicional de ReShade al cerrar Vulkan. Los hooks NGX del add-on siguen evaluando el modelo. También se desactivan los hooks de entrada y red; el add-on adicional bloquea el menú.

En PCSX2 la aplicación se serializa con el hilo CPU y pasa por el hilo GS cuando está abierto. Los INI se escriben después de cerrar el dispositivo antiguo, evitando que ReShade sobrescriba los nuevos valores al cerrarse. `GSreopen` conserva/restaura el estado gráfico y recupera la configuración anterior si no puede abrir el nuevo dispositivo. La descarga de datos sin sincronización recibe la misma espera del GS que los ajustes gráficos existentes.

## Límites pendientes de comprobación por el usuario

No se arrancaron juegos PS2 ni se utilizaron BIOS. La conservación del estado de una partida real y el resultado visual de cada modo deben comprobarse con los juegos del usuario. El cambio puede pausar la imagen mientras recompila shaders o carga el modelo. La prueba sintética no mide FPS ni garantiza que todos los juegos aporten profundidad o vectores de movimiento útiles.

## Repetir las pruebas

```powershell
cmake -S tests/neural-rendering -B ../build-neural-tests -A x64 -DCMAKE_PREFIX_PATH=C:/deps -DVulkan_INCLUDE_DIR=C:/VulkanSDK/Include -DVulkan_LIBRARY=C:/VulkanSDK/Lib/vulkan-1.lib
cmake --build ../build-neural-tests --config Release
ctest --test-dir ../build-neural-tests -C Release --output-on-failure
```

Para `neural-ui`, CTest configura `QT_QPA_PLATFORM=offscreen`. Qt necesita sus DLL y plugins en la ruta de ejecución. La prueba GPU es voluntaria: copia `neural-vulkan-cycle.exe` y sus dependencias en una **carpeta desechable**, instala allí los componentes con el descargador y copia `pcsx2-settings-only.addon64`. Ejecuta oculto con `--disposable-runtime`. Escribe y reemplaza los ajustes de esa carpeta; no se debe ejecutar sobre una instalación personal.
