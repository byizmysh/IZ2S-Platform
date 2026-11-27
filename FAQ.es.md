# Preguntas frecuentes

[English](FAQ.md) · [Русский](FAQ.ru.md) · [简体中文](FAQ.zh.md) · [Español](FAQ.es.md)

[Guía completa de instalación (en inglés)](docs/INSTALLATION.md)

## 1. ¿Qué necesito para la primera instalación?

Una Nintendo Switch 2 con firmware **23.0.1**, un **cartucho de Civilization VII 1.0.0 para Nintendo Switch 1**, una microSD, Linux nativo con un adaptador Wi-Fi compatible y tu propio `prod.keys`, obtenido de tu propia Switch 1. Antes de abrir el juego, selecciona English para la interfaz y English (US) para el teclado USB. Esta edición solo incluye un instalador para Linux.

## 2. ¿Qué idiomas debo seleccionar en la consola?

Interfaz: `HOME → Configuración de la consola → Consola → Idioma → English`. Teclado: `HOME → Configuración de la consola → Mandos y accesorios → Teclado USB → English (US)`. Hazlo antes de iniciar el juego. Windows y WSL no son entornos de instalación admitidos en esta edición.

## 3. ¿Dónde debo extraer el archivo SD?

Descomprime `IZ2S-0.3.5-linux.zip` en el PC. Encontrarás `iz2sciv-core-sd.zip` junto a `install.sh`. Extrae el archivo de la SD en la raíz de la tarjeta para que las rutas comiencen por `SD:/iz2s/`. Puedes descargar otra copia desde el panel. Al actualizar, combina las carpetas y sustituye los archivos coincidentes del paquete. Conserva `Nintendo`, las otras aplicaciones, BIOS, juegos, partidas y ajustes. Mantén las licencias incluidas y expulsa la tarjeta de forma segura.

## 4. ¿Cuándo repito el procedimiento completo?

Reutiliza una partida compatible tras actualizar. Repite el procedimiento completo si hay que sustituirla, al cambiar desde otro cargador o tras más de siete intentos fallidos. En el paso 3, borra todos los datos de guardado de Civilization VII del perfil seleccionado en `HOME → System Settings → Data Management → Delete Save Data → Civilization VII → Your profile`.

## 5. ¿Debo instalar la partida manualmente después de conectar?

No. **Conectar e instalar la partida** restaura el Wi-Fi del PC, verifica el cargador e instala automáticamente `IZ2SCIV.Civ7Save`. Mantén abierta la pantalla Awaiting payload hasta que termine. Si falla, el panel ofrece reintentar; una escritura sin confirmación no se considera correcta.

## 6. ¿La instalación automática elimina mis otras partidas?

La instalación automática solo sustituye `IZ2SCIV.Civ7Save`. Si repites el procedimiento completo, en el paso 3 borra todos los datos de guardado de Civilization VII del perfil seleccionado en la configuración de la consola.

## 7. No se encuentra la IP de la consola. ¿Qué hago?

Abre los ajustes de IP justo debajo de **Estado de la sesión**. Busca y verifica una dirección propuesta o introduce la IP mostrada en la consola. Mantén Awaiting payload abierto y comprueba que se ha restaurado el Wi-Fi del PC. Un servidor FTP disponible no demuestra que el cargador del juego esté preparado.

## 8. ¿Cómo inicio después de instalar?

Vuelve a HOME, cierra Civilization VII con X, abre el juego de nuevo y elige Load Game sin reiniciar la consola. Sigue las indicaciones hasta IZ2S CORE IS RUNNING, cierra Civilization VII y mantén R al abrir un juego de Switch 2. Repite una vez el inicio con R si hace falta. Los pasos completos de recuperación están en la [guía](docs/INSTALLATION.md#4-launch-iz2s).

## 9. ¿Cómo configuro PC Link o FTP?

Para PC Link, abre **Inicio → Configurar PC Link**, introduce la dirección IPv4 local de la consola y pulsa **Iniciar PC Link**. Descarga `pc-link.cfg` y guárdalo en `SD:/iz2s/pc-link.cfg`. Vuelve a abrir IZ2S y pulsa **R + L3**. Conecta el PC y la consola a la misma red local. Si otro panel de IZ2S usa PC Link, páusalo allí primero.

Para FTP, pulsa **L + R3** en la consola y luego **Conectar FTP** en el panel.

## 10. ¿Por qué tarda en iniciar o aparece sd-verify?

Una caché nueva de shaders o del controlador puede necesitar compilación. Esta revisión corrige un fallo al sustituir la caché que antes obligaba a recompilar en cada inicio. Conserva la caché de tu consola y no distribuyas la de otra. Ante sd-verify, compara tamaños y SHA-256 con el manifiesto del entorno y el de la partida de inicio instalada. Usa un paquete coherente y vuelve a instalar su partida cuando corresponda. El texto antiguo legacy, por sí solo, no identifica la versión ejecutada.

SD-flex: instala una vez la partida de inicio SD-flex. Después, las actualizaciones compatibles del lanzador, menú y ROMFS solo requieren sustituir sus archivos SD, aunque cambien de tamaño. Los cambios de bootstrap, ro, core o de la ABI de arranque siguen requiriendo una partida correspondiente. Copia siempre el conjunto completo y verificado de actualización.

En 0.3.5, el resto de las imágenes de las tarjetas se carga después del primer fotograma y la nueva caché gráfica se vincula al contenido de los shaders. Un cambio real de shaders o controlador sigue requiriendo una compilación inicial. Al actualizar desde la versión 0.3.4 publicada, el componente RO cambiado requiere el conjunto completo de archivos compatibles y su archivo de guardado de inicio.
