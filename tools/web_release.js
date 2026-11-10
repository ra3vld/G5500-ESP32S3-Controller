// OTA devices use their saved source; USB-only builds check public release metadata in the browser.
window.firmwareReleases = (() => {
  const repository = 'https://github.com/ra3vld/G5500-ESP32S3-Controller';
  const api = 'https://api.github.com/repos/ra3vld/G5500-ESP32S3-Controller/releases/latest';
  const interval = 6 * 60 * 60 * 1000;
  let current = '', release = null, busy = false, attempted = false, checkedAt = 0, deviceOta = false;
  const element = id => document.getElementById(id);
  function version(text) {
    const match = /^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/.exec(text || '');
    if (!match) return null;
    const parts = match.slice(1, 4).map(Number);
    return parts.every(Number.isSafeInteger) ? {parts, prerelease:Boolean(match[4])} : null;
  }
  function newer(latest, installed) {
    for (let i = 0; i < 3; i++) {
      if (latest.parts[i] !== installed.parts[i]) return latest.parts[i] > installed.parts[i];
    }
    return installed.prerelease && !latest.prerelease;
  }
  function message(text) {
    const target = element('firmwareReleaseStatus');
    if (target) target.textContent = text;
  }
  function render() {
    const installed = version(current), latest = release && version(release.tag);
    const available = Boolean(installed && latest && newer(latest, installed));
    const badge = element('firmwareUpdateBadge');
    if (badge) {
      badge.hidden = !available;
      badge.textContent = available ? 'Доступна прошивка ' + release.tag : '';
    }
    const link = element('firmwareReleaseLink');
    if (link) link.href = release ? release.url : repository + '/releases/latest';
    if (release) {
      if (!installed) message('Последний релиз: ' + release.tag + '. Текущая версия не определена.');
      else if (available) message('Доступна новая прошивка ' + release.tag + '. Установлена ' + current + '.');
      else message('Новой стабильной прошивки нет. Последний релиз: ' + release.tag + ', установлена ' + current + '.');
    }
    const button = element('firmwareReleaseCheck');
    if (button) button.disabled = busy;
  }
  async function check(force = false) {
    if (busy || (!force && attempted && Date.now() - checkedAt < interval)) return;
    attempted = true; checkedAt = Date.now(); busy = true; render();
    if(deviceOta){if(await loadRemoteOta(force)){busy=false;const b=element('firmwareReleaseCheck');if(b)b.disabled=false;return;}deviceOta=false;}
    message('Проверяем последний релиз на GitHub…');
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 8000);
    try {
      const response = await fetch(api, {credentials:'omit', referrerPolicy:'no-referrer',
        cache:force ? 'reload' : 'default', signal:controller.signal});
      if (response.status === 403 || response.status === 429) throw Error('GitHub ограничил запросы. Повторите позже или откройте релизы по ссылке.');
      if (response.status === 404) throw Error('Опубликованные релизы пока не найдены. Повторите проверку позже.');
      if (!response.ok) throw Error('GitHub вернул ошибку ' + response.status + '. Повторите проверку.');
      const data = await response.json();
      if (data.draft || data.prerelease || !/^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(data.tag_name || '') ||
          data.html_url !== repository + '/releases/tag/' + data.tag_name) {
        throw Error('GitHub вернул неподходящие сведения о релизе. Откройте список релизов по ссылке.');
      }
      release = {tag:data.tag_name, url:data.html_url}; render();
    } catch (error) {
      message(error.name === 'AbortError' ? 'GitHub не ответил за 8 секунд. Повторите проверку или откройте релизы по ссылке.' :
        (error instanceof TypeError ? 'Не удалось связаться с GitHub. Проверьте интернет и блокировщики, затем повторите проверку.' : error.message));
    } finally {
      clearTimeout(timer); busy = false;
      const button = element('firmwareReleaseCheck');
      if (button) button.disabled = false;
    }
  }
  function update(value, ota = false) {
    deviceOta = ota;
    if (typeof value !== 'string' || !value || value === current) return;
    current = value; render();
    if (!attempted) check();
  }
  function open(event) {
    const panel = element('otaSettings');
    if (!panel) return;
    const details = panel.querySelector('details');
    if (details) details.open = true;
    setTimeout(() => panel.scrollIntoView({block:'start', behavior:'smooth'}), 100);
  }
  setInterval(() => {if (current && !document.hidden) check();}, interval);
  document.addEventListener('visibilitychange', () => {if (current && !document.hidden) check();});
  return {update, check, open};
})();
