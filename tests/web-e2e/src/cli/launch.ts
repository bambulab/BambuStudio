#!/usr/bin/env node
/**
 * Interactive launcher.  Runs the developer through the four-step flow:
 *
 *   1. confirm bambu-studio.exe path
 *   2. start it under WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS (or attach to
 *      a developer-launched instance)
 *   3. wait for the developer to log in + open the filament manager
 *   4. attach Playwright via CDP, run the chosen test suite, render report
 *
 * Designed to be the "front door" for the framework so a developer never
 * has to remember exact env-var names or Playwright flags.  Power users
 * can still bypass it by running `pnpm e2e:run -- --grep ...` directly.
 */
import { confirm, select, input as askInput } from '@inquirer/prompts'
import boxen from 'boxen'
import chalk from 'chalk'
import * as dotenv from 'dotenv'
import { spawn } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'
import { attachExisting, launchStudio, type StudioHandle } from '../helpers/studio-launcher.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const E2E_ROOT = path.resolve(__dirname, '..', '..')

dotenv.config({ path: path.join(E2E_ROOT, '.env.local') })
dotenv.config({ path: path.join(E2E_ROOT, '.env.example') })

interface ResolvedConfig {
  studioBin: string
  cdpPort: number
  openReport: boolean
}

function resolveConfig(): ResolvedConfig {
  const cdpPort = Number(process.env.STUDIO_E2E_CDP_PORT ?? 9222)
  const studioBin =
    process.env.STUDIO_E2E_BIN ??
    path.resolve(E2E_ROOT, '..', '..', 'build_release', 'src', 'Release', 'bambu-studio.exe')
  const openReport = process.env.STUDIO_E2E_OPEN_REPORT !== '0'
  return { studioBin, cdpPort, openReport }
}

function banner(text: string, kind: 'info' | 'ok' | 'warn' | 'err' = 'info'): void {
  const color =
    kind === 'ok' ? 'green' : kind === 'warn' ? 'yellow' : kind === 'err' ? 'red' : 'cyan'
  process.stdout.write(
    boxen(text, {
      padding: 1,
      borderStyle: 'round',
      borderColor: color,
      margin: { top: 1, bottom: 0 },
    }) + '\n',
  )
}

async function step1_confirmBin(cfg: ResolvedConfig): Promise<string> {
  banner('Step 1/4 \u00b7 Locate bambu-studio.exe')
  if (!fs.existsSync(cfg.studioBin)) {
    process.stderr.write(chalk.red(`  bambu-studio.exe not found at: ${cfg.studioBin}\n`))
    const newPath = await askInput({
      message: 'Enter the full path to bambu-studio.exe:',
      validate: (v) => fs.existsSync(v) || 'Path does not exist',
    })
    return newPath
  }
  process.stdout.write(`  ${chalk.green('found')}  ${cfg.studioBin}\n`)
  return cfg.studioBin
}

async function step2_startOrAttach(
  studioBin: string,
  cdpPort: number,
): Promise<StudioHandle | { dispose: () => Promise<void>; waitForCdp: () => Promise<void> }> {
  banner('Step 2/4 \u00b7 Start BambuStudio (or attach to a running one)')
  const mode = await select({
    message: 'How should we get the BambuStudio process?',
    choices: [
      { name: 'Spawn it for me (recommended)', value: 'spawn' },
      { name: 'I already started it with the env var; just attach', value: 'attach' },
    ],
    default: 'spawn',
  })
  if (mode === 'attach') {
    process.stdout.write(
      chalk.yellow(
        `  Attaching to an existing BambuStudio on CDP port ${cdpPort}.\n` +
          `  If you have not started it yet, do so now from a shell that has\n` +
          `  WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=${cdpPort} exported.\n`,
      ),
    )
    return { ...attachExisting({ cdpPort }), dispose: async () => undefined }
  }
  process.stdout.write(chalk.gray('  Spawning BambuStudio...\n'))
  const handle = await launchStudio({ bin: studioBin, cdpPort })
  process.stdout.write(chalk.gray(`  PID = ${handle.pid}\n`))
  return handle
}

