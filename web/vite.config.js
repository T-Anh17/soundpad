import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// base './': bản build mở được ở bất kỳ thư mục nào (GitHub Pages, host tĩnh…)
export default defineConfig({ plugins: [react()], base: './' });
