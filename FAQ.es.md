# Preguntas frecuentes

[English](FAQ.md) · [Русский](FAQ.ru.md) · [简体中文](FAQ.zh.md) · [Español](FAQ.es.md)

[Guía completa de instalación (en inglés)](docs/INSTALLATION.md)

## 1. ¿Qué necesito para la primera instalación?

Una Nintendo Switch 2 con firmware **23.0.1**, un **cartucho de Civilization VII 1.0.0 para Nintendo Switch 1**, una microSD, Linux nativo con un adaptador Wi-Fi compatible y tu propio `prod.keys`, obtenido de tu propia Switch 1. Antes de abrir el juego, selecciona English para la interfaz y English (US) para el teclado USB. Esta edición solo incluye un instalador para Linux.

## 2. ¿Qué idiomas debo seleccionar en la consola?

Interfaz: `HOME → Configuración de la consola → Consola → Idioma → English`. Teclado: `HOME → Configuración de la consola → Mandos y accesorios → Teclado USB → English (US)`. Hazlo antes de iniciar el juego. Windows y WSL no son entornos de instalación admitidos en esta edición.

## 3. ¿Dónde debo extraer el archivo SD?

En la raíz de la tarjeta, de modo que las rutas comiencen directamente por `SD:/iz2s/`. Al actualizar, combina las carpetas y sustituye los archivos coincidentes del paquete. Conserva `Nintendo`, las otras aplicaciones, BIOS, juegos, partidas y ajustes. Mantén las licencias incluidas y expulsa la tarjeta de forma segura.

## 4. ¿Qué hago si tengo una versión antigua de IZ2S u otro cargador?

Actualiza los archivos SD y sigue el procedimiento completo. El paso condicional de limpieza utiliza `HOME → System Settings → Data Management → Delete Save Data → Civilization VII → Your profile`. Esto elimina los datos de guardado del perfil elegido; lee el aviso antes de confirmar. Una partida de inicio de otra revisión del entorno no es compatible solo por compartir el número de versión del lanzador.

## 5. ¿Debo instalar la partida manualmente después de conectar?

No. **Conectar e instalar la partida** restaura el Wi-Fi del PC, verifica el cargador e instala automáticamente `IZ2SCIV.Civ7Save`. Mantén abierta la pantalla Awaiting payload hasta que termine. Si falla, el panel ofrece reintentar; una escritura sin confirmación no se considera correcta.

## 6. ¿La instalación automática elimina mis otras partidas?

El instalador actual solo sustituye su propia partida de inicio y confirma la escritura del sistema de archivos. Eliminar las partidas del perfil desde la configuración de la consola al migrar desde otro cargador es una acción distinta que sí borra el progreso de ese perfil.

## 7. No se encuentra la IP de la consola. ¿Qué hago?

Abre los ajustes de IP justo debajo de **Estado de la sesión**. Busca y verifica una dirección propuesta o introduce la IP mostrada en la consola. Mantén Awaiting payload abierto y comprueba que se ha restaurado el Wi-Fi del PC. Un servidor FTP disponible no demuestra que el cargador del juego esté preparado.

## 8. ¿Cómo inicio después de instalar?

Vuelve a HOME, cierra Civilization VII con X, abre el juego de nuevo y elige Load Game sin reiniciar la consola. Sigue las indicaciones hasta IZ2S CORE IS RUNNING, cierra Civilization VII y mantén R al abrir un juego de Switch 2. Repite una vez el inicio con R si hace falta. Los pasos completos de recuperación están en la [guía](docs/INSTALLATION.md#4-launch-iz2s).

## 9. ¿Por qué FTP está activado en la consola pero desconectado en el panel?

Son los estados independientes del servidor y del cliente. Activa FTP con L + R3 y pulsa **Conectar FTP** en el panel. PC Link usa R + L3 y tiene su propio estado. L3/R3 significa pulsar el stick izquierdo/derecho. Usa la dirección y el puerto que muestra la consola.

## 10. ¿Por qué tarda en iniciar o aparece sd-verify?

Una caché nueva de shaders o del controlador puede necesitar compilación. Esta revisión corrige un fallo al sustituir la caché que antes obligaba a recompilar en cada inicio. Conserva la caché de tu consola y no distribuyas la de otra. Ante sd-verify, compara tamaños y SHA-256 con el manifiesto del entorno y el de la partida de inicio instalada. Usa un paquete coherente y vuelve a instalar su partida cuando corresponda. El texto antiguo RUN25, por sí solo, no identifica la versión ejecutada.
