const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '..');
const directory = path.join(root, 'dist/web-flasher');
const original = JSON.parse(fs.readFileSync(path.join(directory, 'catalog.json')));
const types = {'.html':'text/html', '.js':'text/javascript', '.css':'text/css', '.json':'application/json', '.webp':'image/webp', '.svg':'image/svg+xml'};
const server = http.createServer((request, response) => {
  const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
  const relative = pathname.replace(/^\/controller\//, '') || 'index.html';
  const file = path.resolve(directory, relative);
  if (!file.startsWith(directory + path.sep) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    response.writeHead(404); response.end(); return;
  }
  response.setHeader('Content-Type', types[path.extname(file)] || 'application/octet-stream');
  response.end(fs.readFileSync(file));
});
const output = path.join(root, 'artifacts/web-flasher-screenshots');
fs.mkdirSync(output, {recursive:true});

function readyCatalog() {
  const data = structuredClone(original);
  for (const profile of data.profiles) {
    // Explicit UI fixtures; these are never written to the published site.
    const capabilities = Object.fromEntries(['WEB_INTERFACE_ENABLED','WIFI_SUPPORT','MQTT_SUPPORT',
      'HOME_ASSISTANT_DISCOVERY_SUPPORT','GS232_SUPPORT','ROTCTLD_SUPPORT','N1MM_SUPPORT','PSTROTATOR_SUPPORT',
      'HUD_SUPPORT','MODBUS_RTU_SUPPORT','BLUETOOTH_SUPPORT','WEB_LIVE_SUPPORT','OTA_SUPPORT','G5500_HAS_OTA_PARTITIONS'].map((name) => [name, true]));
    for (const [key, flags] of Object.entries({mqtt:['MQTT_SUPPORT','HOME_ASSISTANT_DISCOVERY_SUPPORT'],ethernet:['ETHERNET_SUPPORT'],rs485:['RS485_SUPPORT','MODBUS_RTU_SUPPORT']})) {
      for (const flag of flags) capabilities[flag] = profile.services[key];
    }
    capabilities.CAMERA_ENABLED = profile.id === 'waveshare_esp32s3_eth';
    capabilities.BLUETOOTH_SPP_SUPPORT = profile.chip_family === 'ESP32' && !profile.id.includes('compact');
    if (profile.id.includes('compact')) capabilities.WEB_LIVE_SUPPORT = false;
    if (profile.recipe === 'esp32_spp') {
      for (const flag of ['WEB_LIVE_SUPPORT','BLUETOOTH_SUPPORT','HUD_SUPPORT','OTA_SUPPORT']) capabilities[flag] = false;
    }
    Object.assign(capabilities, profile.required_capabilities || {});
    // The actual BIN metadata must override a different planned composition.
    profile.planned_capabilities = Object.fromEntries(Object.entries(capabilities).map(([key, value]) => [key, !value]));
    profile.build = {identity:data.identity, capabilities, app_bytes:2400000 - Object.values(profile.services).filter((v) => !v).length * 10000, app_limit_bytes:6553600,
      static_dram_bytes:48000, static_iram_bytes:16000};
    profile.artifact_base = 'firmware/ui-test-fixture/' + profile.id;
  }
  return data;
}

(async () => {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}/controller/`;
  const browser = await chromium.launch({headless:true, ...(process.env.CHROMIUM_PATH ? {executablePath:process.env.CHROMIUM_PATH} : {}), args:['--no-sandbox']});
  const errors = [];
  try {
    const context = await browser.newContext({viewport:{width:1280,height:1000}});
    await context.addInitScript(() => Object.defineProperty(navigator, 'serial', {configurable:true, value:{requestPort:async () => {
      window.requestPortCalls = (window.requestPortCalls || 0) + 1;
      throw new DOMException('User cancelled port selection', 'NotFoundError');
    }}}));
    const page = await context.newPage();
    page.on('pageerror', (error) => errors.push(error.message));
    const external = [];
    page.on('request', (request) => {if (!request.url().startsWith(base)) external.push(request.url());});
    await page.goto(base);
    await page.locator('#workspace').waitFor({state:'visible'});
    const faviconURL = await page.locator('link[rel="icon"]').evaluate((link) => link.href);
    assert.equal(faviconURL, base + 'images/favicon.svg');
    const favicon = await page.request.get(faviconURL);
    assert.equal(favicon.status(), 200);
    assert.match(favicon.headers()['content-type'], /image\/svg\+xml/);
    assert.deepEqual(await favicon.body(), fs.readFileSync(path.join(root, 'web-flasher/images/favicon.svg')));
    assert.equal(await page.getByText('Ваша плата.', {exact:false}).count(), 0);
    assert.equal(await page.getByText('Ваша конфигурация.', {exact:false}).count(), 0);
    assert.equal(await page.getByText('G5500 Controller · RA3VLD / UA1CFM', {exact:true}).count(), 0);
    await page.waitForFunction(() => Boolean(customElements.get('esp-web-install-button')));
    assert.equal(await page.locator('#boardSelect option').count(), 10);
    assert.deepEqual(await page.locator('#boardSelect option').evaluateAll((options) => options.map((option) => option.value)),
      ['s3', 'esp32', 'c3', 's2mini', 's2legacy', 'guition', 'sunton', 'crowpanel', 'wt32', 'waveshare']);
    const selectedOriginal = original.profiles[0];
    assert.equal(await page.locator('.install-button').isDisabled(), true);
    if (selectedOriginal.build) {
      const manifestURL = base + selectedOriginal.artifact_base + '/manifest.json';
      const response = await page.request.get(manifestURL);
      assert.equal(response.status(), 200);
      const manifest = await response.json();
      assert.equal(manifest.builds[0].chipFamily, selectedOriginal.chip_family);
      for (const part of manifest.builds[0].parts) {
        const image = await page.request.get(new URL(part.path, manifestURL).href);
        assert.equal(image.status(), 200);
        const expected = selectedOriginal.build.files.find((file) => file.path === part.path);
        assert.equal((await image.body()).length, expected.size);
      }
      for (const width of [1280, 390]) {
        await page.setViewportSize({width,height:1000});
        await page.screenshot({path:path.join(output, `catalog-${width}.png`), fullPage:true});
      }
      await page.setViewportSize({width:1280,height:1000});
    }
    if (!selectedOriginal.build) {
      assert.match(await page.locator('#buildStatus').innerText(), /ещё не опубликованы/);
      assert.equal(await page.locator('#memory').isVisible(), false);
      await page.locator('#confirmHardware').check();
      assert.equal(await page.locator('.install-button').isDisabled(), true);
    }
    // A catalog without BINs still shows the profile's evaluated composition.
    for (const profile of original.profiles.filter((p) => !p.build)) {
      assert.ok(profile.planned_capabilities, profile.id);
      await page.locator('#boardSelect').selectOption(profile.board);
      await page.locator(`input[value="${profile.base_id}"]`).check();
      if (Object.values(profile.selectable_services).some(Boolean)) await page.locator('#service-mqtt').setChecked(profile.services.mqtt);
      const rows = await page.locator('#features li').evaluateAll((els) => els.map((el) => ({flag:el.dataset.feature, symbol:el.querySelector('.feature-icon').textContent})));
      for (const row of rows) {
        const caps = profile.planned_capabilities;
        const enabled = row.flag === 'OTA_EFFECTIVE' ? caps.OTA_SUPPORT && caps.G5500_HAS_OTA_PARTITIONS : caps[row.flag];
        assert.equal(row.symbol, enabled ? '✓' : '—', profile.id + ':' + row.flag);
      }
      assert.match(await page.locator('#featureNotice').innerText(), /по конфигурации профиля/);
      assert.equal(await page.locator('#memory').isVisible(), false);
      assert.equal(await page.locator('.install-button').isDisabled(), true);
      assert.equal(await page.locator('esp-web-install-button').getAttribute('manifest'), null);
    }
    await page.goto(base);
    await page.locator('#workspace').waitFor({state:'visible'});
    // Check the real catalog before any UI fixtures replace its metadata.
    for (const profile of original.profiles.filter((p) => p.build)) {
      await page.locator('#boardSelect').selectOption(profile.board);
      await page.locator(`input[value="${profile.base_id}"]`).check();
      if (Object.values(profile.selectable_services).some(Boolean)) await page.locator('#service-mqtt').setChecked(profile.services.mqtt);
      const rows = await page.locator('#features li').evaluateAll((els) => els.map((el) => ({flag:el.dataset.feature, symbol:el.querySelector('.feature-icon').textContent})));
      for (const row of rows) {
        const caps = profile.build.capabilities;
        const enabled = row.flag === 'OTA_EFFECTIVE' ? caps.OTA_SUPPORT && caps.G5500_HAS_OTA_PARTITIONS : caps[row.flag];
        assert.equal(row.symbol, enabled ? '✓' : '—', profile.id + ':' + row.flag);
      }
      assert.match(await page.locator('#featureNotice').innerText(), /из собранной прошивки/);
      assert.equal(await page.locator('#flashPercent').innerText(), (profile.build.app_bytes / profile.build.app_limit_bytes * 100).toFixed(1) + '%');
      assert.equal(await page.locator('esp-web-install-button').getAttribute('manifest'), base + profile.artifact_base + '/manifest.json');
    }
    for (const real of original.profiles.filter((p) => ['esp32_spp','esp32_3248s035c','wt32_eth01','crowpanel_21','crowpanel_21_az'].includes(p.id) && p.build)) {
      await page.goto(base + '?preview=' + real.id + '#profile=' + real.id);
      await page.locator('#memory').waitFor({state:'visible'});
      await page.waitForFunction(() => document.getElementById('boardPhoto').complete && document.getElementById('boardPhoto').naturalWidth > 0);
      assert.match(await page.locator('esp-web-install-button').getAttribute('manifest'), new RegExp(real.id + '/manifest.json$'));
      assert.equal(await page.locator('#flashPercent').innerText(), (real.build.app_bytes / real.build.app_limit_bytes * 100).toFixed(1) + '%');
      assert.equal(await page.locator('#sppEraseAdvice').isVisible(), real.id === 'esp32_spp');
      assert.equal(await page.locator('#bootHelpWT32').isVisible(), real.id === 'wt32_eth01');
      for (const width of [1280,390]) {
        await page.setViewportSize({width,height:1000});
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        await page.screenshot({path:path.join(output, `${real.id}-${width}.png`), fullPage:true});
      }
      await page.setViewportSize({width:1280,height:1000});
      await page.goto(base);
    }
    const fixture = readyCatalog();
    await page.route('**/catalog.json', (route) => route.fulfill({json:fixture}));
    await page.reload();
    await page.waitForFunction(() => Boolean(customElements.get('esp-web-install-button')));
    await page.locator('#memory').waitFor({state:'visible'});
    assert.equal(await page.locator('.install-button').isDisabled(), true);
    assert.match(await page.locator('#featureNotice').innerText(), /получен из собранной прошивки/);
    await page.locator('#confirmHardware').check();
    await page.waitForFunction(() => !document.querySelector('.install-button').disabled);
    assert.match(await page.locator('esp-web-install-button').getAttribute('manifest'), /\/controller\/firmware\/ui-test-fixture\/esp32s3_n16r8_headless\/manifest.json$/);
    assert.match(await page.locator('#flashDetail').innerText(), /свободно/);
    const boards = [...new Set(fixture.profiles.map((p) => p.board))];
    for (const board of boards) {
      await page.locator('#boardSelect').selectOption(board);
      assert.equal(await page.locator('#bootHelpWT32').isVisible(), board === 'wt32');
      if (board === 'wt32') assert.match(await page.locator('#connectInstruction').innerText(), /USB–UART.*3,3/);
      for (const profile of fixture.profiles.filter((p) => p.board === board && p.id === p.base_id)) {
        await page.locator(`input[name="profile"][value="${profile.id}"]`).check();
        if (Object.values(profile.selectable_services).some(Boolean)) await page.locator('#service-mqtt').check();
        await page.waitForFunction(() => document.getElementById('boardPhoto').complete && document.getElementById('boardPhoto').naturalWidth > 0);
        assert.match(await page.locator('#selectedName').innerText(), new RegExp(profile.title.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
        assert.equal(await page.locator('#confirmHardware').isChecked(), false);
        assert.equal(await page.locator('.install-button').isDisabled(), true);
        assert.match(await page.locator('#documentation').getAttribute('href'), new RegExp((fixture.documentation_base ? profile.docs.replace(/\.md$/, '.html') : profile.docs) + '$'));
        assert.match(await page.locator('#download').getAttribute('href'), new RegExp(profile.id + '/usb-firmware.zip$'));
        assert.equal(new URL(page.url()).hash, '#profile=' + profile.id);
        if (['esp32_4mb','esp32c3_4mb'].includes(profile.id)) {
          for (const width of [1280,390]) {
            await page.setViewportSize({width,height:1000});
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
            await page.screenshot({path:path.join(output, `main-${profile.id}-${width}.png`), fullPage:true});
          }
          await page.setViewportSize({width:1280,height:1000});
        }
      }
    }
    for (const board of boards.filter((b) => !['s3','crowpanel'].includes(b))) {
      await page.locator('#boardSelect').selectOption(board);
      assert.equal(await page.locator('#serviceChoices').isVisible(), false);
      assert.equal(await page.locator('#serviceHint').isVisible(), false);
      assert.equal(await page.locator('input[name="profile"]').count(), ['esp32','waveshare'].includes(board) ? 2 : 1);
    }
    // The paired service switch chooses the exact prebuilt manifest,
    // updates measured sizes, resets approval and survives a deep-link reload.
    await page.locator('#boardSelect').selectOption('s3');
    await page.locator('input[value="esp32s3_n16r8_headless"]').check();
    for (const variant of fixture.profiles.filter((p) => p.base_id === 'esp32s3_n16r8_headless')) {
      await page.locator('#service-mqtt').setChecked(variant.services.mqtt);
      assert.equal(await page.locator('#service-rs485').count(), 0);
      assert.equal(new URL(page.url()).hash, '#profile=' + variant.id);
      assert.equal(await page.locator('#flashPercent').innerText(), (variant.build.app_bytes / variant.build.app_limit_bytes * 100).toFixed(1) + '%');
      assert.match(await page.locator('esp-web-install-button').getAttribute('manifest'), new RegExp(variant.id + '/manifest.json$'));
      assert.match(await page.locator('#download').getAttribute('href'), new RegExp(variant.id + '/usb-firmware.zip$'));
      assert.equal(await page.locator('#confirmHardware').isChecked(), false);
      for (const [key, label] of Object.entries({mqtt:'MQTT',ethernet:'Ethernet',rs485:'RS-485'})) {
        assert.equal(await page.getByLabel(label + ': ' + (variant.services[key] ? 'включено' : 'исключено'), {exact:true}).count(), 1);
      }
      await page.locator('#confirmHardware').check();
    }
    await page.reload();
    await page.locator('#memory').waitFor({state:'visible'});
    assert.equal(await page.locator('#service-ethernet').isChecked(), false);
    assert.equal(await page.locator('#service-mqtt').isChecked(), false);
    assert.equal(await page.locator('#service-rs485').count(), 0);
    assert.equal(await page.locator('input[value="esp32s3_n16r8_headless"]').isChecked(), true);
    await page.locator('#boardSelect').selectOption('waveshare');
    assert.equal(await page.locator('input[name="profile"]').count(), 2);
    for (const id of ['waveshare_esp32s3_eth', 'waveshare_esp32s3_eth_nocam']) {
      await page.locator(`input[value="${id}"]`).check();
      assert.equal(await page.locator('#serviceChoices').isVisible(), false);
      for (const label of ['MQTT','Ethernet','RS-485']) assert.equal(await page.getByLabel(label + ': включено', {exact:true}).count(), 1);
      assert.equal(await page.getByLabel('Камера: ' + (id.endsWith('_nocam') ? 'исключено' : 'включено'), {exact:true}).count(), 1);
      assert.match(await page.locator('esp-web-install-button').getAttribute('manifest'), new RegExp(id + '/manifest.json$'));
    }
    await page.locator('#boardSelect').selectOption('crowpanel');
    for (const baseId of ['crowpanel_21', 'crowpanel_21_az']) {
      await page.locator(`input[value="${baseId}"]`).check();
      for (const enabled of [true,false]) {
        await page.locator('#confirmHardware').check();
        await page.getByLabel('MQTT + RS-485', {exact:true}).setChecked(enabled);
        const id = baseId + (enabled ? '' : '_no_mqtt_rs485');
        assert.equal(new URL(page.url()).hash, '#profile=' + id);
        assert.equal(await page.locator('#service-rs485').count(), 0);
        for (const label of ['MQTT','RS-485']) assert.equal(await page.getByLabel(label + ': ' + (enabled ? 'включено' : 'исключено'), {exact:true}).count(), 1);
        assert.match(await page.locator('esp-web-install-button').getAttribute('manifest'), new RegExp(id + '/manifest.json$'));
      }
    }
    await page.locator('#boardSelect').selectOption('esp32');
    await page.locator('input[value="esp32_spp"]').check();
    for (const key of ['mqtt','ethernet','rs485']) assert.equal(await page.locator('#service-' + key).isDisabled(), true);
    assert.equal(await page.locator('#sppEraseAdvice').isVisible(), true);
    assert.match(await page.locator('#profileNotes').innerText(), /обрывы HTTP/);
    assert.match(await page.locator('#profileNotes').innerText(), /TCP 4533.*UDP 12040.*UDP 1234/);
    for (const label of ['GS-232','rotctld','N1MM','PstRotator','Bluetooth Classic SPP']) {
      assert.equal(await page.getByLabel(label + ': включено', {exact:true}).count(), 1);
    }
    for (const label of ['MQTT','Ethernet','RS-485','Live / WebSocket','Удалённый пульт HUD','Обновление OTA']) {
      assert.equal(await page.getByLabel(label + ': исключено', {exact:true}).count(), 1);
    }
    for (const width of [1280,390]) {
      await page.setViewportSize({width,height:1000});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.screenshot({path:path.join(output, `esp32-spp-protocols-${width}.png`), fullPage:true});
    }
    await page.setViewportSize({width:1280,height:1000});
    await page.locator('#boardSelect').selectOption('esp32');
    await page.locator('input[value="esp32_4mb"]').check();
    assert.equal(await page.getByLabel('Bluetooth Classic SPP: исключено', {exact:true}).count(), 1);
    assert.equal(await page.getByLabel('MQTT: включено', {exact:true}).count(), 1);
    assert.equal(await page.getByLabel('Удалённый пульт HUD: включено', {exact:true}).count(), 1);
    assert.equal(await page.locator('input[name="profile"]').count(), 2);
    assert.equal(await page.locator('#serviceChoices').isVisible(), false);
    assert.match(await page.locator('#selectedName').innerText(), /Полная.*без SPP/);
    // The pinned vendor bridge must be present in the actual shipped callback.
    const dialogName = fs.readdirSync(path.join(directory, 'vendor')).find((n) => /^install-dialog-.*\.js$/.test(n));
    assert.match(fs.readFileSync(path.join(directory, 'vendor', dialogName), 'utf8'), /this\._installState=e,this\.dispatchEvent\(new CustomEvent\("g5500-flash-state"/);
    for (const [board, profile, expected] of [
      ['guition','esp32_2424s012c','esp32-2424s012c.html#местный-релейный-привод'],
      ['crowpanel','crowpanel_21','crowpanel-21.html#подключения'],
      ['crowpanel','crowpanel_21_az','crowpanel-az.html#вход-положения-вместо-подсветки'],
      ['s3','esp32s3_n16r8_gc9a01','generic-displays.html#подключение-и-первая-настройка'],
      ['esp32','esp32_spp','classic-esp32.html#подключение'],
      ['s3','esp32s3_n16r8_headless','g5500/index.html'],
    ]) {
      await page.locator('#boardSelect').selectOption(board);
      await page.locator('input[value="' + profile + '"]').check();
      assert.equal(await page.locator('#postInstall').isVisible(), false);
      await page.evaluate(() => document.body.appendChild(document.createElement('ewt-install-dialog')));
      await page.waitForFunction(() => document.getElementById('boardSelect').disabled);
      const flashState = (state) => page.evaluate((state) => document.querySelector('ewt-install-dialog').dispatchEvent(
        new CustomEvent('g5500-flash-state', {detail:{state}, bubbles:true, composed:true})), state);
      for (const state of ['initializing','writing','error']) {
        await flashState(state);
        assert.equal(await page.locator('#postInstall').isVisible(), false);
      }
      await flashState('finished');
      assert.equal(await page.locator('#postInstall').isVisible(), true);
      const url = await page.locator('#postInstallConnections').getAttribute('href');
      assert.ok(decodeURI(url).endsWith(expected), url);
      const doc = await page.request.get(url);
      assert.equal(doc.status(), 200);
      if (expected.includes('#')) assert.ok((await doc.text()).includes('id="' + expected.split('#')[1] + '"'));
      await page.evaluate(() => document.querySelector('ewt-install-dialog').remove());
      await page.waitForFunction(() => !document.getElementById('boardSelect').disabled);
      if (board === 'guition') {
        for (const width of [1280,390]) {
          await page.setViewportSize({width,height:1000});
          for (const selector of ['#postInstall > strong','#postInstallProfile','#postInstallConnections']) {
            assert.ok(await page.locator(selector).evaluate((element) => parseFloat(getComputedStyle(element).fontSize) >= 24), selector);
          }
          assert.ok((await page.locator('#postInstallConnections').boundingBox()).height >= 96);
          assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
          await page.locator('#postInstall').screenshot({path:path.join(output, `post-install-guition-${width}.png`)});
        }
        await page.setViewportSize({width:1280,height:1000});
      }
    }
    for (const width of [1280,390]) {
      await page.setViewportSize({width,height:1000});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.screenshot({path:path.join(output, `post-install-${width}.png`), fullPage:true});
    }
    await page.locator('#boardSelect').selectOption('esp32');
    assert.equal(await page.locator('#postInstall').isVisible(), false);
    // A live installer dialog freezes selection for the whole session.
    await page.evaluate(() => document.body.appendChild(document.createElement('ewt-install-dialog')));
    await page.waitForFunction(() => document.getElementById('boardSelect').disabled);
    assert.equal(await page.locator('#confirmHardware').isDisabled(), true);
    for (const key of ['mqtt','ethernet','rs485']) assert.equal(await page.locator('#service-' + key).isDisabled(), true);
    await page.evaluate(() => document.querySelector('ewt-install-dialog').remove());
    await page.waitForFunction(() => !document.getElementById('boardSelect').disabled);
    await page.locator('#boardSelect').selectOption('s3');
    await page.locator('input[value="esp32s3_n16r8_headless"]').focus();
    await page.keyboard.press('ArrowRight');
    assert.equal(await page.evaluate(() => document.activeElement.value), 'esp32s3_n16r8_tft', 'radio selection retains keyboard focus');
    await page.locator('input[value="esp32s3_n16r8_headless"]').check();
    for (const width of [1280, 390]) {
      await page.setViewportSize({width,height:1000});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, `overflow at ${width}`);
      await page.screenshot({path:path.join(output, `installer-${width}.png`), fullPage:true});
    }
    // Exercise the real bundled Web Serial entry point with a cancelled picker.
    await page.locator('#confirmHardware').check();
    await page.locator('.install-button').click();
    await page.waitForFunction(() => window.requestPortCalls === 1);
    await page.waitForFunction(() => Boolean(customElements.get('ewt-install-dialog')));
    assert.deepEqual(external, [], 'all browser libraries must be self-hosted');
    await page.close();

    const unsupported = await browser.newContext();
    await unsupported.addInitScript(() => {delete Navigator.prototype.serial;});
    const noSerial = await unsupported.newPage();
    await noSerial.route('**/catalog.json', (route) => route.fulfill({json:fixture}));
    await noSerial.goto(base);
    await noSerial.locator('#workspace').waitFor({state:'visible'});
    await noSerial.locator('#confirmHardware').check();
    assert.equal(await noSerial.locator('.install-button').isVisible(), false);
    assert.match(await noSerial.locator('#globalStatus').innerText(), /Web Serial/);
    assert.equal(await noSerial.locator('#download').isVisible(), true);
    await unsupported.close();

    const insecure = await browser.newContext();
    await insecure.addInitScript(() => Object.defineProperty(window, 'isSecureContext', {value:false}));
    const httpPage = await insecure.newPage();
    await httpPage.route('**/catalog.json', (route) => route.fulfill({json:fixture}));
    await httpPage.goto(base);
    await httpPage.locator('#workspace').waitFor({state:'visible'});
    await httpPage.locator('#confirmHardware').check();
    assert.match(await httpPage.locator('#globalStatus').innerText(), /HTTPS/);
    assert.equal(await httpPage.locator('.install-button').isDisabled(), true);
    await insecure.close();

    const broken = await context.newPage();
    await broken.route('**/catalog.json', (route) => route.fulfill({status:503, body:'Unavailable'}));
    await broken.goto(base);
    await broken.locator('#globalStatus').waitFor({state:'visible'});
    assert.match(await broken.locator('#globalStatus').innerText(), /Не удалось загрузить каталог/);
    assert.equal(await broken.locator('#workspace').isVisible(), false);
    await broken.close();

    const noLibrary = await context.newPage();
    await noLibrary.route('**/catalog.json', (route) => route.fulfill({json:fixture}));
    await noLibrary.route('**/vendor/**', (route) => route.abort());
    await noLibrary.goto(base);
    await noLibrary.locator('#confirmHardware').check();
    await noLibrary.waitForFunction(() => document.getElementById('installHint').textContent.includes('Не удалось загрузить прошивальщик'));
    assert.equal(await noLibrary.locator('.install-button').isDisabled(), true);
    await noLibrary.close();
    assert.deepEqual(errors, []);
    console.log('PASS: 26 variants / 18 profiles / 10 photographed boards, paired MQTT+RS485, SPP and erase guidance, exact selected manifest, capability exclusions, confirmation reset, memory, missing builds, cancelled serial picker with real ESP Web Tools, frozen dialog selection, unsupported browser, failed catalog/library, self-hosted assets, desktop/mobile layout');
  } finally {
    await browser.close();
    await new Promise((resolve) => server.close(resolve));
  }
})().catch((error) => {console.error(error);server.close();process.exitCode=1;});
