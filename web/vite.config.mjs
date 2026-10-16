import { defineConfig } from 'vite';
import basicSsl from '@vitejs/plugin-basic-ssl';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const lan = process.env.KATARA_LAN === '1';

export default defineConfig({
    plugins: lan ? [basicSsl()] : [],
    root: resolve(__dirname, 'public'),
    base: './',
    resolve: {
        alias: {
            '@katara-config': resolve(__dirname, 'config.json'),
        },
    },
    build: {
        outDir: resolve(__dirname, 'public/dist'),
        emptyDir: true,
        rollupOptions: {
            input: resolve(__dirname, 'public/index.html'),
        },
    },
    server: {
        host: lan,
        https: lan,
        port: 8000,
        strictPort: true,
        fs: {
            allow: [resolve(__dirname)],
        },
        hmr: {
            protocol: lan ? 'wss' : 'ws',
        }
    },
});
