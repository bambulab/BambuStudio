// Copies the self-contained web app (ui-md3/) into ./app so electron-builder can package it.
// Mirrors ui-md3's layout (index.html + runtime/ + app/) so the app's relative paths resolve.
const fs = require('fs');
const path = require('path');

const SRC = path.resolve(__dirname, '..');      // ui-md3/
const OUT = path.resolve(__dirname, 'app');     // ui-md3/desktop/app/  (packaged web root)

// Ship these; skip design-source, *.md, and the desktop wrapper itself.
const INCLUDE = ['index.html', 'landing.html', 'runtime', 'app'];

fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });

for (const item of INCLUDE) {
  const from = path.join(SRC, item);
  if (fs.existsSync(from)) {
    fs.cpSync(from, path.join(OUT, item), { recursive: true });
    console.log('  +', item);
  } else {
    console.warn('  ! missing (skipped):', item);
  }
}
console.log('prepared', OUT, 'from', SRC);
