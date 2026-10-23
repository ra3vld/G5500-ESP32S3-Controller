# Документация

[О проекте](../README.md)

## Первый запуск

1. [Выберите конфигурацию](setup/configuration.md#выбор-конфигурации) и [схему подключения](hardware/index.md).
2. [Соберите и запишите прошивку через USB](build.md).
3. Выполните [первое подключение и настройку](quickstart.md#первое-подключение).
4. Задайте [диапазоны и калибровку](setup/az-range.md), затем [регуляторы](setup/regulator-tuning.md).
5. Перейдите к [управлению](control/index.md) и [подключению внешних программ](integrations/index.md).

## Настройка

- [Выбор конфигурации, GPIO и настройки оборудования](setup/configuration.md).
- [Wi-Fi и сетевые порты](setup/network.md).
- [Диапазоны и калибровка AZ / EL](setup/az-range.md).
- [Регуляторы движения и автотюн](setup/regulator-tuning.md).
- [Подбор методики автотюна через API](setup/autotune-experiments.md).

## Платы и дисплеи

- [Классический ESP32 WROOM/WROVER](boards/classic-esp32.md).
- [WT32-ETH01 с Ethernet](boards/wt32-eth01.md).
- [CrowPanel: экран и жесты](boards/crowpanel-21.md), [переделка CrowPanel для локального AZ](boards/crowpanel-az.md).

## Подключение приводов и схемы

- [Выбор силового интерфейса](hardware/index.md).
- [G-5500](hardware/g5500/index.md), [G-800DXA](hardware/g800dxa/index.md), [G-800SA и переделка скорости](hardware/g800sa/index.md).
- [BTS7960 / IBT-2](hardware/bts7960/index.md), [отдельные направления и PWM](hardware/separate-pwm.md).
- [Входы датчиков ADC](hardware/adc-input.md), [изоляция обратной связи Yaesu](hardware/yaesu-feedback-isolation.md).

## Управление

- [Веб, режимы, предустановки и скорость](control/index.md).
- [Кнопки и джойстик](control/local-controls.md).
- [HUD: удалённый пульт](control/hud.md).
- [Генератор и отправка команд с компьютера](control/control-generator.md).

## Внешние программы и интеграции

- [Логгеры и программы слежения](integrations/index.md).
- [RS‑485: прямое GS‑232 и подключение компьютера через HUD](integrations/rs485.md).
- [Modbus RTU: общая линия с маркером и карта регистров](integrations/modbus-rtu.md).

## Внешние API

- [Общие правила и HTTP API](api/index.md).
- [WebSocket](api/live.md), [командный API UART/USB, UDP и TCP](api/command-api.md).

## Обслуживание

- [Обновление прошивки](maintenance/ota.md).
- [Диагностика и восстановление](maintenance/diagnostics.md).

## Лицензии и авторство

Проект — [GNU GPL v3](../LICENSE), исходный автор UA1CFM, изменения форка RA3VLD. Сторонние компоненты: [RGB-драйвер CrowPanel](licenses/crowpanel-rgb-driver.md), [DejaVu](licenses/DejaVu.txt), [Ruckig MIT](licenses/ruckig-MIT.txt), [фавиконка Lucide ISC](licenses/lucide-favicon.md).
