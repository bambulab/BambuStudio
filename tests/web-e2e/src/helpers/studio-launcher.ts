/**
 * BambuStudio process launcher.
 *
 * Spawns bambu-studio.exe with WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS set so
 * the embedded WebView2 listens on a debug port.  Exposes start/stop and
 * "is the CDP port live yet?" helpers; nothing in this file talks to
 * Playwright - it only owns the process and the port.
 */
import { spawn, type ChildProcess } from 'node:child_process'
import * as net from 'node:net'
import * as path from 'node:path'
import * as fs from 'node:fs'

export interface LaunchOptions {
  /** Absolute path to bambu-studio.exe. */
  bin: string
  /** Chrome DevTools Protocol port for the embedded WebView2. */
  cdpPort: number
  /** Extra args appended to --remote-debugging-port=N. */
  extraWebView2Args?: string
  /** How long to wait for the CDP port to come alive, ms. */
  bootTimeoutMs?: number
  /** Poll interval while waiting for the CDP port, ms. */
  pollIntervalMs?: number
  /** Stream stdout/stderr to this writable; defaults to ignore. */
  stdio?: 'inherit' | 'ignore'
}

export interface StudioHandle {
  proc: ChildProcess
  pid: number
  cdpPort: number
  /** Resolves once the CDP port is reachable. */
  waitForCdp: () => Promise<void>
  /** Kills the process and any descendants on Windows. */
  dispose: () => Promise<void>
}

export class StudioLaunchError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'StudioLaunchError'
  }
}

function isPortOpen(port: number, host = '127.0.0.1', timeoutMs = 400): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = new net.Socket()
    let settled = false
    const finish = (result: boolean) => {
      if (settled) return
      settled = true
      socket.destroy()
      resolve(result)
    }
    socket.setTimeout(timeoutMs)
    socket.once('connect', () => finish(true))
    socket.once('timeout', () => finish(false))
    socket.once('error', () => finish(false))
    socket.connect(port, host)
  })
}

async function waitForPort(
  port: number,
  timeoutMs: number,
  pollMs: number,
): Promise<void> {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    if (await isPortOpen(port)) return
    await new Promise((r) => setTimeout(r, pollMs))
  }
  throw new StudioLaunchError(
    `WebView2 CDP port ${port} did not open within ${timeoutMs} ms. ` +
      `Either BambuStudio failed to start or it is not honoring ` +
      `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS.  Run scripts/check-cdp.ps1 to diagnose.`,
  )
}

export async function launchStudio(opts: LaunchOptions): Promise<StudioHandle> {
  const {
    bin,
    cdpPort,
    extraWebView2Args = '--remote-allow-origins=*',
    bootTimeoutMs = 60_000,
    pollIntervalMs = 500,
    stdio = 'ignore',
  } = opts

  if (!fs.existsSync(bin)) {
    throw new StudioLaunchError(`bambu-studio.exe not found: ${bin}`)
  }
  if (await isPortOpen(cdpPort)) {
    throw new StudioLaunchError(
      `CDP port ${cdpPort} is already in use before BambuStudio starts. ` +
        `Pick a free port via STUDIO_E2E_CDP_PORT or kill the conflicting process.`,
    )
  }

  const env = {
    ...process.env,
    WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:
      `--remote-debugging-port=${cdpPort} ${extraWebView2Args}`.trim(),
  }

  const proc = spawn(bin, [], {
    env,
    cwd: path.dirname(bin),
    detached: false,
    stdio,
    windowsHide: false,
  })

  if (!proc.pid) {
    throw new StudioLaunchError('Failed to obtain a PID for the spawned BambuStudio process.')
  }

  const handle: StudioHandle = {
    proc,
    pid: proc.pid,
    cdpPort,
    waitForCdp: () => waitForPort(cdpPort, bootTimeoutMs, pollIntervalMs),
    dispose: async () => {
      if (proc.exitCode !== null || proc.killed) return
      // taskkill cleans up the child window/server processes too.
      const { spawnSync } = await import('node:child_process')
      spawnSync('taskkill', ['/PID', String(proc.pid), '/T', '/F'], { stdio: 'ignore' })
    },
  }
  return handle
}

/**
 * "Attach mode": the developer launches Studio manually (e.g. through the
 * interactive CLI) but the env var has already been exported.  We don't own
 * the process - we just wait for the port to come alive.
 */
export function attachExisting(opts: { cdpPort: number; bootTimeoutMs?: number; pollIntervalMs?: number }): {
  cdpPort: number
  waitForCdp: () => Promise<void>
} {
  const { cdpPort, bootTimeoutMs = 120_000, pollIntervalMs = 500 } = opts
  return {
    cdpPort,
    waitForCdp: () => waitForPort(cdpPort, bootTimeoutMs, pollIntervalMs),
  }
}
