#!/usr/bin/env node
/**
 * Convenience wrapper around run.bat so npm users can bootstrap the project.
 */
const { spawnSync } = require('child_process');
const path = require('path');

if (process.platform !== 'win32') {
  console.error('The bootstrap helper only works on Windows because it relies on run.bat.');
  process.exit(1);
}

const repoRoot = path.resolve(__dirname, '..');
const passThroughArgs = [];
let launch = false;
let shortcutMode = 'auto'; // auto => launch keeps shortcut, bootstrap skips

for (let i = 2; i < process.argv.length; i += 1) {
  const arg = process.argv[i];
  if (arg === '--launch') {
    launch = true;
  } else if (arg === '--keep-shortcut') {
    shortcutMode = 'keep';
  } else if (arg === '--no-shortcut') {
    shortcutMode = 'skip';
  } else {
    passThroughArgs.push(arg);
  }
}

const runArgs = ['run.bat'];
if (!launch) {
  runArgs.push('--skip-run');
}
if (shortcutMode === 'skip' || (shortcutMode === 'auto' && !launch)) {
  runArgs.push('--no-shortcut');
}
runArgs.push(...passThroughArgs);

const result = spawnSync('cmd.exe', ['/d', '/c', ...runArgs], {
  cwd: repoRoot,
  stdio: 'inherit',
  windowsVerbatimArguments: true,
});

if (result.error) {
  console.error(result.error.message);
  process.exit(typeof result.status === 'number' ? result.status : 1);
}

process.exit(typeof result.status === 'number' ? result.status : 0);
