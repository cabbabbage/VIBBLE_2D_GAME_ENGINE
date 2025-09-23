#!/usr/bin/env node
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const cliArgs = process.argv.slice(2);

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
  if (cliArgs.includes('--ci')) {
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

if (toBoolean(process.env.GITHUB_ACTIONS)) {
  console.log('[ctest] Skipping ctest invocation on GitHub Actions CI environment.');
  process.exit(0);
}

if (process.platform !== 'win32') {
  const message = `[ctest] Skipping Windows-only ctest helper on ${process.platform}.`;
  if (isCiLike) {
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

const ctestArgs = ['--build-config', 'RelWithDebInfo', '--output-on-failure'];
const child = spawn('ctest', ctestArgs, {
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
