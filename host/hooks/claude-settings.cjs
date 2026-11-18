const { join } = require('node:path');

const quote = value => "'" + value.replaceAll("'", "'\\''") + "'";
const command = [process.execPath, join(__dirname, 'claude-code.cjs')].map(quote).join(' ');
const events = ['SessionStart', 'UserPromptSubmit', 'Stop', 'StopFailure', 'SessionEnd'];
const hooks = Object.fromEntries(events.map(event => [event, [{ hooks: [{ type: 'command', command, timeout: 3 }] }]]));
process.stdout.write(JSON.stringify({ hooks }, null, 2) + '\n');
