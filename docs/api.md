# Справочник API

[Документация](index.md)

## Выбор интерфейса

| Задача | Справочник |
|---|---|
| Управление и параметры регуляторов | [HTTP](#http-api-управления) |
| GPIO, применение и восстановление | [Оборудование](#http-api-оборудования) |
| Диапазоны и калибровка | [API калибровки](az-range.md#http-api) |
| Роль устройства и каталог повороток | [API HUD](hud.md#http-api) |
| Загрузка прошивки | [OTA](ota.md#http-api) |
| Оперативные команды и телеметрия | [WebSocket](live.md) |
| Настройка без браузера | [UART/USB, UDP и TCP](command-api.md) |
| Опции и журнал автотюна | [Расширенная настройка](autotune-experiments.md) |

Используйте [порты](network.md#порты) своего устройства. HTTP и команды управления доступны в локальной сети без отдельной авторизации. `token`, `generation`, `boot`, `selection` и `rangeRevision` защищают от устаревших запросов; они не заменяют пароль доступа к сети.

HTTP-формы передаются как `application/x-www-form-urlencoded`. [OTA](ota.md#http-api) имеет отдельные заголовки и бинарную загрузку. Командный транспорт использует те же пути и поля, но все изменения выполняет через POST; HTTP сохраняет некоторые старые GET-операции. После потери ответа сначала прочитайте состояние: команда могла выполниться. Для HUD дождитесь результата `requestId`; подтверждение команды не означает достижения цели.

## HTTP API управления

Операции POST принимают `application/x-www-form-urlencoded`:

| Метод / путь | Поля |
|---|---|
| GET `/api/status` | model, controlMode, remainingMs, pendingValid/pending, presetActive/presetSlot, presets, speedEnabled/speedPercent, startSpeedPercent, approachSpeedPercent, accelerationMs, decelerationMs, slowdownDegrees, coastDegrees, decelerationFactor, speedOutputPercent (текущий выход, %), fault, сетевые настройки |
| POST `/api/mode` | mode=manual/auto/auto_1h |
| POST `/api/execute` | без полей |
| POST `/api/preset` | slot=1…4, label, az, el (G-5500) |
| POST `/api/preset/run` | slot=1…4 |
| POST `/api/settings` | model=G5500/G800DXA, rotorName, n1mmReport, n1mmPort, pstPort, reportPort, reportHost, n1mmManualFollow/pstManualFollow/gs232ManualFollow/rotctldManualFollow=true/false, externalAzMode=shortest/mechanical, speedEnabled=true/false, speedPercent/startSpeedPercent/approachSpeedPercent=1…100, accelerationMs/decelerationMs=100…10000, slowdownDegrees=1…90, coastDegrees=0…90, decelerationFactor=0.1…10 |
| POST `/api/target` | az и/или el; хотя бы одна ось, для G-800DXA — az; явная локальная команда выполняется сразу |

`GET /api/status` также возвращает `n1mmManualFollow`, `pstManualFollow`, `gs232ManualFollow`, `rotctldManualFollow`: `true` — автоследование в ручном режиме, `false` — подтверждение. Эти параметры доступны на контроллере и на HUD для принимаемых ими команд. `externalAzMode` в статусе и настройках задаёт обработку внешнего AZ: `shortest` — кратчайший доступный путь (по умолчанию), `mechanical` — механическая координата программы.

JSON: `ok`, `result`; 200 успешно/ожидание, 400 неверно/нет калибровки, 409 команда отложена приоритетом пресета. `GET /api/move?d=L/R/U/D/S/A/E` доступен для локального управления.

### Параметры двух осей и аппаратная конфигурация

`/api/status` содержит объект `build` (плата, дисплей, Bluetooth, силовой интерфейс, кнопка и основные GPIO), массив `axes` (положение, цель, калибровка, tracking/completed, направление, допуск, активность и выход скорости) и поля `azSpeedPercent`/`elSpeedPercent`, `azStartSpeedPercent`/`elStartSpeedPercent`, `azApproachSpeedPercent`/`elApproachSpeedPercent`, `azAccelerationMs`/`elAccelerationMs`, `azDecelerationMs`/`elDecelerationMs`, `azSlowdownDegrees`/`elSlowdownDegrees`, `azSpeedOutputPercent`/`elSpeedOutputPercent`.

`azCoastDegrees`/`elCoastDegrees` — настроенный угол выбега; `azDecelerationFactor`/`elDecelerationFactor` — коэффициент торможения. Они доступны в `GET /api/status` и `POST /api/settings`. `coastDegrees` и `decelerationFactor` без префикса относятся к AZ. Фактический допуск находится в `axes[].stopToleranceDegrees`. Настройки выбега меняются на самом контроллере поворотки.

`POST /api/settings` принимает настройки с префиксами `az`/`el` (кроме измеренных выходов). Поля без префикса также относятся к AZ. Если `speedPercent=50` и `azSpeedPercent=75` переданы вместе, весь запрос отклоняется без частичного изменения. `azSpeedEnabled` — алиас опционального `speedEnabled` Yaesu; у независимых приводов управление PWM является частью выбранного аппаратного интерфейса и не отключается настройкой. EL в модели G-800DXA выключена. Аппаратные поля доступны только для чтения; попытка изменить их через settings отклоняется.

`POST /api/speed` принимает `axis=az` / `el` и `value`: в простом режиме — процент выхода, в каскаде — °/с. Допустимый диапазон — от 5% до 100% верхнего предела оси (`speedLimit`). Уставка меняется без остановки текущего движения и сохраняется через 40 секунд после последнего изменения или перед штатной перезагрузкой. В `/api/status` и live-состоянии `axes[].speedSetpoint` и `speedLimit` содержат текущую уставку и её верхний предел, `speedAdjustable` — доступность регулировки. Команда доступна также через USB API контроллера. Для HUD и нерегулируемых выходов она отклоняется.

### API регуляторов движения

`GET /api/motion` возвращает массив `axes` с настройками AZ и EL: `regulatorMode` (`simple` / `cascade`), `ready`, `kickEnabled`, `holdEnabled` и числовые поля ниже. Числовые имена этого ответа начинаются с прописной буквы.

Для записи используйте `POST /api/settings`: `azRegulatorMode` / `elRegulatorMode`; остальные имена имеют вид `azMotion…` / `elMotion…`, например `azMotionVelocity=3`. Сохраняйте оси отдельными запросами, чтобы укладываться в лимит формы. Готовность определяется автоматически. Прежнее поле `MotionReady` принимается для совместимости и не требует подтверждения. Некорректный запрос отклоняется целиком.

Смену модели и изменение параметров регулятора выполняйте отдельными запросами. При смене модели загружаются её сохранённые параметры; без такой записи используются параметры по умолчанию.

| Суффиксы после `Motion` | Значение |
|---|---|
| `KickEnabled`, `HoldEnabled` | Логические `true` / `false` |
| `OutputLimitPercent`, `KickPercent`, `MovementDegrees` | Общий предел, пусковой выход, угол подтверждения движения |
| `SustainingPercent` | Минимальный выход движения каскада, целый процент 0…100; 0 отключает ограничение снизу. Не выше `OutputLimitPercent`. Не действует при торможении, достижении позиции, STOP и защите. |
| `Velocity`, `Acceleration`, `Deceleration`, `Jerk` | Ограничения профиля: °/с, °/с², °/с², °/с³ |
| `PositionKp`, `PositionKi`, `PositionKd` | ПИД положения: 1/с, 1/с², безразмерный D |
| `VelocityKp`, `VelocityKi`, `VelocityKd`, `Feedforward` | ПИД скорости и упреждение выхода |
| `PositionTolerance`, `HoldResumeDegrees`, `StoppedVelocity` | Допуск позиции, порог коррекции и порог остановки |
| `KickMs`, `KickReleaseMs` | Время нарастания пуска и время его снятия. В обычном движении ошибка страгивания возникает через `max(KickMs, StallMs)`; автотюн использует отдельные ограниченные пробы |
| `VelocityWindowMs`, `DerivativeFilterMs`, `FeedbackTimeoutMs` | Окно скорости, фильтр D, срок свежести |
| `SettleMs`, `StallMs` | Подтверждение позиции и контроль заклинивания |

`axes[]` в `/api/status` и live-состоянии содержит `regulatorMode`, `motionReady`, `motionPhase`, `motionFault`, `measuredVelocity`, `requestedVelocity`, `profileAcceleration`, `holdEnabled`, `holdArmed`. Фазы: `idle`, `kick`, `run`, `braking`, `holding`, `fault`. Ошибки: `kick_start_timeout`, `feedback_stale`, `feedback_invalid`, `stalled`, `planner_error`.

Для проверки задержек `GET /api/status` возвращает `localFeedback` в порядке AZ, EL: `ageMs` — возраст последнего отсчёта АЦП, `maxIntervalMs` — наибольший интервал между отсчётами с запуска. Для отсутствующей локальной обратной связи передаётся `null`. Эти значения позволяют отделить задержку получения показаний от задержки передачи статуса по сети. При реверсе каскад ожидает остановки по измеренной скорости и выдерживает паузу направлений 300 мс; неподвижный привод не ждёт дополнительного торможения расчётного профиля.

`POST /api/motion/reset` с `axis=az`, `el` или `all` снимает ошибку и останавливает выбранные оси; наведение не возобновляет. Настройки выполняются на контроллере поворотки. HUD передаёт сброс выбранной поворотке; для этого обе прошивки должны поддерживать сброс через HUD. Старый контроллер отклоняет неизвестную команду без запуска движения.

`GET /api/motion/autotune` возвращает фазу, `active`, `id`, ось (`0` AZ / `1` EL), режим, направление, пробный выход, причину остановки и ошибку проверки. Полный ответ содержит границы пробного участка и `measurements` для обоих направлений, в том числе при остановке с ошибкой. После успешного завершения добавляется `result` с предложенными параметрами. Краткое состояние `autotune` также включено в `/api/status` и live.

`POST /api/motion/autotune` с `action=center&axis=az|el` запускает однократное наведение выбранной оси к середине её откалиброванного диапазона, без удержания после достижения. Используются сохранённые настройки движения; `outputLimit` пробного автотюна к этому наведению не относится. Нужны ручной режим, свежие откалиброванные измерения, отсутствие движения, ошибок, удержания и ожидающих команд. Можно передать `rangeRevision`; устаревшая ревизия отклоняется. STOP останавливает наведение.

`POST /api/motion/autotune` принимает `action=start` с `axis=az|el`, `mode=simple|cascade`, `travel` (ход в каждую сторону), `outputLimit` и `id` из последнего статуса. Успешный запуск увеличивает `id`; повтор старого запроса не запускает новую процедуру. `action=cancel` останавливает активную процедуру; `action=apply&id=…` применяет только готовый результат указанного запуска. Изменённые параметры, калибровка или диапазон требуют нового автотюна. Для сохранения через HTTP используйте контроллер поворотки; HUD не пересылает эти запросы. Во время автотюна `/api/settings` возвращает `busy`.

Прежние поля процентов и ручного выбега относятся к простому варианту. `startSpeedPercent` / `elStartSpeedPercent` сохранены для совместимости хранения; новый пуск задаётся через `MotionKickPercent` и `MotionKickEnabled`. На нерегулируемых выходах пусковой импульс, S-профиль и каскад недоступны.

### Ограничения HTTP-клиента

Передавайте POST-формы с `application/x-www-form-urlencoded`. Полный запрос вместе с заголовками и телом — до 4096 байт, суммарно до 32 параметров query/body; после URL-декодирования ключ — до 32 байт, значение — до 128 байт. Произвольные файлы и chunked-запросы не поддерживаются; для прошивки используйте отдельный [блочный API OTA](ota.md#http-api).

При обрыве или тайм-ауте запроса управления сначала прочитайте состояние: команда могла выполниться до потери ответа. Для HUD дополнительно дождитесь результата по `requestId`, как описано в [API HUD](hud.md#http-api).


## HTTP API оборудования

`GET /api/hardware` возвращает сохранённые, действующие и подтверждённые параметры, состояние пробного запуска, причину восстановления, свободную внутреннюю память, `token` текущего запуска и `generation` конфигурации. Поля `failureCount`, `failureLimit`, `failureResetMs` и `lastFailure` показывают накопленные сбои, порог восстановления, интервал нормальной работы до сброса и последнюю причину сбоя. Объекты `configured`, `effective` и `confirmed` содержат карту `pins`; `effective` показывает реально применённую распиновку.

Запросы изменения используют POST с `Content-Type: application/x-www-form-urlencoded` и актуальными `token` / `generation`:

| Адрес | Действие |
|---|---|
| `/api/hardware` | Сохранить переданные поля оборудования и GPIO, затем перезапустить |
| `/api/hardware/probe` | Проверить обычный обработчик POST и запись NVS перед подтверждением |
| `/api/hardware/confirm` | Подтвердить проверенный пробный запуск и перезапустить |
| `/api/hardware/rollback` | Выбрать предыдущую подтверждённую конфигурацию для пробного запуска |
| `/api/hardware/recovery` | Перезапустить в режиме восстановления |

При сохранении обязательны `token`, `generation` и хотя бы одно изменяемое поле. Пропущенные поля сохраняют значения текущей выбранной конфигурации. Поля оборудования: `driveInterface` (0 Yaesu, 1 отдельный PWM, 2 BTS7960), `simulation`, `bluetoothEnabled`, `bluetoothMode` (1 HC-05, 2 SPP, 3 автоматически), `forceApSta`, `buttonGpio`, `buttonActiveLow`, `buttonPullup`, `pwmInverted`. Логические поля принимают 0 или 1.

Для GPIO используйте поля `pin.<имя>` со значением `−1` или номером GPIO:

| Группа | Имена |
|---|---|
| Привод и ADC | `azAdc`, `elAdc`, `azNegative`, `azPositive`, `elNegative`, `elPositive`, `azSpeedEnable`, `elSpeedEnable` |
| UART0 и HC-05 | `uartRx`, `uartTx`, `hc05Rx`, `hc05Tx`, `hc05State` |
| Индикация | `rgb`, `led` |
| TFT и тачскрин | `tftMosi`, `tftSck`, `tftMiso`, `tftCs`, `tftDc`, `tftRst`, `touchCs`, `touchIrq` |
| LCD1602 | `lcdRs`, `lcdE`, `lcdD4`, `lcdD5`, `lcdD6`, `lcdD7` |

GPIO кнопки передаётся прежним полем `buttonGpio`. `GET /api/hardware/pins` возвращает `roles` с именем, группой, типом, необязательностью и начальным GPIO сигнала, а также `gpios` с номером, возможностями `caps` и владельцем `owner`. Биты `caps`: 1 — вход, 2 — выход, 4 — ADC1, 8 — внутренняя подтяжка. `owner` обозначает встроенную периферию или аппаратный резерв. Занятость настраиваемыми сигналами определяется текущими значениями `pins` и режимом оборудования.

Пример частичного изменения на S3 после чтения актуальных `token` и `generation`:

```sh
curl --data 'token=123&generation=2&pin.azAdc=1' http://192.168.4.1/api/hardware
```

Весь запрос проверяется и сохраняется целиком. Ошибки GPIO возвращают HTTP 400 с `error`, `field`, `gpio` и `owner`; конфликтующее поле можно исправить без изменения сохранённых настроек. Повторяющиеся и неизвестные поля отклоняются. Ошибки сохранения возвращают 503; устаревшие параметры запуска или ещё не готовое подтверждение — 409.

Остальные POST передают только `token` и `generation`. Успешное подтверждение требует предварительного успешного `/api/hardware/probe` и достаточного запаса памяти. В восстановлении `/api/hardware/probe` недоступен: сначала сохраните оборудование для пробного запуска.

## Расширения ручного управления HUD

Существующие кадры HUD/1 с типами 1–10 сохраняют формат. Новые типы:

| Тип | Содержимое после стандартного заголовка |
| --- | --- |
| 11 ExtendedQuery | Запрос поддержки расширенного состояния |
| 12 ExtendedState | Полное состояние и байт маски известных целей AZ/EL |
| 13 ExtendedSubscribe | Подписка на расширенную телеметрию |
| 14 ExtendedTelemetry | Ревизия, компактное состояние и маска известных целей |
| 15 Manual | gesture:u32, mask:u8, release:u8, AZ:f32, EL:f32 |
| 16 ManualAck | gesture:u32, result:u8 |

Биты mask: 1 — AZ, 2 — EL. Значения скоростей конечные, от −1 до +1; масштаб
применяется к рабочей скорости без записи настроек в NVS. Заголовок передаёт
идентичность и загрузочную сессию обоих устройств, последовательный номер и
срок действия команды. CRC и порядок байтов соответствуют HUD/1.


Полный двоичный формат и версии: [hud.hpp](../G5500_Fork/hud.hpp). Пользовательские действия описаны в [руководстве CrowPanel](crowpanel-21.md#ручное-движение).

### Кнопки и калибровка джойстика

`GET /api/hardware` возвращает в настройках оборудования `localInputMode` (0 — выключено, 1 — кнопки, 2 — джойстик), `localAzInvert` / `localElInvert` (0/1), `localAzMin/Center/Max`, `localElMin/Center/Max`, `localDeadzone` и `localHysteresis` (12-битные отсчёты ADC). GPIO находятся в `pins.localAzNegative`, `localAzPositive`, `localElNegative`, `localElPositive`, `localAzAdc`, `localElAdc`. Запись выполняется частичными полями через `POST /api/hardware` с актуальными `token` и `generation`; изменения требуют перезапуска и подтверждения оборудования.

После сохранения и подтверждения GPIO джойстика доступны POST-запросы с теми же `token` и `generation`:

| Путь | Действие |
|---|---|
| `/api/hardware/joystick/start` | Остановить удержание и включить измерения с блокировкой движения |
| `/api/hardware/joystick/poll` | Продлить измерения и получить `raw: [AZ, EL]`; −1 обозначает неподключённый вход |
| `/api/hardware/joystick/cancel` | Завершить измерения без записи калибровки |

Опрос нужен чаще одного раза в 2 секунды. После его прекращения блокировка измерений снимается, новое удержание требует нейтрали. Клиент отдельно фиксирует центр и крайние отсчёты и сохраняет их через настройки оборудования. Калибровка джойстика на HUD относится к входам пульта; калибровка положения поворотки остаётся на её контроллере.
