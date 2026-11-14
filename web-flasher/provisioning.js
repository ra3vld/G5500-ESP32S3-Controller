// Optional preconfiguration and account linking. No secret is put in storage,
// URLs, firmware files, or service requests containing Wi-Fi credentials.
import {SerialApi, pause} from './serial-api.js';

const $ = (id) => document.getElementById(id);
const bytes = (text) => new TextEncoder().encode(text).length;
const cleanText = (text) => !/[\x00-\x1f\x7f]/.test(text);
const ipv4 = (text) => /^(?:\d{1,3}\.){3}\d{1,3}$/.test(text) && text.split('.').every((n) => Number(n) <= 255 && String(Number(n)) === n);
const b64url = (data) => btoa(String.fromCharCode(...data)).replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');

export class ServiceLink {
  constructor(origin) {
    const url = new URL(origin);
    const local = ['localhost','127.0.0.1'].includes(url.hostname) && ['localhost','127.0.0.1'].includes(location.hostname);
    if ((url.protocol !== 'https:' && !(local && url.protocol === 'http:')) || url.origin !== origin) throw Error('Некорректный адрес сервиса.');
    this.origin = origin;
  }
  async call(path, body) {
    let response, data;
    try {
      response = await fetch(this.origin + '/api/provisioning/' + path, {method:'POST', credentials:'omit', cache:'no-store',
        headers:{'Content-Type':'application/json'}, body:JSON.stringify(body), signal:AbortSignal.timeout(15000)});
      data = await response.json();
    } catch {throw Error('Нет связи с rotator.r3v.ru. Проверьте интернет и блокировщики рекламы, затем повторите — текущая привязка сохранена в этой вкладке.');}
    if (!response.ok) {const error = Error(typeof data.detail === 'string' ? data.detail : 'Сервис отклонил привязку.'); error.status = response.status; throw error;}
    return data;
  }
  async start(device, name, model) {
    const verifier = b64url(crypto.getRandomValues(new Uint8Array(48)));
    const challenge = b64url(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(verifier))));
    const data = await this.call('public/sessions', {challenge, device_id:device, name, model});
    if (!/^[0-9a-f-]{36}$/.test(data.id) || data.approval_url !== this.origin + '/provisioning/' + data.id) throw Error('Некорректный ответ сервиса привязки.');
    this.session = {id:data.id, verifier, device};
    this.approvalURL = data.approval_url;
  }
  async poll() {return this.call(`public/${this.session.id}/poll`, {verifier:this.session.verifier});}
  async ack() {return this.call(`public/${this.session.id}/ack`, {verifier:this.session.verifier});}
}

export class Preconfigurator {
  constructor({setBusy}) {
    this.setBusy = setBusy;
    const serviceOrigin = document.querySelector('meta[name="g5500-service"]').content;
    this.service = new ServiceLink(serviceOrigin);
    for (const id of ['presetMqttMode','presetDiscovery','presetIpMode','presetOpenWifi','presetMqttKeepPassword']) $(id).addEventListener('change', () => this.render());
    $('presetForm').addEventListener('submit', (event) => {event.preventDefault(); this.apply();});
    $('presetCancel').addEventListener('click', () => {this.cancelled = true; this.message('Останавливаем настройку. Уже сохранённые параметры останутся в устройстве.');});
    $('presetMqttTls').addEventListener('change', () => {
      if ($('presetMqttTls').checked && $('presetMqttPort').value === '1883') $('presetMqttPort').value = '8883';
      else if (!$('presetMqttTls').checked && $('presetMqttPort').value === '8883') $('presetMqttPort').value = '1883';
    });
    $('presetRefresh').addEventListener('click', () => this.refresh());
    $('presetRestartLink').addEventListener('click', () => {if (this.resetSession(false)) this.apply(this.port);});
    $('presetNewDevice').addEventListener('click', () => this.resetSession(true));
    this.beforeUnload = (event) => {if (this.busy || this.service.credentialPending) event.preventDefault();};
    window.addEventListener('beforeunload', this.beforeUnload);
  }

