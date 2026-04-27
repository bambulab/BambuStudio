import path from 'path'
import { defineConfig, type Plugin } from 'vite'
import { TanStackRouterVite } from '@tanstack/router-plugin/vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

/**
 * Make Vite build output compatible with file:// protocol:
 * 1. Move <script> tags from <head> to end of <body> (DOM must be ready for React)
 * 2. Strip crossorigin attribute from all tags (blocked under file://)
 * 3. Strip type="module" from scripts (IIFE bundles don't need it)
 */
function fileProtocolCompat(): Plugin {
  return {
    name: 'file-protocol-compat',
    enforce: 'post',
    transformIndexHtml(html) {
      // 1) Strip crossorigin from all tags (link, script, etc.)
      let result = html.replace(/ crossorigin/g, '')

      // 2) Move <script> from <head> to end of <body>
      const headEnd = result.indexOf('</head>')
      const scriptRe = /<script\b[^>]*src="[^"]*"[^>]*><\/script>/g
      const scripts: string[] = []
      result = result.replace(scriptRe, (match, offset) => {
        if (offset < headEnd) {
          scripts.push(match)
          return ''
        }
        return match
      })

      if (scripts.length > 0) {
        // 3) Strip type="module" for IIFE bundles
        const cleaned = scripts.map(s => s.replace(/ type="module"/, ''))
        result = result.replace('</body>', '    ' + cleaned.join('\n    ') + '\n  </body>')
      }

      return result
    }
  }
}

// https://vite.dev/config/
export default defineConfig({
  base: './',
  plugins: [
    TanStackRouterVite({
      target: 'react',

      // IMPORTANT: false only for development debugging!!!! 【true for production】
      autoCodeSplitting: false
    }),
    react(),
    tailwindcss(),
    fileProtocolCompat(),
  ],

  resolve: {
    alias: {
      '@locales': path.resolve(__dirname, 'locales'),
    },
  },

  build: {
    rollupOptions: {
      output: {
        // replace hash with real name
        entryFileNames: 'assets/[name].js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: 'assets/[name].[ext]'
      }
    },
    // Use terser instead of esbuild so we can reserve the name "wx".
    // WKWebView's AddScriptMessageHandler("wx") injects `window.wx` as a
    // UserMessageHandler instance. If the minifier generates a top-level
    // variable called "wx", it shadows the global and causes:
    // "wx is not a function (it is an instance of UserMessageHandler)".
    minify: 'terser',
    terserOptions: {
      mangle: {
        reserved: ['wx'],
      },
    },
  }
})
