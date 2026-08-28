// step2_copy.js
// 流程：选打印机 → 确认 → C++ 返回每台打印机可选的参照预设 → 用户每行选一个 → 下一步

function getLangParam() {
    var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
             || localStorage.getItem('BambuWebLang') || '';
    return lang ? '?lang=' + lang : '';
}

var printerData = [];      // [{ name, presets: ['打印机预设名', ...] }]
var selected = {};         // { modelName: Set<presetName> }
var activePrinter = null;
var presetsByMachine = {}; // { printerPresetName: [{name公开名, filament_preset精确名}] }

$(document).ready(function () {
    if (typeof TranslatePage === 'function') TranslatePage();

    // Toggle dropdown
    $('#input-printer').on('click', function (e) {
        e.stopPropagation();
        $('#printer-dropdown').toggleClass('hidden');
    });
    $(document).on('click', function (e) {
        if (!$(e.target).closest('#printer-dropdown, #input-printer').length)
            $('#printer-dropdown').addClass('hidden');
    });

    // 确认选择：关闭下拉，向 C++ 请求每台打印机的可选参照预设
    $('#btn-dropdown-confirm').on('click', function () {
        $('#printer-dropdown').addClass('hidden');
        var names = [];
        printerData.forEach(function (m) {
            if (selected[m.name] && selected[m.name].size > 0)
                selected[m.name].forEach(function (pn) { names.push(pn); });
        });
        $('#input-printer').val(
            printerData
                .filter(function (m) { return selected[m.name] && selected[m.name].size > 0; })
                .map(function (m) { return m.name; })
                .join(', ') || ''
        );
        if (names.length === 0) { $('#preset-table-area').hide(); updateNextBtn(); return; }

        var step1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
        if (typeof SendWXMessage !== 'undefined') {
            SendWXMessage(JSON.stringify({
                sequence_id: Math.round(Date.now() / 1000),
                command: 'get_presets_by_machine',
                printers: names,
                type: step1.type || ''
            }));
        } else {
            // 浏览器预览 mock
            var mockData = names.map(function (pn) {
                return {
                    printer: pn,
                    filament_presets: [
                        { name: 'Generic PETG', filament_preset: 'Generic PETG @BBL A1' },
                        { name: 'Bambu PETG HF', filament_preset: 'Bambu PETG HF @BBL A1' }
                    ]
                };
            });
            handlePresetsByMachine(mockData);
        }
        updateNextBtn();
    });

    // Prev / Cancel / Close
    $('#btn-prev').on('click', function () { window.location.href = 'index.html' + getLangParam(); });
    $('#btn-cancel, #btn-close-window').on('click', function () {
        if (typeof SendWXMessage !== 'undefined')
            SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'close_page' }));
    });

    // Next
    $('#btn-next').on('click', function () {
        if ($(this).prop('disabled')) return;
        var printerNozzles = [];
        printerData.forEach(function (m) {
            if (!selected[m.name]) return;
            selected[m.name].forEach(function (printerName) {
                var selectEl = document.getElementById('preset-select-' + printerName.replace(/[^a-zA-Z0-9]/g, '_'));
                var filamentPreset = selectEl ? selectEl.value : '';
                printerNozzles.push({ printer: printerName, base_preset: filamentPreset });
            });
        });
        var step1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
        var payload = {
            command: 'create_filament_confirm',
            mode: 'copy_presets',
            vendor: step1.vendor || '',
            type:   step1.type   || '',
            serial: step1.serial || '',
            printer_nozzles: printerNozzles
        };
        sessionStorage.setItem('step2', JSON.stringify(payload));
        window.location.href = 'step3.html' + getLangParam();
    });

    // C++ 数据注入
    window.HandleStudio = function (msg) {
        try {
            var data = (typeof msg === 'string') ? JSON.parse(msg) : msg;
            if (data.command === 'all_printers') {
                buildPrinterData(data.printers || []);
            } else if (data.command === 'presets_by_machine') {
                handlePresetsByMachine(data.data || []);
            }
        } catch (e) { console.error('HandleStudio error', e); }
    };

    if (typeof SendWXMessage !== 'undefined') {
        var _s1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
        SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'get_all_printers', type: _s1.type || '' }));
    } else {
        buildPrinterData([
            { name: 'Bambu Lab A1',  presets: ['Bambu Lab A1 0.2 nozzle', 'Bambu Lab A1 0.4 nozzle'] },
            { name: 'Bambu Lab H2D', presets: ['Bambu Lab H2D 0.4 nozzle', 'Bambu Lab H2D 0.6 nozzle'] },
        ]);
    }
});

// all_printers 返回 [{ name, presets: ['Bambu Lab A1 0.4 nozzle', ...] }]
function buildPrinterData(models) {
    printerData = models;
    selected = {};
    printerData.forEach(function (m) { selected[m.name] = new Set(); });
    activePrinter = printerData.length > 0 ? printerData[0].name : null;
    renderPrinterList();
    if (activePrinter) renderNozzleList(activePrinter);
}

