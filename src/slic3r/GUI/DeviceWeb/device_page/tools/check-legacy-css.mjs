// Reports shipped CSS that would break on the oldest engines the installer
// reaches: macOS 10.15 WKWebView (Safari 13.1) and WebView2 109 on Windows 8.1.
// Runs after `vite build` on the real bundle, not on a sample, because the
// regressions it guards against come from what Tailwind emits for the classes
// actually in use.
//
// Warns by default so a false positive cannot block everyone's build; set
// CSS_COMPAT_STRICT=1 (e.g. on CI or release builds) to fail instead.
//
// Usage: node tools/check-legacy-css.mjs [dist/assets/index.js]
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import vm from 'node:vm'
import postcss from 'postcss'

const bundlePath = process.argv[2] ?? fileURLToPath(new URL('../dist/assets/index.js', import.meta.url))
const strict = /^(1|true|on)$/i.test(process.env.CSS_COMPAT_STRICT ?? '')

// The IIFE build inlines all CSS as one `<style>` textContent at the top of
// the bundle. Running the bundle against a stub DOM yields that exact string,
// already through PostCSS and lightningcss, without parsing JS.
function extractInjectedCss(code) {
  const styles = []
  const stop = new Error('stop')
  const createElement = (tag) => {
    const el = { tagName: tag, style: {}, setAttribute() {}, appendChild() {}, addEventListener() {} }
    if (tag === 'style') Object.defineProperty(el, 'textContent', { set: (v) => { styles.push(v) } })
    return el
  }
  const document = {
    createElement,
    head: { appendChild: () => { throw stop } },
    body: { appendChild() {} },
    documentElement: { dataset: {} },
    addEventListener() {},
  }
  try {
    vm.runInNewContext(code, { document, window: {}, navigator: { userAgent: '' } }, { timeout: 5000 })
  } catch (e) {
    if (e !== stop) throw e
  }
  return styles.join('\n')
}

// Unsupported colour syntax: Chromium < 111 and Safari < 15.4 drop these.
const MODERN_COLOR = /\b(?:oklch|oklab|lab|lch)\(|color-mix\(/
const LOGICAL_SHORTHAND = /^(?:inset|padding|margin)-(?:inline|block)$/
const INDIVIDUAL_TRANSFORM = /^(?:translate|rotate|scale)$/
const IS_WHERE = /:(?:is|where)\(/

const normSelector = (s) => s.replace(/\s+/g, '').replace(/=(["'])([^"']*)\1/g, '=$2')

function gatedBy(node, test) {
  for (let p = node.parent; p; p = p.parent) if (p.type === 'atrule' && p.name === 'supports' && test(p.params)) return true
  return false
}

// legacy-webkit inserts each fallback as an @supports block right after the
// rule, one per feature, so a rule using several features is followed by a
// run of them. Yields that run.
function* followingSupports(rule) {
  for (let n = rule.next(); n?.type === 'atrule' && n.name === 'supports'; n = n.next()) yield n
}

const problems = []
const report = (node, why) => {
  const where = node.type === 'decl' ? `${node.parent?.selector ?? '?'} { ${node.prop}: ${node.value} }` : (node.selector ?? `@${node.name} ${node.params}`)
  problems.push(`${why}\n      ${where.slice(0, 160)}`)
}

const root = postcss.parse(extractInjectedCss(readFileSync(bundlePath, 'utf8')))
if (!root.nodes.length) {
  console.error(`check-legacy-css: no CSS found in ${bundlePath}`)
  process.exit(1)
}

// Safari < 15.4 discards the whole block, taking Tailwind with it.
root.walkAtRules('layer', (a) => report(a, '@layer is dropped as a block by Safari < 15.4'))

root.walkRules((rule) => {
  // Safari < 14 drops the whole rule; legacy-webkit adds a :matches() copy right after it.
  if (IS_WHERE.test(rule.selector) && !gatedBy(rule, (p) => /text-orientation/.test(p))) {
    // lightningcss may merge neighbouring copies into one @supports block, and
    // it minifies the original selector but not the :matches() copy it does
    // not understand, so compare normalised selectors anywhere in that block.
    const want = normSelector(rule.selector.replace(/:(?:is|where)\(/g, ':matches('))
    const copied = [...followingSupports(rule)].some((s) => s.some((n) => n.type === 'rule' && normSelector(n.selector) === want))
    if (!copied) report(rule, ':is()/:where() without a :matches() copy (Safari 13.1 drops the rule)')
  }
})

root.walkDecls((d) => {
  // Confirmed on macOS 11.7: `inset-inline: var(..)` resolves the end side to
  // auto and overrides any physical fallback. legacy-webkit must replace these.
  if (LOGICAL_SHORTHAND.test(d.prop)) report(d, `${d.prop} is unsafe on old WebKit; must be rewritten to physical sides`)

  // Safari < 14.1 ignores these; legacy-webkit composes a gated transform.
  if (INDIVIDUAL_TRANSFORM.test(d.prop) && d.value !== 'none' && !gatedBy(d, () => true)) {
    const ok = [...followingSupports(d.parent)].some((s) => /translate/.test(s.params))
    if (!ok) report(d, `${d.prop} without a transform fallback (Safari < 14.1)`)
  }

  // A modern colour needs an sRGB value for the same property right before it,
  // or must sit inside an @supports that tests for it.
  if (MODERN_COLOR.test(d.value) && !gatedBy(d, (p) => MODERN_COLOR.test(p) || /\b(?:lab|oklch|color-mix)\b/.test(p))) {
    const prev = d.prev()
    const hasFallback = prev?.type === 'decl' && prev.prop === d.prop && !MODERN_COLOR.test(prev.value)
    if (!hasFallback) report(d, 'modern colour without an sRGB fallback (WebView2 109 / Safari < 15.4 drop it)')
  }
})

if (problems.length) {
  const label = strict ? '' : 'WARNING '
  console.error(`\ncheck-legacy-css: ${label}${problems.length} problem(s) in ${bundlePath}\n`)
  for (const p of problems.slice(0, 40)) console.error('  - ' + p)
  if (problems.length > 40) console.error(`  ... and ${problems.length - 40} more`)
  console.error('\nThese break the device pages on macOS 10.15/11 WKWebView or Windows 8.1 WebView2.')
  console.error('Fix the source CSS or extend legacy-webkit.postcss.ts, then rebuild.\n')
  if (strict) process.exit(1)
  console.error('Not failing the build; set CSS_COMPAT_STRICT=1 to make this an error.\n')
} else {
  console.log(`check-legacy-css: OK (${Math.round(root.toString().length / 1024)} KB of CSS checked)`)
}
