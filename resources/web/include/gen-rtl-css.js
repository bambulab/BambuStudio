/*
 * Generator for the machine-written half of include/rtl.css.
 *
 * Usage (from resources/web):   node include/gen-rtl-css.js
 *
 * For every page stylesheet listed in SOURCES it finds the rules that use physical
 * left/right properties (margin/padding/border-*, left/right, float, clear, text-align,
 * border-radius corners, background-position keywords) and emits a mirrored rule scoped to
 *     html[dir="rtl"][data-rtl-sheets~="<path of the source sheet>"] <original selector>
 * rtl.css is loaded by data/text.js only when the page language is RTL, and text.js sets
 * data-rtl-sheets on <html> to the list of stylesheets the page really loaded, so rules from
 * one page's sheet never leak onto another page that reuses the same class names.
 *
 * Everything between the BEGIN/END GENERATED markers of rtl.css is overwritten; keep hand-written
 * rules ABOVE the BEGIN marker. Constructs the generator cannot mirror safely (transform,
 * background shorthand with a position, box-shadow with x offset, ...) are only reported on stderr
 * and have to be handled by hand in the hand-written section.
 */
'use strict';
const fs = require('fs');
const path = require('path');

const WEB = path.resolve(__dirname, '..');
const OUT = path.join(__dirname, 'rtl.css');
const BEGIN = '/* ==== BEGIN GENERATED (include/gen-rtl-css.js) - do not edit below ==== */';
const END = '/* ==== END GENERATED ==== */';

// Sheets used by the translated pages. Value = media attribute of the <link>, if any.
const SOURCES = {
	'homepage3/css/common.css': null,
	'homepage3/css/rightcommon.css': null,
	'homepage3/css/recent.css': null,
	'homepage3/css/online.css': null,
	'homepage3/css/manual.css': null,
	'homepage3/css/home.css': null,
	'homepage3/css/wiki.css': null,
	'homepage3/css/left.css': null,
	'homepage3/css/left_mini.css': 'screen and (max-width: 168px)',
	'homepage3/css/disconnect.css': null,
	'guide/css/common.css': null,
	'guide/css/home.css': null,
	'guide/0/load.css': null,
	'guide/1/1.css': null,
	'guide/3/3.css': null,
	'guide/5/5.css': null,
	'guide/6/6.css': null,
	'guide/11/11.css': null,
	'guide/21/21.css': null,
	'guide/22/22.css': null,
	'guide/22_remove/22.css': null,
	'guide/23/23.css': null,
	'guide/24/24.css': null,
	'guide/31/31.css': null,
	'filament_create/style.css': null,
	'filament_create/step1.css': null,
	'filament_create/step2.css': null,
	'filament_create/step2_copy.css': null,
	'filament_create/step2_type.css': null,
	'filament_create/step3.css': null,
	'filament_create/edit_filament.css': null,
	'login/css/login.css': null,
	'device/css/home.css': null,
	'model/model.css': null,
	'model_new/index.css': null,
	'model_new/css/editor.css': null,
	'model_new/css/navigation.css': null,
	'model_new/css/gallery.css': null,
	'model_new/css/accessory_dropdown.css': null,
	'model_new/css/tool.css': null,
	};

const manual = [];   // things reported but not mirrored