async function step3_waitForReady(): Promise<void> {
  banner(
    'Step 3/4 \u00b7 Drive BambuStudio (manual)\n\n' +
      '  1. Log in (your dev account)\n' +
      '  2. Top menu \u2192 Device \u2192 Filament Manager\n' +
      '  3. Wait for the spool list to appear',
  )
  await confirm({ message: 'Ready when you are. Continue?', default: true })
}

async function step4_pickSuiteAndRun(cfg: ResolvedConfig): Promise<number> {
  banner('Step 4/4 \u00b7 Pick a suite to run')
  const suite = await select({
    message: 'Which suite?',
    choices: [
      { name: 'STUDIO-17977 only        (~2 min, @webview-only)', value: 'studio-17977' },
      { name: '@smoke (fastest sanity)  (~3 min)', value: 'smoke' },
      { name: '@webview-only project    (no cloud / no printer)', value: 'webview-only' },
      { name: 'all                       (everything)', value: 'all' },
      { name: 'custom grep...', value: 'custom' },
    ],
    default: 'studio-17977',
  })

  let args: string[] = []
  if (suite === 'studio-17977') {
    args = ['test', '--grep', '@studio-17977']
  } else if (suite === 'smoke') {
    args = ['test', '--grep', '@smoke']
  } else if (suite === 'webview-only') {
    args = ['test', '--project=webview-only']
  } else if (suite === 'all') {
    args = ['test']
  } else {
    const grep = await askInput({ message: 'Grep expression:' })
    args = ['test', '--grep', grep]
  }
  return runPlaywright(args, cfg)
}

function runPlaywright(args: string[], cfg: ResolvedConfig): Promise<number> {
  return new Promise((resolve) => {
    const env = {
      ...process.env,
      STUDIO_E2E_CDP_PORT: String(cfg.cdpPort),
    }
    const isWindows = process.platform === 'win32'
    const cmd = isWindows ? 'pnpm.cmd' : 'pnpm'
    const child = spawn(cmd, ['exec', 'playwright', ...args], {
      cwd: E2E_ROOT,
      env,
      stdio: 'inherit',
      shell: false,
    })
    child.on('exit', (code) => resolve(code ?? 1))
  })
}

async function main(): Promise<void> {
  const cfg = resolveConfig()
  banner(
    `BambuStudio Filament Manager \u00b7 E2E launcher\n\n` +
      `  studio bin : ${cfg.studioBin}\n` +
      `  cdp port   : ${cfg.cdpPort}`,
  )

  const studioBin = await step1_confirmBin({ ...cfg, studioBin: cfg.studioBin })
  const session = await step2_startOrAttach(studioBin, cfg.cdpPort)
  try {
    await step3_waitForReady()
    process.stdout.write(chalk.gray('  Waiting for CDP port to come alive...\n'))
    await session.waitForCdp()
    process.stdout.write(chalk.green('  CDP reachable. Handing off to Playwright.\n'))
    const code = await step4_pickSuiteAndRun(cfg)
    if (cfg.openReport) {
      process.stdout.write(chalk.gray('\n  Opening HTML report...\n'))
      spawn('pnpm', ['exec', 'playwright', 'show-report', 'reports/html'], {
        cwd: E2E_ROOT,
        stdio: 'inherit',
        shell: process.platform === 'win32',
      })
    }
    process.exit(code)
  } finally {
    if ('dispose' in session && typeof session.dispose === 'function') {
      await session.dispose().catch(() => undefined)
    }
  }
}

main().catch((err: Error) => {
  process.stderr.write(chalk.red(`\n[fatal] ${err.message}\n`))
  if (err.stack) process.stderr.write(chalk.gray(err.stack) + '\n')
  process.exit(1)
})
