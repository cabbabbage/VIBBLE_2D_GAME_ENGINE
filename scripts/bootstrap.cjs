#!/usr/bin/env node
const { spawn } = require('child_process');
const path = require('path');

if (process.platform !== 'win32') {
  console.error('[bootstrap] This project must be bootstrapped from Windows.');
  process.exit(1);
}

const scriptPath = path.resolve(__dirname, '..', 'run.bat');
const args = process.argv.slice(2);

const command = [
  `"${scriptPath}"`,
  ...args.map((arg) => {
    if (/\s|"/.test(arg)) {
      return `"${arg.replace(/"/g, '\\"')}"`;
    }
    return arg;
  })
].join(' ');

const child = spawn(command, {
  stdio: 'inherit',
  shell: true,
  windowsVerbatimArguments: false
});

child.on('error', (error) => {
  console.error(`[bootstrap] Failed to launch run.bat: ${error.message}`);
  process.exit(1);
});

child.on('exit', (code) => {
  process.exit(code === undefined ? 1 : code);
});