// ---- tiny CSS tokenizer -------------------------------------------------------------------
function stripComments(s) { return s.replace(/\/\*[\s\S]*?\*\//g, ''); }

// split on a separator at depth 0 (outside (), [], quotes)
function splitTop(s, sep) {
	const out = []; let depth = 0, q = null, cur = '';
	for (let i = 0; i < s.length; i++) {
		const c = s[i];
		if (q) { cur += c; if (c === '\\') { cur += s[++i] || ''; } else if (c === q) q = null; continue; }
		if (c === '"' || c === "'") { q = c; cur += c; continue; }
		if (c === '(' || c === '[') depth++;
		else if (c === ')' || c === ']') depth--;
		if (c === sep && depth === 0) { out.push(cur); cur = ''; } else cur += c;
	}
	out.push(cur);
	return out;
}

// parse a block list into nodes: {sel, decls[]} | {at, body: nodes[]} | {raw}
function parseBlocks(s) {
	const nodes = []; let i = 0;
	while (i < s.length) {
		let start = i, depth = 0, q = null;
		// read prelude up to '{' or ';'
		while (i < s.length && !(s[i] === '{' && !q) && !(s[i] === ';' && !q)) {
			if (q) { if (s[i] === '\\') i++; else if (s[i] === q) q = null; }
			else if (s[i] === '"' || s[i] === "'") q = s[i];
			else if (s[i] === '(') depth++;
			else if (s[i] === ')') depth--;
			i++;
		}
		const prelude = s.slice(start, i).trim();
		if (i >= s.length) break;
		if (s[i] === ';') { i++; continue; }  // @import / @charset - ignored
		// matching close brace
		let d = 1, j = i + 1; q = null;
		while (j < s.length && d > 0) {
			const c = s[j];
			if (q) { if (c === '\\') j++; else if (c === q) q = null; }
			else if (c === '"' || c === "'") q = c;
			else if (c === '(') { /* url(data:...) may contain braces? not in our sheets */ }
			else if (c === '{') d++;
			else if (c === '}') d--;
			j++;
		}
		const body = s.slice(i + 1, j - 1);
		i = j;
		if (!prelude) continue;
		if (prelude[0] === '@') {
			if (/^@(media|supports)/i.test(prelude)) nodes.push({ at: prelude, body: parseBlocks(body) });
			// @keyframes / @font-face / ... : skipped
		} else {
			const decls = splitTop(body, ';').map(x => x.trim()).filter(Boolean).map(x => {
				const k = x.indexOf(':');
				return k < 0 ? null : { prop: x.slice(0, k).trim().toLowerCase(), val: x.slice(k + 1).trim() };
			}).filter(Boolean);
			nodes.push({ sel: prelude, decls });
		}
	}
	return nodes;
}

// ---- mirroring ---------------------------------------------------------------------------
const flipWord = v => v.replace(/\b(left|right)\b/gi, m => (m.toLowerCase() === 'left' ? 'right' : 'left'));
const names = (s) => s; // placeholder for readability

function stripImportant(v) { return v.replace(/\s*!important\s*$/i, ''); }
function hasImportant(v) { return /!important\s*$/i.test(v); }
function vals(v) { return splitTop(stripImportant(v).trim().replace(/\s+/g, ' '), ' ').filter(Boolean); }

// returns array of {prop,val} for the mirrored rule (empty when nothing to mirror)
function mirrorDecls(decls, sel) {
	const m = new Map();     // prop -> val of declarations we keep (last wins)
	for (const d of decls) m.set(d.prop, d.val);
	const out = [];
	const has = p => m.has(p);
	const get = p => m.get(p);
	const emit = (prop, val) => out.push({ prop, val });

	// pairs of longhands that simply swap names
	const pairs = [
		['margin-left', 'margin-right', '0'],
		['padding-left', 'padding-right', '0'],
		['left', 'right', 'auto'],
		['border-left-width', 'border-right-width', '0'],
		['border-left-style', 'border-right-style', 'none'],
		['border-left-color', 'border-right-color', 'transparent'],
		['border-left', 'border-right', 'none'],
		['border-top-left-radius', 'border-top-right-radius', '0'],
		['border-bottom-left-radius', 'border-bottom-right-radius', '0'],
		['scroll-margin-left', 'scroll-margin-right', '0'],
	];
	// left/right:50% (+ negative margin or translateX) centres the box: direction independent, leave it alone
	const centred = ['left', 'right'].some(p => has(p) && /^(50%|calc\(50%)/.test(stripImportant(get(p))));
	const rotated = has('transform') && /rotate/i.test(get('transform'));
	for (const [l, r, reset] of pairs) {
		if (centred && (l === 'left' || l === 'margin-left')) continue;
		// borders that draw a rotated glyph (chevrons made from two borders + rotate(45deg)) must not be mirrored
		if (rotated && l.indexOf('border-') === 0) continue;
		const hl = has(l), hr = has(r);
		if (!hl && !hr) continue;
		const lv = hl ? get(l) : null, rv = hr ? get(r) : null;
		if (hl && hr) {
			if (stripImportant(lv) === stripImportant(rv)) continue;   // symmetric
			emit(l, rv); emit(r, lv);
		} else if (hl) {
			emit(r, lv); emit(l, reset + (hasImportant(lv) ? ' !important' : ''));
		} else {
			emit(l, rv); emit(r, reset + (hasImportant(rv) ? ' !important' : ''));
		}
	}

	// 4-value shorthands: top right bottom left  -> top left bottom right
	for (const p of ['margin', 'padding']) {
		if (!has(p)) continue;
		const v = vals(get(p));
		if (v.length === 4 && v[1] !== v[3]) emit(p, [v[0], v[3], v[2], v[1]].join(' ') + (hasImportant(get(p)) ? ' !important' : ''));
	}
	// border-radius: tl tr br bl (with 2/3-value expansion); skip the "a / b" elliptic form
	if (has('border-radius') && !/\//.test(get('border-radius'))) {
		let v = vals(get('border-radius'));
		if (v.length === 1) v = null;
		else {
			if (v.length === 2) v = [v[0], v[1], v[0], v[1]];
			else if (v.length === 3) v = [v[0], v[1], v[2], v[1]];
			if (v && !(v[0] === v[1] && v[2] === v[3])) emit('border-radius', [v[1], v[0], v[3], v[2]].join(' '));
		}
	}
	// float / clear / text-align
	for (const p of ['float', 'clear', 'text-align']) {
		if (has(p) && /\b(left|right)\b/i.test(get(p))) emit(p, flipWord(get(p)));
	}
	// background-position keywords
	if (has('background-position') && /\b(left|right)\b/i.test(get('background-position'))) emit('background-position', flipWord(get('background-position')));
	if (has('background-position-x') && /\b(left|right)\b/i.test(get('background-position-x'))) emit('background-position-x', flipWord(get('background-position-x')));
	if (has('object-position') && /\b(left|right)\b/i.test(get('object-position'))) emit('object-position', flipWord(get('object-position')));

	// ---- things we only report ----
	for (const [p, v] of m) {
		if (p === 'transform' && /translate(X)?\(|rotate|scaleX|matrix/i.test(v)) manual.push(`${sel} { transform: ${v} }`);
		else if (p === 'background' && /\b(left|right)\b/i.test(v)) manual.push(`${sel} { background: ${v.slice(0, 60)} }`);
		else if (p === 'background' && /\)\s*(no-repeat\s*)?-?\d+(px|%)/i.test(v)) manual.push(`${sel} { background: ${v.slice(0, 60)} (px position) }`);
		else if (p === 'background-position' && !/\b(left|right|center|top|bottom)\b/i.test(v) ) manual.push(`${sel} { background-position: ${v} }`);
		else if (p === 'box-shadow' && /^-?[1-9]\d*px\s/.test(v)) manual.push(`${sel} { box-shadow: ${v.slice(0, 50)} } (x offset)`);
		else if ((p === 'clip' || p === 'clip-path') && /\d/.test(v)) manual.push(`${sel} { ${p}: ${v.slice(0, 40)} }`);
	}
	return out;
}

function prefixFor(key) { return `html[dir="rtl"][data-rtl-sheets~="${key}"]`; }

function prefixSelector(sel, key) {
	const pre = prefixFor(key);
	return splitTop(sel, ',').map(s => s.trim()).filter(Boolean).map(s => {
		// html / :root as the first compound selector: merge into the prefix
		const mm = s.match(/^(html|:root)(?![\w-])(.*)$/i);
		if (mm) return pre + mm[2];
		return pre + ' ' + s;
	}).join(',\n');
}

function genNodes(nodes, key, indent) {
	let css = '';
	for (const n of nodes) {
		if (n.at) {
			const inner = genNodes(n.body, key, indent + '\t');
			if (inner) css += `${indent}${n.at} {\n${inner}${indent}}\n`;
		} else {
			const md = mirrorDecls(n.decls, n.sel.replace(/\s+/g, ' '));
			if (!md.length) continue;
			const needBang = /#/.test(n.sel);   // an #id in the original beats the attribute-based prefix
			const body = md.map(d => `${indent}\t${d.prop}: ${needBang && !hasImportant(d.val) ? d.val + ' !important' : d.val};`).join('\n');
			css += `${indent}${prefixSelector(n.sel.replace(/\s+/g, ' '), key).replace(/\n/g, '\n' + indent)} {\n${body}\n${indent}}\n`;
		}
	}
	return css;
}

let gen = '';
let total = 0;
for (const key of Object.keys(SOURCES)) {
	const file = path.join(WEB, key);
	if (!fs.existsSync(file)) { console.error('missing source: ' + key); continue; }
	const nodes = parseBlocks(stripComments(fs.readFileSync(file, 'utf8')));
	let css = genNodes(nodes, key, '');
	if (!css) continue;
	if (SOURCES[key]) css = `@media ${SOURCES[key]} {\n${css.replace(/^/gm, '\t').replace(/\t$/, '')}}\n`;
	total += (css.match(/\{/g) || []).length;
	gen += `\n/* --- ${key} --- */\n${css}`;
}

let cur = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '';
const bi = cur.indexOf(BEGIN);
const head = bi >= 0 ? cur.slice(0, bi) : cur.replace(/\s*$/, '\n\n');
fs.writeFileSync(OUT, head + BEGIN + '\n' + gen + END + '\n', 'utf8');
console.error(`generated ${total} rules for ${Object.keys(SOURCES).length} sheets`);
if (manual.length) { console.error('\nNOT auto-mirrored (review by hand):'); manual.forEach(x => console.error('  ' + x)); }
