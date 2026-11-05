import type { AtRule, Container, Plugin, Rule } from 'postcss'

// Downlevels CSS for old macOS WKWebView, which is pinned to the OS Safari
// (13.1 on 10.15, 14.0 on 11.0-11.2) and cannot be upgraded on its own.
// Logical shorthands are rewritten to their physical sides. Everything else
// gets copies inserted right after the original rule inside a feature query
// that is false on Safari >= 14.1 and Chromium, so modern engines keep the
// original cascade.

// translate/rotate/scale, flex gap and logical shorthands shipped in 14.1.
const PRE_14_1 = 'not (translate: 0)'
// :is()/:where() shipped in 14.0, the same release as text-orientation.
const PRE_14 = 'not (text-orientation: upright)'

const LOGICAL: Record<string, readonly [string, string]> = {
  'padding-inline': ['padding-left', 'padding-right'],
  'padding-block': ['padding-top', 'padding-bottom'],
  'margin-inline': ['margin-left', 'margin-right'],
  'margin-block': ['margin-top', 'margin-bottom'],
  'inset-inline': ['left', 'right'],
  'inset-block': ['top', 'bottom'],
}

// Splits a space-separated value list. Tailwind emits minified values such as
// `var(--a)var(--b)`, so a closing paren at depth 0 also ends a component.
function splitTopLevel(value: string): string[] {
  const out: string[] = []
  let depth = 0
  let cur = ''
  const flush = () => {
    if (cur) out.push(cur)
    cur = ''
  }
  for (const ch of value.trim()) {
    if (depth === 0 && /\s/.test(ch)) {
      flush()
      continue
    }
    cur += ch
    if (ch === '(') depth++
    else if (ch === ')' && --depth === 0) flush()
  }
  flush()
  return out
}

function physical(prop: string, value: string): [string, string][] | null {
  if (prop in LOGICAL) {
    const [a, b = a] = splitTopLevel(value)
    const [p1, p2] = LOGICAL[prop]
    return [[p1, a], [p2, b]]
  }
  if (prop === 'inset') {
    const [t, r = t, b = t, l = r] = splitTopLevel(value)
    return [['top', t], ['right', r], ['bottom', b], ['left', l]]
  }
  return null
}

// Each individual transform property feeds its own variable and a single
// rule composes them in spec order, so translate + rotate utilities on one
// element combine instead of overwriting each other's `transform`.
function transformVar(prop: string, value: string): [string, string] | null {
  const p = splitTopLevel(value)
  if (prop === 'translate') return ['--lw-translate', `translate(${p[0]}, ${p[1] ?? '0'})`]
  if (prop === 'rotate') return ['--lw-rotate', `rotate(${value})`]
  if (prop === 'scale') return ['--lw-scale', `scale(${p[0]}, ${p[1] ?? p[0]})`]
  return null
}

const inKeyframes = (rule: Rule) => {
  for (let p: Rule['parent'] | Container['parent'] = rule.parent; p; p = p.parent) {
    if (p.type === 'atrule' && (p as AtRule).name.endsWith('keyframes')) return true
  }
  return false
}

export default function legacyWebkit(): Plugin {
  return {
    postcssPlugin: 'legacy-webkit',
    OnceExit(root, { AtRule, Rule }) {
      // Safari < 15.4 drops whole @layer blocks, taking Tailwind's preflight
      // and utilities with them. Tailwind emits layers in priority order and
      // is imported before feature CSS, so flattening keeps the cascade.
      const layers: AtRule[] = []
      root.walkAtRules('layer', (a) => { layers.push(a) })
      for (const a of layers.reverse()) {
        if (a.nodes?.length) a.replaceWith(a.nodes)
        else a.remove()
      }

      let usesTransform = false

      root.walkRules((rule) => {
        if (inKeyframes(rule)) return
        let after: Rule | AtRule = rule

        // :matches() is Safari 9+. It counts :where() at full specificity,
        // so the copy is gated to engines that dropped the original.
        if (/:(is|where)\(/.test(rule.selector)) {
          const copy = rule.clone({ selector: rule.selector.replace(/:(is|where)\(/g, ':matches(') })
          const gate = new AtRule({ name: 'supports', params: PRE_14, nodes: [copy] })
          after.after(gate)
          after = gate
        }

        const gated = new Rule({ selector: rule.selector })
        rule.each((node) => {
          if (node.type !== 'decl') return
          // Replaced, not shadowed: the macOS 11.7 WKWebView resolves
          // `inset-inline: var(..)` to a start side of 0 but an end side of auto,
          // and that overrides a physical fallback placed before it. The pages
          // are horizontal LTR only (no RTL locale), so the physical sides are
          // exact.
          const sides = physical(node.prop, node.value)
          if (sides) {
            for (const [prop, value] of sides) node.cloneBefore({ prop, value })
            node.remove()
            return
          }
          const tv = transformVar(node.prop, node.value)
          if (tv) {
            gated.append({ prop: tv[0], value: tv[1], important: node.important })
            usesTransform = true
          }
          if (node.prop === 'overflow-wrap' && node.value === 'anywhere') {
            const wrap = new Rule({ selector: rule.selector })
            wrap.append({ prop: 'word-break', value: 'break-word' })
            const gate = new AtRule({ name: 'supports', params: 'not (overflow-wrap: anywhere)', nodes: [wrap] })
            after.after(gate)
            after = gate
          }
          if (node.prop === 'aspect-ratio') {
            const [w, h = '1'] = node.value.split('/').map((s) => s.trim())
            // Padding percentages resolve against width, so a floated spacer
            // reproduces the ratio for boxes that have a width and no height.
            const ratio = `calc(${h} / ${w} * 100%)`
            const before = new Rule({ selector: rule.selectors.map((s) => `${s}::before`).join(', ') })
            before.append(
              { prop: 'content', value: '""' },
              { prop: 'float', value: 'left' },
              { prop: 'padding-top', value: ratio },
            )
            const clear = new Rule({ selector: rule.selectors.map((s) => `${s}::after`).join(', ') })
            clear.append({ prop: 'content', value: '""' }, { prop: 'display', value: 'block' }, { prop: 'clear', value: 'both' })
            const gate = new AtRule({ name: 'supports', params: 'not (aspect-ratio: 1)', nodes: [before, clear] })
            after.after(gate)
            after = gate
          }
        })
        if (gated.nodes.length) {
          gated.append({ prop: 'transform', value: 'var(--lw-translate) var(--lw-rotate) var(--lw-scale)' })
          after.after(new AtRule({ name: 'supports', params: PRE_14_1, nodes: [gated] }))
        }
      })

      if (usesTransform) {
        const base = new Rule({ selector: '*, ::before, ::after' })
        base.append(
          { prop: '--lw-translate', value: 'translate(0, 0)' },
          { prop: '--lw-rotate', value: 'rotate(0)' },
          { prop: '--lw-scale', value: 'scale(1, 1)' },
        )
        root.prepend(new AtRule({ name: 'supports', params: PRE_14_1, nodes: [base] }))
      }
    },
  }
}
