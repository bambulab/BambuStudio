// Flex `gap` needs Safari 14.1. Older macOS WKWebView accepts the property (it
// is valid for grid) but ignores it on flex containers, so CSS can neither
// detect nor replace it. This shim turns the computed gap into margins and is
// a no-op wherever flex gap works natively.

const ITEM = 'data-lw-gap-item'

// Variables carry the author's own margins plus the gap, so the stylesheet
// can win with !important without discarding the original spacing, and React
// re-renders that rewrite `style` do not fight stored inline margins.
const SHEET = `
[${ITEM}] {
  margin-left: calc(var(--lw-ml) + var(--lw-gl)) !important;
  margin-top: calc(var(--lw-mt) + var(--lw-gt)) !important;
  margin-right: calc(var(--lw-mr) + var(--lw-gr)) !important;
  margin-bottom: calc(var(--lw-mb) + var(--lw-gb)) !important;
}
`
const VARS = ['--lw-ml', '--lw-mt', '--lw-mr', '--lw-mb', '--lw-gl', '--lw-gt', '--lw-gr', '--lw-gb']

function supportsFlexGap(): boolean {
  const box = document.createElement('div')
  box.style.cssText = 'display:flex;flex-direction:column;row-gap:1px;position:absolute;visibility:hidden'
  box.append(document.createElement('div'), document.createElement('div'))
  document.body.appendChild(box)
  const ok = box.scrollHeight === 1
  box.remove()
  return ok
}

const px = (v: string) => parseFloat(v) || 0

// Inline <svg> children are flex items too.
type Styled = HTMLElement | SVGElement
type Item = {
  el: Styled
  margins: [string, string, string, string] // left, top, right, bottom
  auto: boolean
  afterText: boolean
  beforeText: boolean
}

// A rendered flex item reports `margin: auto` as the resolved pixels, so auto
// has to be read from the declarations instead. A rule whose auto is later
// overridden still matches here; that only costs the item its gap, whereas
// missing a real auto would freeze it and push the item out of place.
const MARGIN_PROPS = [
  'margin-left', 'margin-top', 'margin-right', 'margin-bottom',
  'margin-inline-start', 'margin-inline-end', 'margin-block-start', 'margin-block-end',
]
type AutoRule = { selector: string; media: MediaList[] }
let autoRules: AutoRule[] = []
let autoRulesSheetCount = -1

const declaresAutoMargin = (s: CSSStyleDeclaration) => MARGIN_PROPS.some((p) => s.getPropertyValue(p) === 'auto')

function collectAutoRules(rules: CSSRuleList, media: MediaList[], out: AutoRule[]) {
  for (const r of Array.from(rules)) {
    if (r instanceof CSSStyleRule) {
      if (!declaresAutoMargin(r.style)) continue
      try {
        // Pseudo-element selectors cannot match an element and may throw.
        document.documentElement.matches(r.selectorText)
        out.push({ selector: r.selectorText, media })
      } catch { /* not matchable */ }
    } else if (r instanceof CSSMediaRule) {
      collectAutoRules(r.cssRules, [...media, r.media], out)
    } else if (!(r instanceof CSSKeyframesRule) && 'cssRules' in r) {
      // @supports and friends: recursing unconditionally only errs towards
      // treating an item as auto.
      collectAutoRules((r as CSSGroupingRule).cssRules, media, out)
    }
  }
}

function refreshAutoRules() {
  // Production CSS is injected once at startup; the count only moves in dev.
  if (document.styleSheets.length === autoRulesSheetCount) return
  autoRulesSheetCount = document.styleSheets.length
  autoRules = []
  for (const sheet of Array.from(document.styleSheets)) {
    try {
      collectAutoRules(sheet.cssRules, [], autoRules)
    } catch { /* cross-origin sheet */ }
  }
}

const hasAutoMargin = (el: Styled) =>
  declaresAutoMargin(el.style) ||
  autoRules.some((r) => r.media.every((m) => matchMedia(m.mediaText).matches) && el.matches(r.selector))
type Plan = { row: boolean; hGap: string; vGap: string; items: Item[]; wrap: boolean }

function reset(root: ParentNode) {
  for (const el of Array.from(root.querySelectorAll<Styled>(`[${ITEM}]`))) {
    el.removeAttribute(ITEM)
    for (const v of VARS) el.style.removeProperty(v)
  }
}