  select(profile) {if (this.profile?.id !== profile.id) this.localURL = null; this.profile = profile; this.flashDraft = null; this.render();}
  get enabled() {return (this.profile?.build?.capabilities || this.profile?.planned_capabilities || {}).COMMAND_API_SUPPORT === true;}
  lock(installing) {if (installing) this.localURL = null; this.installing = installing; this.render();}
  render() {
    if (!this.profile) return;
    const caps = this.profile.build?.capabilities || this.profile.planned_capabilities || {};
    const supported = caps.COMMAND_API_SUPPORT === true;
    $('presetUnavailable').hidden = supported;
    $('presetUnavailable').textContent = this.profile.recipe === 'esp32_spp'
      ? 'В компактной SPP нет USB API. Выполните первоначальную настройку в основной прошивке, затем установите SPP без стирания.'
      : 'Преднастройка по USB недоступна в этой сборке.';
    $('presetFields').disabled = Boolean(this.busy || this.installing || !supported);
    $('presetApply').disabled = Boolean(this.busy || this.installing || !supported || !('serial' in navigator));
    $('presetCancel').hidden = !this.busy;
    $('presetMqttMode').disabled = !caps.MQTT_SUPPORT || this.busy || this.installing;
    $('presetMqttUnavailable').hidden = Boolean(caps.MQTT_SUPPORT);
    if (!caps.MQTT_SUPPORT) $('presetMqttMode').value = 'preserve';
    $('presetEthernet').disabled = !caps.ETHERNET_SUPPORT;
    if (!caps.ETHERNET_SUPPORT && $('presetInterface').value === 'eth') $('presetInterface').value = 'wifi';
    const manual = $('presetMqttMode').value === 'manual';
    $('presetMqttManual').hidden = !manual;
    $('presetServiceHelp').hidden = $('presetMqttMode').value !== 'service';
    $('presetServiceTroubleshooting').hidden = $('presetMqttMode').value !== 'service';
    $('presetDiscoveryField').hidden = !manual || !$('presetDiscovery').checked;
    $('presetStatic').hidden = $('presetIpMode').value !== 'static';
    $('presetWifiPassword').disabled = $('presetOpenWifi').checked || this.busy || this.installing;
    $('presetMqttPassword').disabled = $('presetMqttKeepPassword').checked || this.busy || this.installing;
    $('presetRestartLink').hidden = !this.service.session || this.busy || this.installing;
    $('presetNewDevice').hidden = !this.expectedId || this.busy || this.installing;
    const localLink = $('presetLocalLink');
    const canOpen = Boolean(this.localURL && caps.WEB_INTERFACE_ENABLED && !this.busy && !this.installing);
    if (canOpen) localLink.href = this.localURL; else localLink.removeAttribute('href');
    localLink.setAttribute('aria-disabled', String(!canOpen));
    localLink.tabIndex = canOpen ? 0 : -1;
    localLink.title = canOpen ? 'Открыть веб-интерфейс устройства в новой вкладке.' : 'Подключитесь по USB и дождитесь проверки сети.';
  }
  message(text, error = false) {
    $('presetProgress').hidden = false; $('presetProgress').dataset.error = String(error); $('presetStatus').textContent = text;
    if (error && text.startsWith('Нет связи с rotator.r3v.ru.')) $('presetServiceTroubleshooting').open = true;
  }
  log(text) {const item = document.createElement('li'); item.textContent = text; $('presetLog').append(item);}
  checkCancelled() {if (this.cancelled) throw Error('Настройка остановлена. Нажмите «Применить настройки», чтобы продолжить.');}

