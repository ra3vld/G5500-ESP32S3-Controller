const $ = (id) => document.getElementById(id);
const labels = [
  ['WEB_INTERFACE_ENABLED', 'Веб-управление'], ['WEB_LIVE_SUPPORT', 'Live / WebSocket'],
  ['WIFI_SUPPORT', 'Wi-Fi'], ['ETHERNET_SUPPORT', 'Ethernet'],
  ['MQTT_SUPPORT', 'MQTT'], ['HOME_ASSISTANT_DISCOVERY_SUPPORT', 'Home Assistant'],
  ['GS232_SUPPORT', 'GS-232'], ['ROTCTLD_SUPPORT', 'rotctld'],
  ['N1MM_SUPPORT', 'N1MM'], ['PSTROTATOR_SUPPORT', 'PstRotator'],
  ['HUD_SUPPORT', 'Удалённый пульт HUD'], ['RS485_SUPPORT', 'RS-485'], ['MODBUS_RTU_SUPPORT', 'Modbus RTU'],
  ['BLUETOOTH_SUPPORT', 'Внешний HC-05'], ['BLUETOOTH_SPP_SUPPORT', 'Bluetooth Classic SPP'],
  ['CAMERA_ENABLED', 'Камера'], ['OTA_EFFECTIVE', 'Обновление OTA'],
];
const serviceLabels = {mqtt:'MQTT / Home Assistant', ethernet:'Ethernet', rs485:'RS-485 / Modbus RTU'};
let catalog, selected, libraryReady = false, libraryError = false, locked = false;
let installer, installButton;
const kib = (bytes) => (bytes / 1024).toLocaleString('ru-RU', {maximumFractionDigits: 1}) + ' КиБ';
const mib = (bytes) => (bytes / 1048576).toLocaleString('ru-RU', {maximumFractionDigits: 2}) + ' МиБ';
const node = (tag, text, className) => {
  const element = document.createElement(tag);
  if (text !== undefined) element.textContent = text;
  if (className) element.className = className;
  return element;
};

function browserIssue() {
  if (!window.isSecureContext) return 'Для прошивки откройте сайт по HTTPS или через localhost.';
  if (!('serial' in navigator)) return 'Нужен браузер с Web Serial. Откройте страницу в Chrome или Edge на компьютере. USB-комплект можно скачать ниже.';
  return '';
}

function updateInstall() {
  if (!selected || !installButton) return;
  const ready = Boolean(selected.build) && libraryReady && !browserIssue() && $('confirmHardware').checked && !locked;
  installButton.disabled = !ready;
  let hint = browserIssue();
  if (!hint && !selected.build) hint = 'Установка станет доступна после публикации бинарников этой версии.';
  else if (!hint && libraryError) hint = 'Не удалось загрузить прошивальщик. Обновите страницу или скачайте USB-комплект.';
  else if (!hint && !libraryReady) hint = 'Загрузка прошивальщика…';
  else if (!hint && locked) hint = 'Завершите текущую установку перед выбором другой сборки.';
  else if (!hint && !$('confirmHardware').checked) hint = 'Подтвердите соответствие оборудования выбранной конфигурации.';
  else if (!hint) hint = 'Откроется диалог ESP Web Tools с выбором порта и подтверждением установки.';
  $('installHint').textContent = hint;
}

function renderInstaller() {
  installer = document.createElement('esp-web-install-button');
  if (selected.build) installer.setAttribute('manifest', new URL(`./${selected.artifact_base}/manifest.json`, location.href).href);
  installButton = node('button', 'Подключить и прошить  ↗', 'install-button');
  installButton.type = 'button';
  installButton.slot = 'activate';
  installButton.disabled = true;
  // The library handles clicks on the slotted button. Stop events when gated,
  // including synthetic clicks on its disabled parent/custom element.
  installer.addEventListener('click', (event) => {
    if (installButton.disabled) {event.preventDefault(); event.stopImmediatePropagation();}
  }, true);
  installer.append(installButton);
  installer.append(Object.assign(node('span', browserIssue(), 'small muted'), {slot: 'unsupported'}));
  installer.append(Object.assign(node('span', browserIssue(), 'small muted'), {slot: 'not-allowed'}));
  $('installerMount').replaceChildren(installer);
  updateInstall();
}

