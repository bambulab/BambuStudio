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
// 从 step3 Back 返回时暂存的状态。分两段消化：
// buildPrinterData → 回填 selected 并触发 get_presets_by_machine；
// handlePresetsByMachine → 回填每行 <select> 的选中值。
var pendingRestore = null;

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
        if (names.length === 0) {
            $('#preset-table-area').hide();
            $('#empty-state').show();
            updateNextBtn();
            return;
        }

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

    // Check saved state up front so buildPrinterData can consume it. The two-stage
    // replay lives in buildPrinterData (restore selected printers) and
    // handlePresetsByMachine (restore each row's preset pick).
    try {
        var s0 = JSON.parse(sessionStorage.getItem('step2') || 'null');
        if (s0 && s0.mode === 'copy_presets' && s0.printer_nozzles && s0.printer_nozzles.length > 0)
            pendingRestore = s0;
    } catch (e) {}

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
    // Toggle dropdown-internal empty state: when there are no compatible printers
    // for this filament type, show only the "No compatible printers" text — hide the
    // selection area AND the Confirm bar so there's nothing to interact with.
    if (printerData.length === 0) {
        $('#printer-dropdown .selection-area').hide();
        $('#printer-dropdown .dropdown-confirm-bar').hide();
        $('#dropdown-empty').show();
    } else {
        $('#dropdown-empty').hide();
        $('#printer-dropdown .selection-area').show();
        $('#printer-dropdown .dropdown-confirm-bar').show();
    }

    // Restore from step3 Back (stage 1/2): replay each saved printer pick into
    // `selected`, then rehydrate the input label and fire the same request the
    // Confirm button would have — stage 2 (per-row preset pick) runs in
    // handlePresetsByMachine once the server responds.
    if (pendingRestore && pendingRestore.printer_nozzles) {
        var validPrinters = new Set();
        models.forEach(function (m) {
            (m.presets || []).forEach(function (item) {
                var name = typeof item === 'string' ? item : (item.printer || item);
                validPrinters.add(name);
            });
        });
        var names = [];
        pendingRestore.printer_nozzles.forEach(function (pn) {
            if (!validPrinters.has(pn.printer)) return;
            var owning = models.find(function (m) {
                return (m.presets || []).some(function (item) {
                    var name = typeof item === 'string' ? item : (item.printer || item);
                    return name === pn.printer;
                });
            });
            if (owning) {
                selected[owning.name].add(pn.printer);
                names.push(pn.printer);
            }
        });
        if (names.length > 0) {
            $('#input-printer').val(
                models
                    .filter(function (m) { return selected[m.name] && selected[m.name].size > 0; })
                    .map(function (m) { return m.name; })
                    .join(', ') || ''
            );
            var step1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
            if (typeof SendWXMessage !== 'undefined') {
                SendWXMessage(JSON.stringify({
                    sequence_id: Math.round(Date.now() / 1000),
                    command: 'get_presets_by_machine',
                    printers: names,
                    type: step1.type || ''
                }));
            }
        } else {
            // No saved printer picks matched the current all_printers response — drop the
            // pending restore so stage 2 doesn't try to reapply row preset picks that no
            // longer belong to any selected row.
            pendingRestore = null;
        }
    }

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
    // Restore from step3 Back (stage 2/2): after the <select>s exist in the DOM,
    // reapply each remembered `base_preset` — skipping rows whose remembered value
    // isn't among the freshly-fetched options.
    if (pendingRestore && pendingRestore.printer_nozzles) {
        pendingRestore.printer_nozzles.forEach(function (pn) {
            if (!pn.base_preset) return;
            var selectEl = document.getElementById('preset-select-' + pn.printer.replace(/[^a-zA-Z0-9]/g, '_'));
            if (!selectEl) return;
            var hasOption = Array.prototype.some.call(selectEl.options, function (o) { return o.value === pn.base_preset; });
            if (hasOption) selectEl.value = pn.base_preset;
        });
        pendingRestore = null;
    }
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
        $('#empty-state').hide();
    } else {
        $('#preset-table-area').hide();
        $('#empty-state').show();
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