function renderPrinterList() {
    var html = '';
    printerData.forEach(function (m) {
        var state = getCheckboxState(m.name);
        var activeClass = m.name === activePrinter ? ' active' : '';
        html += '<div class="list-row' + activeClass + '" data-name="' + m.name + '">'
              + '<div class="row-checkbox ' + state + '"></div>'
              + '<span class="row-name">' + m.name + '</span>'
              + '<span class="row-arrow">›</span>'
              + '</div>';
    });
    $('#printer-list').html(html);

    $('#printer-list .list-row').on('click', function (e) {
        e.stopPropagation();
        var name = $(this).data('name');
        if ($(e.target).hasClass('row-checkbox') || $(e.target).closest('.row-checkbox').length) {
            togglePrinter(name);
        } else {
            activePrinter = name;
            renderPrinterList();
            renderNozzleList(name);
        }
    });
}

function renderNozzleList(modelName) {
    var model = printerData.find(function (m) { return m.name === modelName; });
    if (!model) return;
    var html = '';
    (model.presets || []).forEach(function (item) {
        var printerName = typeof item === 'string' ? item : item.printer || item;
        var parts = printerName.split(' ');
        var label = parts.length >= 2 ? parts[parts.length - 2] + ' ' + parts[parts.length - 1] : printerName;
        var isChecked = selected[modelName] && selected[modelName].has(printerName);
        html += '<div class="list-row" data-preset="' + printerName + '">'
              + '<div class="row-checkbox ' + (isChecked ? 'checked' : '') + '"></div>'
              + '<span class="row-name">' + label + '</span>'
              + '<span class="row-arrow">›</span>'
              + '</div>';
    });
    $('#nozzle-list').html(html);

    $('#nozzle-list .list-row').on('click', function (e) {
        e.stopPropagation();
        var printerName = $(this).data('preset');
        if (selected[activePrinter].has(printerName))
            selected[activePrinter].delete(printerName);
        else
            selected[activePrinter].add(printerName);
        renderPrinterList();
        renderNozzleList(activePrinter);
    });
}

function getCheckboxState(modelName) {
    var model = printerData.find(function (m) { return m.name === modelName; });
    var total = model ? (model.presets || []).length : 0;
    var count = selected[modelName] ? selected[modelName].size : 0;
    if (count === 0) return '';
    if (count === total) return 'checked';
    return 'indeterminate';
}

function togglePrinter(modelName) {
    var model = printerData.find(function (m) { return m.name === modelName; });
    var state = getCheckboxState(modelName);
    if (state === 'checked') {
        selected[modelName] = new Set();
    } else {
        selected[modelName] = new Set(model.presets || []);
    }
    activePrinter = modelName;
    renderPrinterList();
    renderNozzleList(modelName);
}

// C++ 返回 presets_by_machine 后渲染参照预设表格
function handlePresetsByMachine(data) {
    // data: [{ printer, filament_presets: [{name, filament_preset}] }]
    presetsByMachine = {};
    data.forEach(function (item) {
        presetsByMachine[item.printer] = item.filament_presets || [];
    });
    renderPresetTable();
    updateNextBtn();
}

function renderPresetTable() {
    var html = '';
    var hasAny = false;
    printerData.forEach(function (m) {
        if (!selected[m.name] || selected[m.name].size === 0) return;
        selected[m.name].forEach(function (printerName) {
            var options = presetsByMachine[printerName] || [];
            var safeId = printerName.replace(/[^a-zA-Z0-9]/g, '_');
            var parts = printerName.split(' ');
            var label = parts.length >= 2 ? parts[parts.length - 2] + ' ' + parts[parts.length - 1] : printerName;
            var modelLabel = parts.length >= 3 ? parts.slice(0, parts.length - 2).join(' ') : printerName;

            var selectHtml = '<select class="preset-input" id="preset-select-' + safeId + '">';
            if (options.length === 0) {
                selectHtml += '<option value="">（无可用预设）</option>';
            } else {
                options.forEach(function (opt) {
                    selectHtml += '<option value="' + opt.filament_preset + '">' + opt.name + '</option>';
                });
            }
            selectHtml += '</select>';

            html += '<div class="preset-row">'
                  + '<span class="nozzle-name">' + modelLabel + ' ' + label + '</span>'
                  + '<div class="preset-select-wrapper">' + selectHtml + '</div>'
                  + '</div>';
            hasAny = true;
        });
    });

    if (hasAny) {
        $('#preset-table').html(html);
        $('#preset-table-area').show();
    } else {
        $('#preset-table-area').hide();
    }
}

function updateNextBtn() {
    var hasSelected = printerData.some(function (m) { return selected[m.name] && selected[m.name].size > 0; });
    var hasPresets = hasSelected && Object.keys(presetsByMachine).length > 0;

    // Every selected printer row must have an actual preset chosen — a row whose
    // select shows "（无可用预设）" (empty value) must not let the user proceed.
    var allRowsHavePreset = true;
    if (hasPresets) {
        printerData.forEach(function (m) {
            if (!selected[m.name]) return;
            selected[m.name].forEach(function (printerName) {
                var selectEl = document.getElementById('preset-select-' + printerName.replace(/[^a-zA-Z0-9]/g, '_'));
                if (!selectEl || !selectEl.value) allRowsHavePreset = false;
            });
        });
    }

    $('#btn-next').prop('disabled', !(hasPresets && allRowsHavePreset));
}
