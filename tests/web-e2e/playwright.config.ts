import { defineConfig, devices } from '@playwright/test'
import * as dotenv from 'dotenv'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

// Load .env.local first (developer-specific overrides), then fall back to
// .env.example for shared defaults. Existing process.env values win.
dotenv.config({ path: path.join(__dirname, '.env.local') })
dotenv.config({ path: path.join(__dirname, '.env.example') })

/**
 * BambuStudio filament manager E2E configuration.
 *
 * The framework attaches Playwright to a developer-launched BambuStudio
 * process via Chrome DevTools Protocol exposed by the embedded WebView2.
 * Tests therefore run serially on a single worker against a shared session;
 * see src/fixtures/studio.ts for the attach mechanics.  The CDP endpoint is
 * read directly from process.env (STUDIO_E2E_CDP_PORT / STUDIO_E2E_CDP_ENDPOINT)
 * because worker-scoped fixtures cannot consume per-test `use` options.
 */
export default defineConfig({
  testDir: './tests',
  outputDir: './test-results',

  // CDP attaches to a single live WebView2 instance — never parallelize.
  fullyParallel: false,
  workers: 1,

  // Local-only suite: no CI guard, but a single retry absorbs flake.
  retries: process.env.CI ? 0 : 1,

  // Generous defaults for a real desktop process; individual specs can
  // shorten via test.setTimeout if needed.
  timeout: 60_000,
  expect: { timeout: 7_000 },

  reporter: [
    ['list'],
    ['html', { outputFolder: 'reports/html', open: 'never' }],
    ['json', { outputFile: 'reports/results.json' }],
  ],

  use: {
    actionTimeout: 7_000,
    navigationTimeout: 15_000,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'retain-on-failure',
    ...devices['Desktop Chrome'],
  },

  // Tag-based projects let `pnpm e2e:webview-only` skip anything that needs
  // cloud reachability or a real printer.  Each project filters by a tag
  // expression: a spec inherits a project when its `test()` title (or any
  // `test.describe()` along its chain) carries the matching tag.
  projects: [
    { name: 'webview-only', grep: /@webview-only/ },
    { name: 'cloud', grep: /@cloud/ },
    { name: 'printer', grep: /@printer/ },
    { name: 'all' },
  ],
})