function chooseProfile(id, updateURL = true) {
  if (locked) return;
  const restoreRadioFocus = document.activeElement?.matches('input[name="profile"]');
  selected = catalog.profiles.find((p) => p.id === id);
  if (!selected) return;
  $('confirmHardware').checked = false;
  $('boardSelect').value = selected.board;
  const pills = [selected.chip_family, `Flash ${selected.flash_bytes / 1048576} МБ`,
    selected.psram_bytes ? `PSRAM ${selected.psram_bytes / 1048576} МБ` : 'Без PSRAM'];
  $('boardSpecs').replaceChildren(...pills.map((text) => node('span', text)));
  const photo = catalog.photos[selected.photo];
  $('boardPhoto').src = './images/' + photo.file;
  $('boardPhoto').alt = photo.alt;
  $('photoSource').href = photo.source;
  $('photoCaption').textContent = photo.caption;
  $('sppEraseAdvice').hidden = selected.recipe !== 'esp32_spp';
  $('connectInstruction').textContent = selected.board === 'wt32' ? 'Подключите WT32-ETH01 через USB–UART с логикой 3,3 В по схеме платы.' : 'Подключите плату USB-кабелем с передачей данных.';
  $('bootHelpWT32').hidden = selected.board !== 'wt32';
  $('bootHelpDefault').hidden = selected.board === 'wt32';
  $('profileChoices').replaceChildren(Object.assign(node('legend', 'Конфигурация прошивки', 'sr-only')));
  for (const profile of catalog.profiles.filter((p) => p.board === selected.board && p.id === p.base_id)) {
    const label = node('label', undefined, 'profile-choice');
    const radio = document.createElement('input');
    Object.assign(radio, {type: 'radio', name: 'profile', value: profile.id, checked: profile.base_id === selected.base_id});
    radio.addEventListener('change', () => {
      const next = catalog.profiles.find((p) => p.base_id === profile.base_id && Object.keys(serviceLabels).every(
        (k) => p.services[k] === (p.available_services[k] && selected.services[k])));
      chooseProfile(next?.id || profile.id);
    });
    const text = node('span');
    text.append(node('strong', profile.title), node('span', profile.description, 'description'));
    label.append(radio, text);
    $('profileChoices').append(label);
  }
  $('serviceChoices').replaceChildren(node('legend', 'Дополнительные сервисы'));
  for (const [key, labelText] of Object.entries(serviceLabels)) {
    const available = selected.available_services[key];
    const label = node('label', undefined, available ? '' : 'unavailable');
    const input = document.createElement('input');
    Object.assign(input, {type:'checkbox', id:'service-' + key, checked:selected.services[key], disabled:!available});
    input.dataset.available = String(available);
    input.addEventListener('change', () => {
      const services = {...selected.services, [key]:input.checked};
      const next = catalog.profiles.find((p) => p.base_id === selected.base_id && Object.keys(serviceLabels).every((k) => p.services[k] === services[k]));
      if (next) {chooseProfile(next.id); $('service-' + key).focus({preventScroll:true});}
    });
    label.append(input, node('span', labelText + (available ? '' : ' · исключено в профиле')));
    $('serviceChoices').append(label);
  }
  $('selectedName').textContent = selected.board_label + ' · ' + selected.title;
  $('displayLabel').textContent = 'Дисплей: ' + selected.display;
  $('profileNotes').replaceChildren(...selected.notes.map((text) => node('p', text)));
  const caps = selected.build?.capabilities;
  $('features').replaceChildren(...labels.map(([flag, label]) => {
    const enabled = caps ? (flag === 'OTA_EFFECTIVE' ? caps.OTA_SUPPORT && caps.G5500_HAS_OTA_PARTITIONS : caps[flag]) : null;
    const li = node('li', undefined, enabled === false ? 'disabled' : '');
    const symbol = enabled === null ? '·' : enabled ? '✓' : '—';
    li.append(node('span', symbol, 'feature-icon'), node('span', label));
    li.setAttribute('aria-label', label + ': ' + (enabled === null ? 'состав уточняется после сборки' : enabled ? 'включено' : 'исключено'));
    return li;
  }));
  $('featureNotice').textContent = caps ? 'Состав получен из собранной прошивки. Поддерживаемые сервисы включаются в настройках устройства.' : 'Точный состав и расход памяти появятся после сборки этой версии.';
  const revision = selected.build?.identity.commit || 'main';
  $('documentation').href = `https://github.com/ra3vld/G5500-ESP32S3-Controller/blob/${revision}/${selected.docs}`;
  $('memory').hidden = !selected.build;
  $('download').hidden = !selected.build;
  const status = $('buildStatus');
  if (selected.build) {
    const build = selected.build;
    const percent = build.app_bytes / build.app_limit_bytes * 100;
    status.textContent = '✓ Собрана · приложение помещается в раздел';
    status.className = 'build-status ready';
    $('flashPercent').textContent = percent.toFixed(1) + '%';
    $('flashProgress').value = percent;
    $('flashDetail').textContent = `${mib(build.app_bytes)} из ${mib(build.app_limit_bytes)} · свободно ${kib(build.app_limit_bytes - build.app_bytes)}`;
    $('dram').textContent = kib(build.static_dram_bytes);
    $('iram').textContent = kib(build.static_iram_bytes);
    $('memory').classList.toggle('warning', percent > 90);
    $('download').href = `./${selected.artifact_base}/usb-firmware.zip`;
  } else {
    status.textContent = 'Бинарники этой версии ещё не опубликованы';
    status.className = 'build-status';
    $('download').removeAttribute('href');
  }
  if (updateURL) history.replaceState(null, '', '#profile=' + encodeURIComponent(id));
  renderInstaller();
  if (restoreRadioFocus) $('profileChoices').querySelector('input:checked')?.focus({preventScroll:true});
}

