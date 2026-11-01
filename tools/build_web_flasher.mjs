import {cp, readFile, writeFile} from 'node:fs/promises';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const at = args.indexOf('--output');
const output = at === -1 ? path.join(root, 'dist/web-flasher') : path.resolve(args[at + 1]);
const result = spawnSync(process.env.PYTHON || 'python3', ['tools/web_flasher.py', 'assemble', ...args], {cwd: root, stdio: 'inherit'});
if (result.status !== 0) process.exit(result.status || 1);
// Use the publisher's complete browser distribution, including chip loaders
// and serial stubs. No CDN or npm resolution is needed in the user's browser.
await cp(path.join(root, 'node_modules/esp-web-tools/dist/web'), path.join(output, 'vendor'), {recursive: true});
// Retain attribution and the complete licenses in every published site.
const licenses = [];
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
