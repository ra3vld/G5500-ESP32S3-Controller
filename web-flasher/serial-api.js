// G5500 command API over Web Serial. GPL-3.0.
// POST retries keep the identical cid/body; a changed boot/session is an error.
export const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export class SerialApi {
  constructor(port, {timeout = 1800, retries = 3} = {}) {
    this.port = port; this.timeout = timeout; this.retries = retries; this.cid = 0;
    this.lines = []; this.waiters = []; this.buffer = ''; this.closed = false;
  }

  async open() {
    await this.port.open({baudRate:9600, bufferSize:8192});
    // Espressif native CDC (notably S2/TinyUSB) gates TX on DTR. External
    // UART bridges keep both modem lines inactive to avoid asserting BOOT/EN.
    const nativeUSB = this.port.getInfo?.().usbVendorId === 0x303a;
    try {await this.port.setSignals({dataTerminalReady:nativeUSB, requestToSend:false});} catch { /* Some USB drivers omit modem signals. */ }
    this.reader = this.port.readable.getReader();
    this.reading = this.readLoop();
    const deadline = Date.now() + 30000;
    do {
      try {await this.hello(); return this;} catch (error) {
        if (this.closed || Date.now() >= deadline) throw error;
        await pause(400);
      }
    } while (true);
  }

  async readLoop() {
    const decoder = new TextDecoder();
    try {
      while (!this.closed) {
        const {value, done} = await this.reader.read();
        if (done) break;
        this.buffer += decoder.decode(value, {stream:true});
        if (this.buffer.length > 65536) throw Error('Слишком длинный ответ USB. Переподключите плату.');
        const parts = this.buffer.split(/[\r\n]+/); this.buffer = parts.pop();
        for (const line of parts) {
          if (!line.trim()) continue;
          let packet;
          try {packet = JSON.parse(line);} catch {continue;}
          if (packet?.v !== 1) continue;
          if (this.waiters.length) this.waiters.shift()(packet);
          else {this.lines.push(packet); if (this.lines.length > 32) this.lines.shift();}
        }
      }
    } catch (error) {this.readError = error;}
    finally {this.closed = true; this.reader.releaseLock(); while (this.waiters.length) this.waiters.shift()(null);}
  }

  next(ms) {
    if (this.lines.length) return Promise.resolve(this.lines.shift());
    if (this.closed) return Promise.reject(Error('USB отключён. Подключитесь снова для проверки сохранённых настроек.'));
    return new Promise((resolve, reject) => {
      const ready = (packet) => {clearTimeout(timer); packet ? resolve(packet) : reject(Error('USB отключён. Подключитесь снова.'));};
      const timer = setTimeout(() => {
        const index = this.waiters.indexOf(ready); if (index >= 0) this.waiters.splice(index, 1);
        reject(Error('Нет ответа устройства по USB. Закройте монитор порта и повторите.'));
      }, Math.max(1, ms));
      this.waiters.push(ready);
    });
  }

  async exchange(fields) {
    const cid = ++this.cid;
    const request = '@' + new URLSearchParams({v:'1', cid:String(cid), ...fields}).toString() + '\r\n';
    if (request.length - 2 > 1024) throw Error('Параметры не помещаются в USB-запрос. Сократите текстовые поля.');
    for (let attempt = 0; attempt < this.retries; attempt++) {
      if (this.closed) throw Error('USB отключён. Переподключитесь для проверки настроек.');
      const writer = this.port.writable.getWriter();
      try {await writer.write(new TextEncoder().encode(request));} finally {writer.releaseLock();}
      const deadline = Date.now() + this.timeout;
      while (Date.now() < deadline) {
        let reply;
        try {reply = await this.next(deadline - Date.now());} catch (error) {if (this.closed) throw error; break;}
        if (reply.cid === cid) return reply;
      }
    }
    throw Error('Устройство не подтвердило запрос. Повторите подключение и проверьте сохранённые настройки.');
  }

  async hello() {
    const numbers = crypto.getRandomValues(new Uint32Array(1));
    const reply = await this.exchange({method:'GET', path:'/api/hello', client:String(numbers[0] || 1)});
    if (reply.status !== 200 || !/^[0-9a-f]{16}$/.test(reply.boot) || !Number.isInteger(reply.sid) || !reply.sid) {
      throw Error('Нет совместимого командного API. Проверьте выбранную прошивку и USB-порт.');
    }
    this.boot = reply.boot; this.sid = reply.sid; this.selection = reply.selection || 0;
  }

  async request(method, path, fields = {}) {
    for (const key of ['v','cid','boot','sid','method','path','page','client','selection']) {
      if (key in fields) throw Error('Недопустимое поле USB-запроса.');
    }
    for (let attempt = 0; attempt < (method === 'GET' ? 3 : 1); attempt++) {
      let offset = 0, revision, total; const chunks = [];
      do {
        const reply = await this.exchange({method, path, boot:this.boot, sid:String(this.sid), selection:String(this.selection), page:String(offset), ...fields});
        if (reply.boot !== this.boot || reply.sid !== this.sid) throw Error('Устройство перезапустилось или изменился сеанс. Переподключитесь для проверки.');
        if (reply.error || !Number.isInteger(reply.status)) throw Error('Устройство отклонило USB-запрос. Переподключитесь и проверьте настройки.');
        if (revision === undefined) {revision = reply.revision; total = reply.total;}
        else if (revision !== reply.revision || total !== reply.total) break;
        if (!Number.isInteger(total) || total < 0 || total > 65536 || reply.offset !== offset) throw Error('Некорректный размер ответа USB.');
        const chunk = Uint8Array.from(atob(reply.base64), (c) => c.charCodeAt(0));
        chunks.push(chunk); offset += chunk.length;
        if (offset > total) throw Error('Некорректная страница USB.');
        this.selection = reply.selection ?? this.selection;
        if (reply.next === null) {
          if (offset !== total) throw Error('Неполный ответ USB.');
          const bytes = new Uint8Array(total); let at = 0;
          for (const item of chunks) {bytes.set(item, at); at += item.length;}
          const text = new TextDecoder('utf-8', {fatal:true}).decode(bytes);
          const body = reply.type === 'application/json' ? JSON.parse(text) : text;
          if (reply.status < 200 || reply.status >= 300) {
            const reason = typeof body === 'string' ? body : body.error || body.result || 'Ошибка устройства';
            throw Error(`Устройство: ${reason} (${reply.status}).`);
          }
          return body;
        }
        if (reply.next !== offset || !chunk.length) throw Error('Некорректная пагинация USB.');
      } while (offset < total);
    }
    throw Error('Данные устройства меняются во время чтения. Повторите проверку.');
  }

  async identity() {
    // /api/device begins with these immutable identity fields. HUD counters in
    // later pages are live; reading this bounded prefix avoids assembling them
    // across different revisions. Unknown firmware layout fails closed.
    const reply = await this.exchange({method:'GET', path:'/api/device', boot:this.boot, sid:String(this.sid), page:'0'});
    if (reply.status !== 200 || reply.boot !== this.boot || reply.sid !== this.sid || reply.offset !== 0 || reply.type !== 'application/json') throw Error('Не удалось проверить ID устройства.');
    const prefix = new TextDecoder('utf-8', {fatal:true}).decode(Uint8Array.from(atob(reply.base64), (c) => c.charCodeAt(0)));
    const fields = /^\{"ok":true,"deviceRole":"(controller|hud)","deviceId":"([0-9a-f]{12})"[,}]/.exec(prefix);
    if (!fields || fields[2] === '000000000000') throw Error('Прошивка не вернула постоянный ID устройства.');
    return {deviceRole:fields[1], deviceId:fields[2]};
  }

  async close() {
    this.closed = true;
    try {await this.reader?.cancel();} catch { /* disconnected */ }
    await this.reading;
    try {await this.port.close();} catch { /* already disconnected */ }
  }
}