function plan(container: HTMLElement, cs: CSSStyleDeclaration): Plan {
  const items: Item[] = []
  // Bare text becomes an anonymous flex item that cannot take a margin, so
  // the gap on either side of it goes on the neighbouring element instead.
  let pendingText = false
  for (const c of Array.from(container.childNodes)) {
    if (c.nodeType === Node.TEXT_NODE) {
      if (c.textContent?.trim()) {
        pendingText = true
        const prev = items[items.length - 1]
        if (prev) prev.beforeText = true
      }
      continue
    }
    if (!(c instanceof HTMLElement || c instanceof SVGElement)) continue
    const k = getComputedStyle(c)
    if (k.display === 'none' || k.position === 'absolute' || k.position === 'fixed') continue
    items.push({
      el: c,
      margins: [k.marginLeft, k.marginTop, k.marginRight, k.marginBottom],
      auto: hasAutoMargin(c),
      afterText: pendingText,
      beforeText: false,
    })
    pendingText = false
  }
  return {
    row: cs.flexDirection.startsWith('row'),
    // column-gap is always horizontal and row-gap vertical.
    hGap: `${px(cs.columnGap)}px`,
    vGap: `${px(cs.rowGap)}px`,
    items,
    wrap: cs.flexWrap !== 'nowrap',
  }
}

// Custom properties inherit, so every marked item sets all of them
// explicitly; otherwise a nested item would pick up its ancestor's gap.
function mark(it: Item, gaps: [string, string, string, string]) {
  const s = it.el.style
  const names = ['l', 't', 'r', 'b']
  it.margins.forEach((m, i) => s.setProperty(`--lw-m${names[i]}`, m))
  gaps.forEach((g, i) => s.setProperty(`--lw-g${names[i]}`, g))
  it.el.setAttribute(ITEM, '')
}

// `margin: auto` pushes items apart on purpose; such items are left alone.
const isAuto = (it: Item) => it.auto

function scan(root: HTMLElement) {
  reset(root)
  refreshAutoRules()
  // Read everything first, then write, to avoid interleaved reflows.
  const plans: Plan[] = []
  for (const el of Array.from(root.querySelectorAll<HTMLElement>('*'))) {
    if (!(el instanceof HTMLElement)) continue
    const cs = getComputedStyle(el)
    if (cs.display !== 'flex' && cs.display !== 'inline-flex') continue
    if (px(cs.rowGap) === 0 && px(cs.columnGap) === 0) continue
    // Reversed directions would need mirrored margins; nothing uses them.
    if (cs.flexDirection.endsWith('reverse')) continue
    plans.push(plan(el, cs))
  }

  // Pass 1: main-axis gap before every item that follows another one, and
  // after any element followed by text.
  const Z = '0px'
  const gapsFor = new Map<Item, [string, string, string, string]>()
  for (const p of plans) {
    const g = p.row ? p.hGap : p.vGap
    p.items.forEach((it, i) => {
      if (isAuto(it)) return
      const lead = i > 0 || it.afterText ? g : Z
      const trail = it.beforeText ? g : Z
      if (lead === Z && trail === Z) return
      const gaps: [string, string, string, string] = p.row ? [lead, Z, trail, Z] : [Z, lead, Z, trail]
      gapsFor.set(it, gaps)
      mark(it, gaps)
    })
  }

  // Pass 2, wrapping only: an item that starts a new line drops its
  // main-axis lead and takes the cross-axis gap instead. Dropping a margin
  // only shrinks the line it starts, so the breaks found here stay valid.
  for (const p of plans) {
    if (!p.wrap) continue
    // Items on one line can sit at different cross offsets (align-items:
    // center), so a new line starts only past the current line's far edge.
    const span = (it: Item) => {
      const r = it.el.getBoundingClientRect()
      return p.row ? [r.top, r.bottom] : [r.left, r.right]
    }
    const starts: Item[] = []
    let lineEnd = -Infinity
    p.items.forEach((it, i) => {
      const [start, end] = span(it)
      if (i > 0 && start >= lineEnd) {
        starts.push(it)
        lineEnd = end
      } else {
        lineEnd = Math.max(lineEnd, end)
      }
    })
    for (const it of starts) {
      if (isAuto(it)) continue
      const prev = gapsFor.get(it) ?? [Z, Z, Z, Z]
      mark(it, p.row ? [Z, p.vGap, prev[2], Z] : [p.hGap, Z, Z, prev[3]])
    }
  }
}

// `force` lets a modern engine run the shim (with native gap disabled by the
// caller) to preview the old-WebKit layout.
export function installFlexGapShim(force = false) {
  if (!force && supportsFlexGap()) return

  const style = document.createElement('style')
  style.textContent = SHEET
  document.head.appendChild(style)

  const OBSERVE: MutationObserverInit = { childList: true, subtree: true, attributes: true, characterData: true }
  let queued = false
  const run = () => {
    queued = false
    observer.disconnect()
    scan(document.body)
    // Records produced by our own writes are dropped before re-observing.
    observer.takeRecords()
    observer.observe(document.body, OBSERVE)
  }
  const schedule = () => {
    if (queued) return
    queued = true
    requestAnimationFrame(run)
  }
  const observer = new MutationObserver(schedule)
  observer.observe(document.body, OBSERVE)
  window.addEventListener('resize', schedule)
  schedule()
}
