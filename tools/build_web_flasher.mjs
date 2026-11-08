import {cp, readFile, writeFile, readdir} from 'node:fs/promises';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const at = args.indexOf('--output');
const output = at === -1 ? path.join(root, 'dist/web-flasher') : path.resolve(args[at + 1]);
const result = spawnSync(process.env.PYTHON || 'python3', ['tools/web_flasher.py', 'assemble', ...args], {cwd: root, stdio: 'inherit'});
if (result.status !== 0) process.exit(result.status || 1);
const docs = spawnSync(process.env.PYTHON || 'python3', ['tools/build_web_flasher_docs.py', '--output', output], {cwd:root, stdio:'inherit'});
if (docs.status !== 0) process.exit(docs.status || 1);
// Use the publisher's complete browser distribution, including chip loaders
// and serial stubs. No CDN or npm resolution is needed in the user's browser.
await cp(path.join(root, 'node_modules/esp-web-tools/dist/web'), path.join(output, 'vendor'), {recursive: true});
// ESP Web Tools 10.4.0 has no public flash completion event. Bridge its
// actual flash callback, keeping error/cancel distinct from successful writing.
// Pin the exact upstream fragment: a dependency change must fail the build.
const dialogFiles = (await readdir(path.join(output, 'vendor'))).filter((name) => /^install-dialog-.*\.js$/.test(name));
if (dialogFiles.length !== 1) throw new Error('Unexpected ESP Web Tools dialog distribution');
const dialogFile = path.join(output, 'vendor', dialogFiles[0]);
const dialog = await readFile(dialogFile, 'utf8');
const callback = 'this._installState=e,"finished"===e.state?';
if (dialog.split(callback).length !== 2) throw new Error('ESP Web Tools flash callback changed');
await writeFile(dialogFile, dialog.replace(callback,
  'this._installState=e,this.dispatchEvent(new CustomEvent("g5500-flash-state",{detail:e,bubbles:true,composed:true})),"finished"===e.state?'));
// Retain attribution and the complete licenses in every published site.
const licenses = ['Local modification to ESP Web Tools 10.4.0: dispatch g5500-flash-state from the original flash callback for the post-install connection guide.'];
for (const name of ['esp-web-tools', 'esptool-js', 'lit', 'lit-element', 'lit-html', '@lit/reactive-element', '@material/web', 'tslib', 'improv-wifi-serial-sdk', 'pako', 'atob-lite']) {
  const folder = path.join(root, 'node_modules', name);
  const pkg = JSON.parse(await readFile(path.join(folder, 'package.json'), 'utf8'));
  let license = '';
  for (const filename of ['LICENSE', 'LICENSE.txt', 'LICENSE.md', 'CopyrightNotice.txt']) {
    try {license = await readFile(path.join(folder, filename), 'utf8'); break;} catch {}
  }
  licenses.push(`${name} ${pkg.version} (${pkg.license})\n${license || pkg.homepage || ''}`);
}
const photos = JSON.parse(await readFile(path.join(root, 'web-flasher/photos.json'), 'utf8'));
licenses.push('Board photographs: copyright remains with the respective authors. Sources and attribution:\n' +
  Object.values(photos).map((photo) => `${photo.file}: ${photo.caption}\n${photo.source}\nOriginal: ${photo.original}`).join('\n\n'));
await writeFile(path.join(output, 'THIRD_PARTY_LICENSES.txt'), licenses.join('\n\n--------------------\n\n'));
console.log(`Static installer: ${output}`);
