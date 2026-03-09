import { defineConfig } from 'vite'

export default defineConfig({
  root: '.',
  base: './',       // 使用相对路径，兼容 file:/// 加载
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    // 单文件输出，方便内联资源脚本打包
    rollupOptions: {
      output: {
        manualChunks: undefined,
        entryFileNames: 'assets/[name].js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: 'assets/[name].[ext]',
      },
    },
  },
  server: {
    port: 5173,
    strictPort: true,
  },
})
