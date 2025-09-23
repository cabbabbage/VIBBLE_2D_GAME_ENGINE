#!/usr/bin/env node
const { spawn } = require('child_process');
const path = require('path');

const args = process.argv.slice(2);

const toBoolean = (value) => {
  if (typeof value === 'boolean') {
    return value;
  }

  if (typeof value === 'number') {
    return value !== 0;
  }

  if (typeof value === 'string') {
    const normalized = value.trim().toLowerCase();
    return normalized === 'true' || normalized === '1' || normalized === 'yes';
  }

  return false;
};

const isCiLike = (() => {
  if (args.includes('--ci')) {
    return true;
  }

  const ciEnvKeys = [
    'CI',
    'CONTINUOUS_INTEGRATION',
    'BUILD_ID',
    'BUILD_NUMBER',
    'RUN_ID',
    'GITHUB_ACTIONS'
  ];

  return ciEnvKeys.some((key) => toBoolean(process.env[key]));
})();

if (process.platform !== 'win32') {
  const reason = `[bootstrap] Skipping Windows-specific bootstrap on ${process.platform}.`;
  if (isCiLike) {
    console.log(reason);
    process.exit(0);
  }

  console.error(`${reason} Run this command from Windows to configure the project.`);
  process.exit(1);
}

const scriptPath = path.resolve(__dirname, '..', 'run.bat');

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