function setLocked(value) {
  locked = value;
  $('boardSelect').disabled = value;
  for (const input of $('profileChoices').querySelectorAll('input')) input.disabled = value;
  for (const input of $('serviceChoices').querySelectorAll('input')) input.disabled = value || input.dataset.available !== 'true';
  $('confirmHardware').disabled = value;
  updateInstall();
}

async function main() {
  try {
    const response = await fetch('./catalog.json', {cache: 'no-cache'});
    if (!response.ok) throw new Error('HTTP ' + response.status);
    catalog = await response.json();
    if (catalog.schema !== 1 || !catalog.profiles?.length || !catalog.identity) throw new Error('Некорректный каталог');
    $('catalogVersion').textContent = catalog.identity.version;
    $('catalogCommit').textContent = catalog.identity.commit + (catalog.identity.dirty ? ' · локальная сборка' : '');
    document.querySelector('.source-link').href = 'https://github.com/ra3vld/G5500-ESP32S3-Controller/tree/' + catalog.identity.commit;
    const boards = new Map(catalog.profiles.map((p) => [p.board, p.board_label]));
    $('boardSelect').replaceChildren(...[...boards].map(([id, label]) => Object.assign(node('option', label), {value: id})));
    $('boardSelect').addEventListener('change', () => chooseProfile(catalog.profiles.find((p) => p.board === $('boardSelect').value).id));
    $('confirmHardware').addEventListener('change', updateInstall);
    // Freeze selection for the lifetime of the library's port/dialog, including
    // its console and pre-install confirmation, not only during write progress.
    new MutationObserver(() => setLocked(Boolean(document.querySelector('ewt-install-dialog'))))
      .observe(document.body, {childList: true});
    const hash = new URLSearchParams(location.hash.slice(1)).get('profile');
    chooseProfile(catalog.profiles.some((p) => p.id === hash) ? hash : catalog.profiles[0].id, false);
    $('workspace').hidden = false;
    if (browserIssue()) {
      $('globalStatus').textContent = browserIssue();
      $('globalStatus').hidden = false;
    }
    try {
      await import('./vendor/install-button.js');
      await customElements.whenDefined('esp-web-install-button');
      libraryReady = true;
    } catch {
      libraryError = true;
    }
    updateInstall();
  } catch {
    $('catalogVersion').textContent = 'Недоступен';
    $('globalStatus').textContent = 'Не удалось загрузить каталог прошивок. Проверьте соединение и обновите страницу.';
    $('globalStatus').classList.add('error');
    $('globalStatus').hidden = false;
  }
}
main();
