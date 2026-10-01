import { StrictMode } from 'react'
import ReactDOM from 'react-dom/client'
import { RouterProvider, createRouter, createHashHistory } from '@tanstack/react-router'

// i18n must be imported before any component that uses useTranslation()
import './i18n'

// STUDIO-17977 dev-only mock bridge. Vite tree-shakes the import in
// production via `if (import.meta.env.DEV)` constant folding; the mock
// also no-ops when a real webview host is detected. Import is *static*
// (top-level) so the bridge is installed before React renders and the
// first init() request - if dynamic, the request races ahead of mock
// install and falls into the iframe-fallback path that never resolves.
import { installMockBridge } from './dev/mockBridge'

if (import.meta.env.DEV) {
  installMockBridge()
}

// Import the generated route tree
import { routeTree } from './routeTree.gen'
import WindowResizeListener from './utils/WindowResizeListener'

// Create a new router instance
const router = createRouter({ routeTree, history: createHashHistory() })

// Register the router instance for type safety
declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router
  }
}

// Render the app
const rootElement = document.getElementById('root')!
if (!rootElement.innerHTML) {
  const root = ReactDOM.createRoot(rootElement)
  root.render(
    <StrictMode>
      <WindowResizeListener />
      <RouterProvider router={router} />
    </StrictMode>,
  )
}
