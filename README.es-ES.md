

<p align="center">
  <img src="./assets/readme/hero.svg" width="100%" alt="¡Logdor! el discernidor. Un visor nativo rápido para archivos de registro enormes: filtrado, codificación por colores, anotaciones y más de 30 formatos integrados.">
</p>

<p align="center">
  <img src="./assets/readme/screenshot.png" width="100%" alt="Logdor filtrando un logcat de Android de 9.8 millones de líneas con la consulta de campo tag:vold OR level:error, filas coloreadas por gravedad e histograma de línea de tiempo.">
</p>
<p align="center"><sub>Un logcat real de 9.8M líneas, indexado en ~2 s: consulta de campo <code>tag:vold OR level:error</code>, colores por gravedad, histograma de línea de tiempo.</sub></p>

Logdor es un visor de registros nativo en Qt para archivos demasiado grandes para un editor de texto. Mapea el archivo en memoria, lo indexa rápidamente, detecta automáticamente el formato y analiza solo las filas visibles en pantalla. Así, un registro de 1 GB se abre en aproximadamente dos segundos y filtra sin retrasos.

## Destacados

- **Más de 30 formatos integrados**: logcat, syslog, Apache/nginx, JSON Lines,
  journalctl, Docker, Kubernetes CRI/klog, dmesg, auditd, CEF/LEEF, Snort,
  W3C/IIS y más. Añade los tuyos colocando una [especificación JSON
  declarativa](core/formats/) en `~/.local/share/logdor/formats`; sin necesidad de compilar,
  o créalos de forma interactiva con el Visor de Formato Personalizado con regex en vivo.
- **Lenguaje de consulta de campos**: `level:error tag:Wifi* pid>=100 "texto libre"`
  con AND/OR/NOT (alterna el botón `Q`), además de filtrado por texto plano, regex y
  rangos de tiempo con presets guardados.
- **Línea de tiempo**: un histograma coloreado por gravedad de las filas visibles a lo largo del tiempo;
  haz clic para saltar, arrastra para restringir el filtro a un rango de tiempo.
- **Anotaciones**: haz clic derecho en una línea para adjuntar una nota; las notas se guardan en un
  archivo adjunto (`<log>.logdor.json`) con clave por contenido, por lo que sobreviven
  a reinicios, cambios de nombre y rotaciones, y puedes compartir el archivo adjunto con un
  colega o exportar un informe en HTML/CSV.
- **Línea de tiempo combinada**: intercala eventos de múltiples archivos (incluso en
  diferentes formatos) en una única vista ordenada por tiempo.
- **Cola en vivo**: sigue un archivo en crecimiento (F8); los filtros siguen aplicándose y
  los archivos rotados o truncados se recargan automáticamente.
- **Flujos de trabajo con carpetas**: abre un árbol de directorios y salta entre archivos
  manteniendo su estado de vista intacto, o busca con grep en todo el árbol (incluidos registros comprimidos con gz)
  y salta a cualquier coincidencia. Los archivos `*.gz` se abren directamente.
- **Reglas de resaltado**: colorea las líneas que coincidan con tus patrones en cada visor.

El visor principal renderiza todos los formatos: integrados, definidos por el usuario y
derivados de archivos como tablas CSV, registros W3C/IIS y capturas Chrome NetLog,
mientras que los plugins de visores especializados añaden vistas distintas: volcados hexadecimales,
filtrado de logcat de Android, líneas de tiempo combinadas e incluso el trazado
de coordenadas encontradas en líneas de registro en un mapa. Consulta
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) para obtener la lista completa y saber cómo escribir los tuyos.

## Arquitectura

Dos capas, mantenidas estrictamente separadas:

- **`core/`**: un núcleo funcional sin interfaz gráfica (solo QtCore, ejecutado en tiempo de compilación): acceso a archivos mediante mmap, indexación de líneas, analizadores de formato y detección automática,
  motor de consultas y filtrado/ordenamiento en segundo plano.
- **`app/` + `plugins/`**: la interfaz Qt Widgets y los plugins de visores, envoltorios ligeros
  sobre una vista de tabla compartida y perezosa que analiza solo las filas visibles.

## Compilación

Requiere CMake >= 3.22, Qt >= 6.8 y un compilador C++20.

```bash
cmake -B build        # or /path/to/Qt/6.x/gcc_64/bin/qt-cmake -B build
cmake --build build
build/app/logdor /path/to/file.log
```

Las pruebas unitarias se ejecutan con `ctest --test-dir build -L unit --output-on-failure`.
Las pruebas de rendimiento son opcionales: configura con `-DLOGDOR_ENABLE_BENCH=ON`
y ejecuta la etiqueta `bench` (umbrales: indexación >1000 MB/s, <4.5 bytes/línea,
cancelación <100 ms).

<details>
<summary><b>Atajos de teclado</b></summary>

| Atajo | Acción |
|---|---|
| Ctrl+O / Ctrl+Shift+O | Abrir archivo / carpeta |
| Ctrl+1 ... Ctrl+9 | Abrir un archivo o carpeta reciente |
| Ctrl+PgDn / Ctrl+PgUp | Archivo siguiente / anterior en la carpeta abierta |
| Ctrl+L | Enfocar la entrada del filtro |
| Ctrl+Shift+F | Buscar en carpeta |
| F8 | Seguir el archivo actual (cola en vivo) |
| Ctrl+S / Ctrl+Shift+S | Guardar anotaciones / guardar una copia en otra ubicación |

</details>

---

<p align="center">
  <img src="https://user-images.githubusercontent.com/5616068/173696819-3d5ffdcf-5578-474b-8568-0ea793729328.png" height="220" alt="La mascota de Logdor: un dragón dibujado a mano al estilo de Trogdor the Burninator, con detalles de archivos de registro.">
</p>
<p align="center"><sub>¡Logdor era un hombre! No... ¡era un hombre de registros! O... tal vez solo era un visor de registros. <a href="LICENSE">MIT</a>.</sub></p>
