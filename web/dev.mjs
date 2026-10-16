import { spawn } from 'node:child_process';

const args = process.argv.slice(2);
const lan = args.includes('--lan');
const viteArgs = args.filter((arg) => arg !== '--lan');

const child = spawn('vite', viteArgs, {
    stdio: 'inherit',
    shell: true,
    env: {
        ...process.env,
        ...(lan ? { KATARA_LAN: '1' } : {}),
    },
});

child.on('exit', (code, signal) => {
    if (signal) {
        process.kill(process.pid, signal);
        return;
    }
    process.exit(code ?? 1);
});
