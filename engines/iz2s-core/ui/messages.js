'use strict';

// The panel selects the language. Downloadable logs keep their original text.
globalThis.IZ2S_CORE_MESSAGES = (() => {
  const messages = `
openAddAppToInstall|Ready to install. Open Add App on Switch to receive it.|Пакет готов. Откройте Add App на Switch, чтобы установить его.|El paquete está listo. Abre Add App en Switch para instalarlo.|安装包已准备好。在 Switch 上打开 Add App 进行安装。
rpcReady|Filesystem RPC connected|Файловый RPC подключён|RPC del sistema de archivos conectado|文件系统 RPC 已连接
menuVersion|Menu version: {version}|Версия меню: {version}|Versión del menú: {version}|菜单版本：{version}
menuUpdate|Update launcher menu|Обновить меню лаунчера|Actualizar el menú|更新启动器菜单
menuUpdate_idle||||
menuUpdate_waiting|Return to the IZ2S main menu to start the update.|Вернитесь в главное меню IZ2S, чтобы начать обновление.|Vuelve al menú principal de IZ2S para iniciar la actualización.|返回 IZ2S 主菜单以开始更新。
menuUpdate_transferring|Downloading menu and resources…|Передаём меню и ресурсы…|Enviando el menú y los recursos…|正在传输菜单和资源…
menuUpdate_verifying|Checking files on Switch 2…|Проверяем файлы на Switch 2…|Comprobando archivos en Switch 2…|正在检查 Switch 2 上的文件…
menuUpdate_staged|Files are ready. Close Add App or return from the running app.|Файлы готовы. Закройте Add App или вернитесь из приложения в меню.|Archivos listos. Cierra Add App o vuelve al menú.|文件已就绪。关闭 Add App 或从应用返回菜单。
menuUpdate_applying|Switching to the new menu…|Переключаемся на новое меню…|Cambiando al nuevo menú…|正在切换到新菜单…
menuUpdate_applied|The new menu is active.|Новое меню уже работает.|El nuevo menú está activo.|新菜单已启用。
menuUpdate_rejected|The update failed. The previous version was kept ({error}).|Обновление не прошло. Прежняя версия сохранена ({error}).|La actualización falló. Se conservó la versión anterior ({error}).|更新失败。已保留上一个版本（{error}）。
menuUpdate_interrupted|IZ2S kept the previous menu. Stage: {stage}. Code: {error}.|IZ2S сохранил прежнее меню. Этап: {stage}. Код: {error}.|IZ2S conservó el menú anterior. Etapa: {stage}. Código: {error}.|IZ2S 保留了旧菜单。阶段：{stage}。代码：{error}。
menuRetry|Retry menu update|Повторить обновление меню|Reintentar la actualización|重试菜单更新
menuUpdate_expired|The menu update timed out. Try again.|Время ожидания вышло. Попробуйте ещё раз.|Se agotó el tiempo de espera. Inténtalo otra vez.|菜单更新超时。请重试。
menuUpdate_cancelled|The pending update was cancelled.|Запланированное обновление отменено.|Se canceló la actualización pendiente.|待处理的更新已取消。
connection|Connection mode|Режим подключения|Modo de conexión|连接模式
setup|First Setup|Первичная настройка|Configuración inicial|初始设置
launch|Launch|Запуск|Inicio|启动
setupProgress|First setup progress|Этапы первичной настройки|Progreso de la configuración inicial|初始设置进度
setupStepPrepare|Preparation|Подготовка|Preparación|准备
restartSetup|Restart|Начать заново|Reiniciar|重新开始
setupStepInput|Source entry|Ввод исходника|Introducción del código|输入源代码
setupStepReview|Review|Проверка|Revisión|校验
setupStepSave|Save|Сохранение|Guardado|保存
slot|Program slot {n}|Слот программы {n}|Ranura de programa {n}|程序槽 {n}
slotLabel|Empty program slot|Пустой слот программы|Ranura de programa vacía|空程序槽
setupReady|Direct is open, the selected slot is empty and the keyboard is set to English (US).|Открыт Direct, выбранный слот пуст, клавиатура настроена на English (US).|Direct está abierto, la ranura seleccionada está vacía y el teclado está en inglés (EE. UU.).|已打开 Direct，所选程序槽为空，键盘已设为英语（美国）。
verify|Check the source|Проверить исходник|Verificar el código|校验源代码
save|Save NS2IZA|Сохранить NS2IZA|Guardar NS2IZA|保存 NS2IZA
optional|Checking is optional. Confirm the save or overwrite on Switch.|Проверка необязательна. Подтвердите сохранение или замену файла на Switch.|La verificación es opcional. Confirma el guardado o la sustitución en Switch.|校验为可选操作。请在 Switch 上确认保存或覆盖。
checkIntro|Enter the character count and checksum shown on Switch for each part.|Введите число символов и контрольную сумму каждой части с экрана Switch.|Introduce el número de caracteres y la suma de verificación de cada parte que muestra Switch.|输入 Switch 上显示的每部分字符数和校验和。
part|Part|Часть|Parte|部分
chars|Characters|Символы|Caracteres|字符数
checksum|Checksum|Контрольная сумма|Suma de verificación|校验和
metric|Part {n}, {field}|Часть {n}, {field}|Parte {n}, {field}|第 {n} 部分：{field}
compare|Compare values|Сверить значения|Comparar valores|比对数值
match|All three parts match.|Все три части совпали.|Coinciden las tres partes.|三个部分均匹配。
mismatch|Some values differ. Check the readings on Switch and compare again.|Некоторые значения не совпали. Проверьте числа на Switch и сверьте их снова.|Algunos valores no coinciden. Revisa los datos en Switch y compáralos de nuevo.|部分数值不匹配。请检查 Switch 上的读数并重新比对。
savePrompt|Confirm Save or Overwrite on Switch and wait for it to finish.|Подтвердите Save или Overwrite на Switch и дождитесь завершения.|Confirma Save u Overwrite en Switch y espera a que termine.|请在 Switch 上确认 Save 或 Overwrite，并等待操作完成。
savedReady|NS2IZA was saved on Switch.|NS2IZA сохранён на Switch.|NS2IZA se ha guardado en Switch.|NS2IZA 已保存在 Switch 上。
finishSetup|Finish setup|Завершить настройку|Finalizar configuración|完成设置
launchIntro|Launch uses the saved NS2IZA. The selected network address comes from the PC tile.|Для запуска используется сохранённый NS2IZA. Адрес берётся из сетевой плитки ПК.|El inicio usa el NS2IZA guardado. La dirección se toma de la tarjeta del PC.|启动使用已保存的 NS2IZA。所用网络地址来自电脑卡片。
fresh|Switch 2 was fully restarted; fresh Direct is open.|Switch 2 полностью перезапущена; открыт новый Direct.|Switch 2 se ha reiniciado por completo; Direct está recién abierto.|Switch 2 已完全重启；已打开新的 Direct。
estimate|Estimated success per attempt|Ориентировочный шанс успеха за попытку|Probabilidad estimada de éxito por intento|每次尝试的估计成功率
fsAccess|Filesystem access|Доступ к файловой системе|Acceso al sistema de archivos|访问文件系统
manageSwitchApps|Manage apps on Switch|Приложения на Switch|Gestionar aplicaciones en Switch|管理 Switch 上的应用
addApp|Add App|Добавить приложение|Añadir aplicación|添加应用
restore|Restore connection|Восстановить связь|Restablecer conexión|恢复连接
checkPcLink|Check PC Link|Проверить PC Link|Comprobar PC Link|检查 PC Link
connectFromConsole|Press R + L3 in the IZ2S menu to open PC Link.|Нажмите R + L3 в меню IZ2S, чтобы открыть PC Link.|Pulsa R + L3 en el menú de IZ2S para abrir PC Link.|在 IZ2S 菜单中按 R + L3 打开 PC Link。
restored|Connection restored|Связь восстановлена|Conexión restablecida|连接已恢复
restorePending|Synchronizing session…|Синхронизация сеанса…|Sincronizando sesión…|正在同步会话…
restoreUnavailable|Restore requires an existing launcher session with a recovery service.|Для восстановления нужен существующий сеанс IZ2S со службой восстановления.|Para restablecer la conexión se necesita una sesión de IZ2S con servicio de recuperación.|恢复连接需要已有的 IZ2S 会话及恢复服务。
imported|Application installed on IZ2S SD.|Приложение установлено на SD IZ2S.|Aplicación instalada en la SD de IZ2S.|应用已安装到 IZ2S SD。
importFormat|Select an .iza file.|Выберите файл .iza.|Selecciona un archivo .iza.|请选择 .iza 文件。
sameVersion|This version is already installed.|Эта версия уже установлена.|Esta versión ya está instalada.|此版本已安装。
cardputer|Cardputer ADV or Switch 1 is on the network and connected to Switch 2 by USB.|Cardputer ADV или Switch 1 подключён к сети и соединён со Switch 2 по USB.|Cardputer está en la red Wi-Fi y conectado por USB a Switch.|Cardputer 已连接 Wi-Fi，并通过 USB 连接到 Switch。
start|Launch IZ2S|Запустить IZ2S|Iniciar IZ2S|启动 IZ2S
host|PC address {host}|Адрес ПК {host}|Dirección del PC {host}|电脑地址 {host}
noHost|Check the PC network tile for a valid address.|Проверьте адрес в сетевой плитке ПК.|Comprueba la dirección en la tarjeta de red del PC.|请在电脑网络卡片中检查可用地址。
sessionReady|IZ2S is running|IZ2S запущен|IZ2S está en ejecución|IZ2S 已启动
waitingLauncher|Waiting for IZ2S|Ожидание IZ2S|Esperando a IZ2S|正在等待 IZ2S
storeHint|On Switch, open Add App to choose an app from the PC library.|На Switch откройте Add App и выберите приложение из библиотеки на ПК.|Abre Add App en Switch para elegir una aplicación de la biblioteca del PC.|在 Switch 上打开 Add App，从电脑应用库中选择应用。
resetSession|Force reset session|Принудительно сбросить сеанс|Forzar el reinicio de la sesión|强制重置会话
resetPrompt|Reset this session in the panel? Transfers will stop and the new launch form will open. Logs and pairing will be kept. This does not reboot the console.|Сбросить текущий сеанс в панели? Передачи остановятся, откроется форма нового запуска. Журналы и сопряжение сохранятся. Консоль не будет перезагружена.|¿Restablecer la sesión del panel? Se detendrán las transferencias y se abrirá un nuevo inicio. Se conservarán registros y vinculación. La consola no se reiniciará.|重置面板会话？传输将停止，并显示新的启动表单。日志和配对将保留。主机不会重启。
continueOnConsole|Connection is complete. Now open IZ2S on Switch 2.|Подключение завершено. Теперь откройте IZ2S на Switch 2.|La conexión se ha completado. Abre IZ2S en Switch 2.|连接已完成。现在请在 Switch 2 上打开 IZ2S。
openWithR|Hold R and open a Switch 2 game|Удерживая R, откройте игру Switch 2|Mantén R y abre un juego de Switch 2|按住 R 打开 Switch 2 游戏
showSettings|Connection settings|Настройки подключения|Ajustes de conexión|连接设置
hideSettings|Hide connection settings|Скрыть настройки|Ocultar ajustes|隐藏连接设置
recordedSession|The last successful launch is saved. If you have not restarted Switch 2, continue with R; after a console restart, choose New launch.|Последний успешный запуск сохранён. Если Switch 2 не перезагружалась, продолжайте через R; после перезагрузки выберите «Новая попытка».|Se ha guardado el último inicio correcto. Si no reiniciaste Switch 2, continúa con R; después de reiniciar, elige Nuevo intento.|已保存上次成功启动。如果未重启 Switch 2，请按 R 继续；重启后请选择新建尝试。
confirmed|IZ2S launch confirmed|Запуск IZ2S подтверждён|Inicio de IZ2S confirmado|已确认 IZ2S 启动
lastConfirmed|Last IZ2S launch confirmed|Последний запуск IZ2S подтверждён|Se confirmó el último inicio de IZ2S|上次 IZ2S 启动已确认
pid|IZ2S PID {pid}. Continue on Switch.|PID IZ2S {pid}. Продолжайте на Switch.|PID de IZ2S {pid}. Continúa en Switch.|IZ2S PID {pid}。请在 Switch 上继续。
holdR|Keep R held until IZ2S appears. If the game starts normally, close it and repeat once.|Не отпускайте R, пока не появится IZ2S. Если открылась обычная игра, закройте её и повторите один раз.|Mantén pulsado R al abrir un juego de Switch 2. Repite una vez si no aparece el menú.|按住 R 打开一款 Switch 2 游戏。如果菜单未出现，请再试一次。
menuReady|IZ2S is open on Switch.|IZ2S открыт на Switch.|IZ2S está abierto en Switch.|IZ2S 已在 Switch 上打开。
menuConfirmed|You confirmed that IZ2S opened.|Вы подтвердили открытие IZ2S.|Has confirmado que IZ2S se abrió.|你已确认 IZ2S 打开。
apps|Apps|Приложения|Aplicaciones|应用
appsHint|Choose a tile on Switch. Use ZR to change SD pages.|Выберите плитку на Switch. Кнопка ZR переключает страницы SD.|Elige una tarjeta en Switch. Pulsa ZR para cambiar de página en la SD.|在 Switch 上选择卡片。按 ZR 切换 SD 页面。
ps2Hint|Open IZPS2, then select a BIOS and a game on the SD card.|Откройте IZPS2 и выберите BIOS и игру на SD-карте.|Abre IZPS2 y elige una BIOS y un juego de la tarjeta SD.|打开 IZPS2，然后选择 SD 卡上的 BIOS 和游戏。
return|Return|Возврат|Volver|返回
returnHint|Press the right stick twice to exit IZA. Press + to leave the menu.|Из IZA выйдите двумя короткими нажатиями правого стика, из меню — кнопкой «+».|Pulsa dos veces el stick derecho para salir de IZA. Pulsa + para salir del menú.|按两次右摇杆退出 IZA。按 + 退出菜单。
newLaunch|New launch|Новая попытка|Nuevo intento|新建尝试
offlineMenu|The console menu runs independently of the PC panel.|Меню на консоли работает без панели на ПК.|El menú de la consola funciona sin el panel del PC.|主机菜单可独立于电脑面板运行。
stop|Stop attempt|Остановить попытку|Detener intento|停止尝试
stopping|Stopping…|Остановка…|Deteniendo…|正在停止…
refresh|Refresh|Обновить|Actualizar|刷新
download|Download full log|Скачать полный журнал|Descargar registro completo|下载完整日志
journal|IZ2S activity|Журнал IZ2S|Actividad de IZ2S|IZ2S 活动日志
summary|Summary|Сводка|Resumen|摘要
details|Process output|Вывод процесса|Salida del proceso|进程输出
technical|Technical details|Технические подробности|Detalles técnicos|技术详情
attempt|Attempt {id}|Попытка {id}|Intento {id}|尝试 {id}
startedAt|Started {date}|Начало {date}|Inicio {date}|开始时间 {date}
finishedAt|Finished {date}|Завершение {date}|Fin {date}|结束时间 {date}
lastLine|Last entry {date}|Последняя запись {date}|Última entrada {date}|最后一条记录 {date}
noTimestamp|No timestamp was recorded for this line.|Время этой строки не записано.|No se registró la hora de esta línea.|此行未记录时间戳。
quiet|No new entries for {seconds} s|Нет новых записей {seconds} с|Sin entradas nuevas desde hace {seconds} s|已有 {seconds} 秒没有新记录
tail|Showing the end of the log. Download the full file for earlier entries.|Показан конец журнала. Ранние записи в полном файле.|Se muestra el final del registro. Descarga el archivo completo para ver las entradas anteriores.|当前显示日志末尾。下载完整文件可查看更早的记录。
noEvents|Waiting for the first log entry.|Ожидаем первую запись журнала.|Esperando la primera entrada del registro.|正在等待第一条日志。
inputNotStarted|Input did not start.|Ввод не начался.|No se inició la entrada de texto.|输入未开始。
usbKeyboardPending|Waiting for the input device to report that its USB keyboard is ready.|Ожидаем, пока устройство ввода сообщит о готовности USB-клавиатуры.|Esperando a que el dispositivo de entrada indique que su teclado USB está listo.|正在等待输入设备报告 USB 键盘已就绪。
usbKeyboardNotReady|The input device reports that its USB keyboard is not ready.|Устройство ввода сообщает, что USB-клавиатура не готова.|El dispositivo de entrada indica que su teclado USB no está listo.|输入设备报告 USB 键盘尚未就绪。
usbKeyboardBusy|The USB keyboard is busy or not ready. Check the cable and wait until the input device is ready.|USB-клавиатура занята или не готова. Проверьте кабель и дождитесь готовности устройства ввода.|El teclado USB está ocupado o no está listo. Comprueba el cable y espera a que el dispositivo esté listo.|USB 键盘正忙或尚未就绪。请检查连接线，等待输入设备就绪。
chooseInputAddress|Choose the address shown on your input device before continuing.|Выберите адрес с экрана устройства ввода, прежде чем продолжить.|Antes de continuar, elige la dirección que muestra tu dispositivo de entrada.|请先选择输入设备屏幕上显示的地址，再继续。
localSocketDenied|The PC could not open its local IPC socket (EACCES).|На ПК не удалось открыть локальный IPC-сокет (EACCES).|El PC no pudo abrir su socket IPC local (EACCES).|电脑无法打开本地 IPC 套接字（EACCES）。
inputUpdates|Cardputer reported typing progress ({n} updates).|Cardputer сообщает о ходе ввода ({n} обновлений).|Cardputer informó del progreso de escritura ({n} actualizaciones).|Cardputer 报告输入进度（{n} 条更新）。
phase1|Check console|Проверка консоли|Comprobar consola|检查主机
phase2|Prepare filesystem access|Подготовка доступа к SD|Preparar acceso al sistema de archivos|准备文件系统访问
phase3|Start native helper|Запуск системного помощника|Iniciar asistente nativo|启动本机辅助程序
phase4|Connect to RO|Подключение к RO|Conectar con RO|连接 RO
phaseStatus|Stage {n}/4 · {name}|Этап {n}/4 · {name}|Etapa {n}/4 · {name}|阶段 {n}/4 · {name}
starting|Starting the installer|Запуск установщика|Iniciando el instalador|正在启动安装程序
typing|{device} is typing|{device} вводит команды|{device} está escribiendo|{device} 正在输入
waiting|Waiting for Switch|Ожидание Switch|Esperando a Switch|正在等待 Switch
connected|Switch connected|Switch подключён|Switch conectada|Switch 已连接
lastFailed|Last attempt failed|Последняя попытка завершилась ошибкой|El último intento falló|上次尝试失败
failed|Attempt failed|Попытка завершилась ошибкой|El intento falló|尝试失败
stopped|Attempt stopped|Попытка остановлена|Intento detenido|尝试已停止
interrupted|Attempt interrupted|Попытка прервана|Intento interrumpido|尝试已中断
review|Review the source on Switch|Проверьте исходник на Switch|Revisa el código en Switch|请在 Switch 上检查源代码
checking|Checking the three parts|Проверка трёх частей|Verificando las tres partes|正在校验三个部分
values|Enter the check results|Введите результаты проверки|Introduce el resultado de la verificación|输入校验结果
checked|Source check passed|Проверка исходника прошла|Código verificado|源代码校验通过
saving|Saving NS2IZA|Сохранение NS2IZA|Guardando NS2IZA|正在保存 NS2IZA
save_confirmation|Confirm the save on Switch|Подтвердите сохранение на Switch|Confirma el guardado en Switch|请在 Switch 上确认保存
saved|Setup complete|Настройка завершена|Configuración completada|设置完成
reboot|Fully restart Switch before trying again.|Перед повтором полностью перезагрузите Switch.|Reinicia Switch por completo antes de volver a intentarlo.|再次尝试前，请完全重启 Switch。
noInput|No input was sent to Switch.|Команды на Switch не отправлялись.|No se enviaron comandos a Switch.|未向 Switch 发送任何命令。
pcError|Could not refresh IZ2S status and log. Check the panel connection and retry.|Не удалось обновить статус и журнал IZ2S. Проверьте связь с панелью и повторите обновление.|No se pudo actualizar el estado y el registro de IZ2S. Comprueba la conexión con el panel y vuelve a intentarlo.|无法刷新 IZ2S 状态和日志。请检查与面板的连接并重试。
requestError|The IZ2S operation failed.|Не удалось выполнить действие IZ2S.|No se pudo completar la operación de IZ2S.|IZ2S 操作失败。
downloadError|Could not download the log.|Не удалось скачать журнал.|No se pudo descargar el registro.|无法下载日志。
fsError|Filesystem access failed. IZ2S launch was not confirmed.|Не получен доступ к SD. Запуск IZ2S не подтверждён.|Falló el acceso al sistema de archivos. No se confirmó el inicio de IZ2S.|未能获得文件系统访问权限。IZ2S 启动未确认。
pathError|The installer did not create an attempt folder.|Установщик не создал папку попытки.|El instalador no creó la carpeta del intento.|安装程序未创建尝试目录。
receiptError|The installer exited without confirming IZ2S.|Установщик завершился без подтверждения запуска IZ2S.|El instalador salió sin confirmar IZ2S.|安装程序退出，未确认 IZ2S 启动。
inputError|Cardputer stopped before typing finished.|Cardputer прервал ввод.|Cardputer se detuvo antes de terminar de escribir.|Cardputer 在输入完成前停止。
deviceError|Check the input device USB connection and wait for it to become available.|Проверьте USB-подключение устройства ввода и дождитесь готовности.|Comprueba el USB de Cardputer y espera a que esté disponible.|请检查 Cardputer 的 USB 连接并等待设备就绪。
pairingError|The input device needs a valid pairing key. Refresh status to restore this PC's saved pairing, or install the device package.|Устройству ввода нужен действующий ключ сопряжения. Обновите статус для восстановления сохранённого сопряжения этого ПК или установите пакет устройства.|El dispositivo necesita una clave válida. Actualiza el estado para recuperar la vinculación del PC o instala el paquete.|输入设备需要有效配对密钥。请刷新状态以恢复此电脑保存的配对，或安装设备包。
chooseInput|Select Cardputer ADV or Switch 1 in Connection.|Выберите Cardputer ADV или Switch 1 в разделе «Подключение».|Selecciona Cardputer ADV o Switch 1 en Conexión.|请在“连接”中选择 Cardputer ADV 或 Switch 1。
inputNetworkError|The input device is not accepting network connections. Check its address and leave its keyboard app open.|Устройство ввода не принимает сетевые подключения. Проверьте адрес и оставьте приложение клавиатуры открытым.|El dispositivo no acepta conexiones de red. Comprueba su dirección y deja abierta la aplicación de teclado.|输入设备未接受网络连接。请检查地址并保持键盘应用打开。
busyError|A IZ2S attempt is already active.|Попытка IZ2S уже выполняется.|Ya hay un intento de IZ2S activo.|已有 IZ2S 尝试正在进行。
panelBusy|Finish the current panel session first.|Сначала завершите текущий сеанс панели.|Finaliza primero la sesión actual del panel.|请先结束当前面板会话。
portBusy|The selected port is already in use.|Порт уже используется.|El puerto seleccionado ya está en uso.|所选端口已被占用。
chooseCardputer|Select Cardputer ADV in Connection.|Выберите Cardputer ADV в разделе «Подключение».|Selecciona Cardputer ADV en Conexión.|请在“连接”中选择 Cardputer ADV。
confirmScreen|Confirm the current Switch screen before continuing.|Подтвердите текущее состояние экрана Switch.|Confirma la pantalla actual de Switch antes de continuar.|继续前，请确认 Switch 当前的屏幕状态。
setupError|Check the selected slot and restart First Setup if needed.|Проверьте слот и при необходимости начните первичную настройку заново.|Revisa la ranura y repite la configuración inicial si hace falta.|请检查所选程序槽，必要时重新开始初始设置。
previewError|Actions are disabled in preview mode.|Действия отключены в режиме предпросмотра.|Las acciones están desactivadas en la vista previa.|预览模式下无法执行操作。
deviceServer|{host} · serving this IZ2S attempt|{host} · порт занят текущей попыткой IZ2S|{host} · atendiendo este intento de IZ2S|{host} · 正在处理本次 IZ2S 尝试
deviceTyped|Input complete|Ввод завершён|Entrada completada|输入完成
engineCaption|First setup · Launch · R entry|Первичная настройка · Запуск · Вход через R|Configuración inicial · Inicio · Acceso con R|初始设置 · 启动 · 按 R 进入
unavailable|IZ2S status is unavailable|Статус IZ2S недоступен|Estado de IZ2S no disponible|IZ2S 状态不可用
inProgress|Attempt in progress; IZ2S is not yet confirmed.|Попытка выполняется; запуск IZ2S ещё не подтверждён.|Intento en curso; IZ2S aún no está confirmado.|尝试正在进行；IZ2S 尚未确认。
loading|Loading IZ2S status and log…|Загрузка статуса и журнала IZ2S…|Cargando el estado y el registro de IZ2S…|正在加载 IZ2S 状态和日志…
staleSnapshot|Showing the last status and log received at {date}.|Показаны последние полученные статус и журнал: {date}.|Se muestran el último estado y registro recibidos: {date}.|显示最后收到的状态和日志：{date}。
noSnapshot|No IZ2S status has been received yet.|Статус IZ2S ещё не получен.|Aún no se ha recibido el estado de IZ2S.|尚未收到 IZ2S 状态。
attemptError|The attempt ended with an error. IZ2S launch was not confirmed.|Попытка завершилась ошибкой. Запуск IZ2S не подтверждён.|El intento terminó con un error. No se confirmó el inicio de IZ2S.|尝试因错误结束。IZ2S 启动未确认。
unconfirmed|IZ2S launch has no confirmation.|Подтверждение запуска IZ2S отсутствует.|No hay confirmación del inicio de IZ2S.|没有 IZ2S 启动确认。
ready|Waiting for IZ2S confirmation|Ожидание подтверждения IZ2S|Esperando confirmación de IZ2S|正在等待 IZ2S 确认
idle|IZ2S is idle|Сеанс IZ2S не запущен|IZ2S está inactivo|IZ2S 空闲
`.trim().split('\n').map(line => line.split('|'));
  const values = Object.fromEntries(messages.map(([key, ...translations]) => [key, translations]));
  return {
    t(key, fields = {}) {
      const index = ['en', 'ru', 'es', 'zh'].indexOf(globalThis.RUN21_LANGUAGE?.id);
      return (values[key]?.[Math.max(0, index)] ?? key).replace(/\{(\w+)\}/g, (_, name) => String(fields[name] ?? ''));
    },
  };
})();
