#!/usr/bin/env node
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

if (process.platform !== 'win32') {
  const message = `[ctest] Skipping Windows-only ctest helper on ${process.platform}.`;
  if (process.env.CI === 'true' || process.env.CI === '1') {
    console.log(message);
    process.exit(0);
  }

  console.error(`${message} Run this command from Windows to exercise CTest.`);
  process.exit(1);
}

const buildDir = path.resolve(__dirname, '..', 'build');
if (!fs.existsSync(buildDir)) {
  console.error('[ctest] Build directory not found. Run the bootstrap step first.');
  process.exit(1);
}

const args = ['--build-config', 'RelWithDebInfo', '--output-on-failure'];
const child = spawn('ctest', args, {
  cwd: buildDir,
  stdio: 'inherit',
  shell: true
});

child.on('error', (error) => {
  console.error(`[ctest] Failed to launch ctest: ${error.message}`);
  process.exit(1);
});

child.on('exit', (code) => {
  process.exit(code === undefined ? 1 : code);
});
