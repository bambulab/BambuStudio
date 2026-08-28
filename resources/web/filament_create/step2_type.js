// step2_type.js
// 流程：选基准预设 → C++返回 compatible_printers → 左右两列选打印机+喷嘴 → 下一步

function getLangParam() {
    var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
             || localStorage.getItem('BambuWebLang') || '';
    return lang ? '?lang=' + lang : '';
}

// 每个机型对应的喷嘴列表：{ modelName: [printerPresetName, ...] }
var printerData = [];
// 勾选状态：{ modelName: Set<printerPresetName> }
var selected = {};
var activePrinter = null;
var selectedPreset = '';
var systemPresets = [];

$(document).ready(function () {
    if (typeof TranslatePage === 'function') TranslatePage();

    $('#btn-prev').on('click', function () { window.location.href = 'index.html' + getLangParam(); });
    $('#btn-cancel, #btn-close-window').on('click', function () {
        if (typeof SendWXMessage !== 'undefined')
            SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'close_page' }));
    });

    $('#btn-next').on('click', function () {
        if ($(this).prop('disabled')) return;
        // 收集所有勾选的打印机预设名称，base_preset 使用 C++ 传来的精确 filament preset 名
        var printerNozzles = [];
        printerData.forEach(function (model) {
            selected[model.name].forEach(function (printerName) {
                var item = model.presets.find(function (p) { return p.printer === printerName; });
                printerNozzles.push({
                    printer: printerName,
                    base_preset: item ? item.filament_preset : selectedPreset
                });
            });
        });
        var step1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
        var payload = {
            command: 'create_filament_confirm',
            mode: 'based_on_type',
            vendor: step1.vendor || '',
            type:   step1.type   || '',
            serial: step1.serial || '',
            base_preset: selectedPreset,
            printer_nozzles: printerNozzles
        };
        sessionStorage.setItem('step2', JSON.stringify(payload));
        window.location.href = 'step3.html' + getLangParam();
    });

    // 基准预设下拉
    $('#input-base-preset').on('click', function (e) {
        e.stopPropagation();
        $('#base-preset-dropdown').toggleClass('hidden');
    });
    $(document).on('click', function (e) {
        if (!$(e.target).closest('.input-wrapper').length)
            $('#base-preset-dropdown').addClass('hidden');
    });
    $(document).on('click', '#base-preset-options .dropdown-item', function () {
        selectedPreset = $(this).data('val');
        $('#input-base-preset').val(selectedPreset);
        $('#base-preset-dropdown').addClass('hidden');
        loadPrinterList(selectedPreset);
    });

    // C++ 数据注入入口
    window.HandleStudio = function (msg) {
        try {
            var data = (typeof msg === 'string') ? JSON.parse(msg) : msg;
            if (data.command === 'init_data') {
                systemPresets = data.system_presets || [];
                renderPresetDropdown();
            } else if (data.command === 'compatible_printers') {
                // data.printers: [
                //   { name: 'Bambu Lab A1', presets: ['Bambu Lab A1 0.4 nozzle', 'Bambu Lab A1 0.2 nozzle'] },
                //   ...
                // ]
                buildPrinterData(data.printers || []);
            }
        } catch (e) { console.error('HandleStudio error', e); }
    };

    if (typeof SendWXMessage !== 'undefined') {
        var _s1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
        SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'request_init_data', type: _s1.type || '' }));
    } else {
        // 浏览器预览：只填充预设下拉，不自动展开打印机列表
        systemPresets = ['Bambu ABS', 'Polymaker ABS Basic'];
        renderPresetDropdown();
    }
});

function renderPresetDropdown() {
    var html = '';
    systemPresets.forEach(function (p) {
        html += '<div class="dropdown-item" data-val="' + p + '">' + p + '</div>';
    });
    $('#base-preset-options').html(html || '<div class="dropdown-item disabled">No presets available</div>');
}

function loadPrinterList(presetName) {
    if (typeof SendWXMessage !== 'undefined') {
        SendWXMessage(JSON.stringify({
            sequence_id: Math.round(Date.now() / 1000),
            command: 'get_compatible_printers',
            preset: presetName
        }));
    } else {
        // 浏览器预览：只有用户主动选了预设才 mock 返回打印机列表
        if (!presetName) return;
        buildPrinterData([
            { name: 'Bambu Lab A1',        presets: [{ printer: 'Bambu Lab A1 0.2 nozzle', filament_preset: 'Bambu ABS @BBL A1 0.2 nozzle' }, { printer: 'Bambu Lab A1 0.4 nozzle', filament_preset: 'Bambu ABS @BBL A1' }] },
            { name: 'Bambu Lab X1 Carbon', presets: [{ printer: 'Bambu Lab X1 Carbon 0.4 nozzle', filament_preset: 'Bambu ABS @BBL X1C' }] },
        ]);
    }
}

// 构建 printerData + selected 状态，然后渲染左列
// models 格式: [{ name, presets: [{printer, filament_preset}, ...] }]
function buildPrinterData(models) {
    printerData = models;
    selected = {};
    models.forEach(function (m) { selected[m.name] = new Set(); });
    activePrinter = models.length > 0 ? models[0].name : null;

    $('#selection-area').show();
    renderPrinterList();
    if (activePrinter) renderNozzleList(activePrinter);
    updateNextBtn();
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
    model.presets.forEach(function (item) {
        var printerName = item.printer;
        // 喷嘴显示名：取最后两个 token（即 "0.4 nozzle"）
        var parts = printerName.split(' ');
        var nozzleLabel = parts.length >= 2 ? parts[parts.length - 2] + ' ' + parts[parts.length - 1] : printerName;
        var isChecked = selected[modelName].has(printerName);
        html += '<div class="list-row" data-preset="' + printerName + '">'
              + '<div class="row-checkbox ' + (isChecked ? 'checked' : '') + '"></div>'
              + '<span class="row-name">' + nozzleLabel + '</span>'
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
        updateNextBtn();
    });
}

function getCheckboxState(modelName) {
    var model = printerData.find(function (m) { return m.name === modelName; });
    var total = model ? model.presets.length : 0;
    var count = selected[modelName] ? selected[modelName].size : 0;
    if (count === 0) return '';
    if (count === total) return 'checked';
    return 'indeterminate';
}

function togglePrinter(modelName) {
    var model = printerData.find(function (m) { return m.name === modelName; });
    var state = getCheckboxState(modelName);
    selected[modelName] = state === 'checked'
        ? new Set()
        : new Set(model.presets.map(function (p) { return p.printer; }));
    activePrinter = modelName;
    renderPrinterList();
    renderNozzleList(modelName);
    updateNextBtn();
}

function updateNextBtn() {
    var any = printerData.some(function (m) { return selected[m.name].size > 0; });
    $('#btn-next').prop('disabled', !any);
}