  collect() {
    if (!$('presetForm').reportValidity()) throw Error('Проверьте заполнение полей преднастройки.');
    const value = (id) => $(id).value.trim();
    const draft = {profile:this.profile, name:value('presetName'), ssid:$('presetSsid').value,
      wifiPassword:$('presetWifiPassword').value, openWifi:$('presetOpenWifi').checked,
      ipMode:value('presetIpMode'), interface:value('presetInterface'),
      address:value('presetAddress'), mask:value('presetMask'), gateway:value('presetGateway'), dns:value('presetDns'),
      mqttMode:value('presetMqttMode')};
    if (draft.name && !/^[A-Za-z0-9 _-]{1,31}$/.test(draft.name)) throw Error('Имя контроллера: до 31 латинского символа, цифры, пробел, дефис и подчёркивание.');
    if (draft.ssid && (bytes(draft.ssid) > 32 || !cleanText(draft.ssid))) throw Error('SSID: до 32 байт, без управляющих символов.');
    if (draft.ssid && !draft.openWifi && !((bytes(draft.wifiPassword) >= 8 && bytes(draft.wifiPassword) <= 63) || /^[a-fA-F0-9]{64}$/.test(draft.wifiPassword))) throw Error('Введите пароль Wi-Fi (8–63 байта или 64 шестнадцатеричных символа) либо отметьте открытую сеть.');
    if (draft.openWifi) draft.wifiPassword = '';
    if (draft.ipMode === 'static') {
      for (const field of ['address','mask','gateway','dns']) if ((['address','mask'].includes(field) || draft[field]) && !ipv4(draft[field])) throw Error('Проверьте IP-адрес, маску, шлюз и DNS.');
      const number = (s) => s.split('.').reduce((a, n) => a * 256 + Number(n), 0) >>> 0;
      const mask = number(draft.mask), address = number(draft.address), hosts = ~mask >>> 0;
      if (!mask || (hosts & (hosts + 1)) || hosts < 3 || (address & hosts) === 0 || (address & hosts) === hosts || address >>> 24 === 0 || address >>> 24 >= 224) throw Error('Некорректный адрес или маска подсети.');
      if (draft.gateway && ((number(draft.gateway) & mask) !== (address & mask) || number(draft.gateway) === address)) throw Error('Шлюз должен находиться в той же подсети и отличаться от IP устройства.');
    }
    if (draft.mqttMode === 'manual') {
      const host = value('presetMqttHost'), port = Number(value('presetMqttPort'));
      if (!host || bytes(host) > 95 || /[^\x21-\x7e]|[/#+@]/.test(host)) throw Error('Укажите адрес MQTT-брокера без mqtt://, пути и пробелов.');
      if (!Number.isInteger(port) || port < 1 || port > 65535) throw Error('Порт MQTT: от 1 до 65535.');
      const username = $('presetMqttUser').value, password = $('presetMqttPassword').value;
      const prefix = value('presetDiscoveryPrefix');
      if (bytes(username) > 63 || bytes(password) > 95 || !cleanText(username) || !cleanText(password)) throw Error('Логин MQTT: до 63 байт; пароль: до 95 байт, без управляющих символов.');
      if (!prefix || bytes(prefix) > 47 || /^\/|\/$|[\s#+\x00-\x1f]/.test(prefix)) throw Error('Проверьте префикс Home Assistant Discovery.');
      draft.mqtt = {enabled:true, host, port, tls:$('presetMqttTls').checked, username,
        discovery:$('presetDiscovery').checked, discoveryPrefix:prefix};
      if (!$('presetMqttKeepPassword').checked) draft.mqtt.password = password;
    }
    return draft;
  }
  prepare() {
    if (!this.enabled) {this.flashDraft = null; return true;}
    try {
      const draft = this.collect();
      this.flashDraft = draft.name || draft.ssid || draft.ipMode !== 'preserve' || draft.mqttMode !== 'preserve' ? draft : null;
      return true;
    } catch (error) {this.message(error.message, true); return false;}
  }

  observeDialog(dialog) {
    let handoff = false;
    dialog.addEventListener('g5500-flash-state', (event) => {
      if (event.target !== dialog || !event.cancelable || event.detail?.state !== 'finished' || !this.flashDraft) return;
      event.preventDefault(); handoff = true; this.port = dialog.port;
      const draft = this.flashDraft;
      this.setBusy(true);
      // The flasher has already disconnected its loader. Skip its normal
      // post-install close handler, which would close our newly opened port.
      dialog.addEventListener('closed', (closed) => closed.stopImmediatePropagation(), {capture:true, once:true});
      setTimeout(() => this.apply(this.port, draft), 400);
    });
    dialog.addEventListener('closed', () => {if (!handoff) {this.flashDraft = null; this.render();}});
  }

  async connect(port) {
    if (!port) port = await navigator.serial.requestPort();
    const api = new SerialApi(port); this.port = port;
    try {await api.open(); return api;} catch (error) {await api.close(); throw error;}
  }
  resetSession(newDevice) {
    if (this.service.credentialPending && this.serviceResult?.mqtt?.password &&
        !confirm('MQTT-пароль ещё не подтверждён в плате. При новой привязке потребуется явно заменить его. Продолжить?')) return false;
    this.service = new ServiceLink(this.service.origin); this.serviceResult = null;
    if (newDevice) {this.expectedId = null; this.port = null; this.flashDraft = null; this.localURL = null;}
    for (const id of ['presetConsent','presetDeviceLink','presetRefresh']) $(id).hidden = true;
    this.message(newDevice ? 'Можно выбрать другую плату и начать новый сеанс настройки.' : 'Начинаем новую привязку. Существующая карточка устройства сохранится.');
    this.render(); return true;
  }
  async device(api, draft) {
    const identity = await api.identity();
    if (this.expectedId && this.expectedId !== identity.deviceId) throw Error('Подключена другая плата. Выберите устройство с ID ' + this.expectedId + '.');
    this.expectedId = identity.deviceId;
    const config = await api.request('GET', '/api/config');
    const caps = config.build?.features;
    if (!caps?.commandApi || config.build.displayKind !== draft.profile.display_kind) throw Error('Плата запущена с другой конфигурацией дисплея или без совместимого API. Проверьте прошивку.');
    const status = await api.request('GET', '/api/status');
    if (status.moving) throw Error('Устройство движется. Остановите его перед настройкой.');
    if (status.recovery) throw Error('Устройство в режиме восстановления. Сначала исправьте оборудование в его веб-интерфейсе.');
    if (draft.mqttMode !== 'preserve' && !caps.mqtt) throw Error('В работающей прошивке нет MQTT.');
    if ((draft.mqttMode === 'service' || draft.mqtt?.tls) && !caps.mqttTls) throw Error('Работающая прошивка не поддерживает MQTT TLS.');
    if (draft.interface === 'eth' && draft.ipMode !== 'preserve' && !caps.ethernet) throw Error('В работающей прошивке нет Ethernet.');
    if (draft.mqttMode === 'service' && identity.deviceRole !== 'controller') throw Error('Для сервиса выберите контроллер поворотки. Пульт HUD настраивается отдельно.');
    this.log('USB: контроллер ' + identity.deviceId + ', совместимость проверена.');
    return {identity, config};
  }

  async serviceCredentials(device, config, name) {
    if (this.service.session?.device !== device) {
      await this.service.start(device, name || config.rotorName || 'G5500', config.model === 'G5500' ? 'AZ_EL' : 'AZ');
    }
    $('presetConsent').href = this.service.approvalURL; $('presetConsent').hidden = false;
    this.message('Подтвердите привязку в своём аккаунте rotator.r3v.ru.');
    while (true) {
      this.checkCancelled();
      let result;
      try {result = await this.service.poll();} catch (error) {if (error.status === 410) this.service.session = null; throw error;}
      if (result.status === 'denied') {this.service.session = null; throw Error('Привязка отменена в аккаунте. Можно выбрать свой MQTT или повторить привязку.');}
      if (result.status === 'issued' || result.status === 'complete') {
        if (result.device_id !== device) throw Error('Сервис вернул параметры другого устройства.');
        this.service.credentialPending = result.status === 'issued';
        this.serviceResult = result; $('presetConsent').hidden = true;
        return result;
      }
      await pause(2000);
    }
  }

  async mqtt(api, config) {
    const before = await api.request('GET', '/api/mqtt');
    this.checkCancelled();
    if (config.password === null) {
      if (!before.enabled || !before.tls || !before.hasPassword || before.host !== config.host || before.port !== config.port || before.username !== config.username) throw Error('В плате нет сохранённых параметров этого MQTT-доступа. Откройте новую привязку и явно выберите замену пароля.');
      return;
    }
    const fields = Object.fromEntries(Object.entries(config).map(([key, value]) => [key, String(value)]));
    await api.request('POST', '/api/mqtt', fields);
    const saved = await api.request('GET', '/api/mqtt');
    for (const [key, value] of Object.entries(config)) {
      if (key !== 'password' && saved[key] !== value) throw Error('Устройство не подтвердило сохранение MQTT. Повторите проверку.');
    }
    if (!saved.storageOk || (config.password && !saved.hasPassword)) throw Error('Устройство сообщило об ошибке сохранения MQTT.');
    this.log('MQTT-параметры сохранены и проверены по USB.');
  }

  async apply(port = null, frozen = null) {
    if (this.busy || (!frozen && !this.enabled)) return;
    let draft;
    try {draft = frozen || this.collect();} catch (error) {this.message(error.message, true); return;}
    this.localURL = null; this.busy = true; this.cancelled = false; this.setBusy(true); this.render();
    $('presetLog').replaceChildren();
    this.message('Подключаемся к запущенной прошивке по USB…');
    let api;
    try {
      api = await this.connect(port);
      const {identity, config} = await this.device(api, draft);
      this.checkCancelled();
      if (draft.name && draft.name !== config.rotorName) {
        await api.request('POST', '/api/settings', {rotorName:draft.name});
        if ((await api.request('GET', '/api/config')).rotorName !== draft.name) throw Error('Не подтверждено сохранение имени.');
        this.log('Имя контроллера сохранено.');
      }
      this.checkCancelled();
      if (draft.mqttMode === 'manual') await this.mqtt(api, draft.mqtt);
      else if (draft.mqttMode === 'off') await this.mqtt(api, {enabled:false});
      else if (draft.mqttMode === 'service') {
        const result = await this.serviceCredentials(identity.deviceId, config, draft.name);
        this.checkCancelled();
        if (result.status !== 'complete') {
          await this.mqtt(api, {...result.mqtt, enabled:true, discovery:false});
          await this.service.ack(); this.service.credentialPending = false;
          // Do not retain the one-time password after verified delivery.
          if (this.serviceResult.mqtt) this.serviceResult.mqtt.password = null;
        }
        this.showService(result);
      }
      this.checkCancelled();
      if (draft.ssid) {
        await api.request('POST', '/api/wifi', {ssid:draft.ssid, pass:draft.wifiPassword});
        if ((await api.request('GET', '/api/network')).ssid !== draft.ssid) throw Error('Не подтверждено сохранение Wi-Fi.');
        this.log('Wi-Fi сохранён и проверен.');
      }
      let network = await api.request('GET', '/api/network');
      if (draft.ipMode !== 'preserve') {
        const desired = {mode:draft.ipMode, ...(draft.ipMode === 'static' ? {address:draft.address, mask:draft.mask, gateway:draft.gateway || '0.0.0.0', dns1:draft.dns || '0.0.0.0'} : {})};
        const current = network[draft.interface];
        if (Object.entries(desired).some(([key, value]) => current[key] !== value)) {
          const field = {mode:'Mode', address:'Address', mask:'Mask', gateway:'Gateway', dns1:'Dns1'};
          const values = Object.fromEntries(Object.entries(desired).map(([key, value]) => [draft.interface + field[key], value]));
          if (draft.ipMode === 'static') values[draft.interface + 'Dns2'] = '0.0.0.0';
          values.choice = draft.interface === 'eth' ? '4' : '1';
          this.checkCancelled();
          await api.request('POST', '/api/network', values);
          this.log('IP-параметры сохранены. Ожидаем перезапуск и проверяем сеть…');
          await api.close(); api = null; await pause(4000);
          api = await this.connect(this.port); await this.device(api, draft);
        }
      }
      this.message('Параметры сохранены. Проверяем подключение к сети…');
      for (let attempt = 0; attempt < 25; attempt++) {
        this.checkCancelled(); network = await api.request('GET', '/api/network');
        if (network.connected || network.ethConnected || (!draft.ssid && draft.ipMode === 'preserve')) break;
        await pause(1000);
      }
      if (draft.ssid && network.ssid !== draft.ssid) throw Error('Сохранённая сеть отличается от указанной.');
      this.checkCancelled();
      if (draft.ipMode !== 'preserve' && network[draft.interface].mode !== draft.ipMode) throw Error('IP-настройки не сохранились или были возвращены после неудачного запуска.');
      if (draft.ipMode === 'static' && network[draft.interface].address !== draft.address) throw Error('Статический IP не подтверждён.');
      const ip = network.ethConnected ? network.ethIp : network.connected ? network.staIp : network.apIp;
      if (network.trial && network.trialBooted && (network.connected || network.ethConnected)) {
        await api.request('POST', '/api/network/confirm', {token:String(network.token), address:network.choice === 4 ? network.ethIp : network.staIp});
        if ((await api.request('GET', '/api/network')).trial) throw Error('Не подтверждена новая сетевая конфигурация.');
        this.log('Сетевая конфигурация успешно запущена и подтверждена.');
      }
      if (ip && ipv4(ip) && ip !== '0.0.0.0') this.localURL = 'http://' + ip + '/';
      this.message(network.connected || network.ethConnected ? 'Настройки применены. Устройство подключено к сети.' : 'Настройки сохранены. Подключение к сети пока не подтверждено — проверьте Wi-Fi и повторите проверку.');
      if (draft.mqttMode === 'manual' || draft.mqttMode === 'service') {
        const mqtt = await api.request('GET', '/api/mqtt');
        this.log(mqtt.connected ? 'Устройство подтвердило подключение к MQTT.' : 'MQTT ещё не подключён' + (mqtt.error ? ': ' + mqtt.error : '.') + ' Состояние можно проверить в интерфейсе устройства.');
      }
      if (draft.mqttMode === 'service') await this.refresh();
    } catch (error) {
      const text = error.name === 'NotFoundError' ? 'USB-порт не выбран. Вы можете повторить подключение.' : error.message;
      this.message(text, true);
    } finally {
      await api?.close(); this.busy = false; this.setBusy(false); this.render();
      this.flashDraft = null;
    }
  }

  showService(result) {
    if (result.device_url !== this.service.origin + '/?rotator=' + result.rotator_id || !/^[0-9a-f-]{36}$/.test(result.rotator_id)) throw Error('Некорректная ссылка устройства от сервиса.');
    $('presetDeviceLink').href = result.device_url; $('presetDeviceLink').hidden = false; $('presetRefresh').hidden = false;
    if (!result.approved) this.log('Карточка создана. MQTT-доступ ожидает одобрения администратора.');
    else if (result.blocked || !result.enabled) this.log('Карточка создана. Доступ MQTT заблокирован или выключен — проверьте его в кабинете.');
    else this.log(result.online ? 'Сервис подтвердил свежую телеметрию устройства.' : 'Карточка есть в шеке. Ожидаем подключение контроллера.');
  }
  async refresh() {
    if (!this.service.session) return;
    $('presetRefresh').disabled = true;
    try {this.showService(await this.service.poll());} catch (error) {this.message(error.message, true);}
    finally {$('presetRefresh').disabled = false;}
  }
}
