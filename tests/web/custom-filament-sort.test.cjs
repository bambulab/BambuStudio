// Run with: node tests/web/custom-filament-sort.test.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../resources/web/guide/23/23.js'), 'utf8');
const functions = source.slice(source.indexOf('var g_customFilaments = []'), source.indexOf('function toggleCustomGroup('));
const filaments = [
    { name: 'SUNLU PLA 10', type: 'PLA', date: '2026-10-01' },
    { name: 'elegoo ASA-CF', type: 'ASA-CF', date: '2026-10-02' },
    { name: 'Amolen PLA', type: 'PLA', date: '2026-10-03' },
    { name: 'SUNLU PLA 2', type: 'PLA', date: '2026-10-04' }
];
let savedOrder;

function openWindow() {
    let selection;
    let rendered = [];
    const context = vm.createContext({
        localStorage: { getItem: () => 'en' },
        LangText: { en: {} },
        FilamentPriority: [],
        sortByPriority: (items) => items.sort(),
        $: (selector) => ({
            val(value) {
                assert.equal(selector, '#CFilament_Sort');
                if (arguments.length) selection = value;
                return selection;
            },
            hide() {}, show() {}, text() {}, html() {}
        }),
        SendWXMessage: (raw) => {
            const message = JSON.parse(raw);
            assert.equal(message.command, 'set_custom_filament_sort_order');
            savedOrder = message.sort_order;
        },
        renderCustomGroup: (_title, items) => {
            rendered.push(...items.map((item) => item.name));
            return '';
        }
    });
    vm.runInContext(functions, context);
    return {
        restore(order = savedOrder) {
            rendered = [];
            context.UpdateCustomFilaments(filaments, order);
        },
        select(order) {
            selection = order;
            rendered = [];
            context.onCustomSortChange();
        },
        get selection() { return selection; },
        get rendered() { return rendered; }
    };
}

let window = openWindow();
window.restore();
assert.equal(window.selection, 'desc');
assert.equal(savedOrder, undefined, 'opening a window must not overwrite a saved preference');

const alphabetical = ['Amolen PLA', 'elegoo ASA-CF', 'SUNLU PLA 2', 'SUNLU PLA 10'];
window.select('name');
assert.deepEqual(window.rendered, alphabetical);
window = openWindow();
window.restore();
assert.equal(window.selection, 'name');
assert.deepEqual(window.rendered, alphabetical);

window.select('name_desc');
window = openWindow();
window.restore();
assert.equal(window.selection, 'name_desc');
assert.deepEqual(window.rendered, alphabetical.slice().reverse());

for (const order of ['asc', 'desc', 'type']) {
    window.select(order);
    window = openWindow();
    window.restore();
    assert.equal(window.selection, order);
}

// Reset removes the saved app setting; a new window gets the original default.
savedOrder = undefined;
window = openWindow();
window.restore();
assert.equal(window.selection, 'desc');
window.restore('invalid');
assert.equal(window.selection, 'desc');
console.log('Custom filament sort save/restore, both name directions, and default/reset behavior passed.');
