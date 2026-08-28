// step2.js — 为当前连接的打印机创建耗材
// 流程：C++ 返回设备信息（型号 + 喷嘴 + 可用基准预设）→ 用户选喷嘴 + 基准预设
//       → C++ 返回该预设参数 → 展示参数 → 下一步到 step3

function getLangParam() {
    var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
             || localStorage.getItem('BambuWebLang') || '';
    return lang ? '?lang=' + lang : '';
}

var deviceInfo   = null;  // { device_name, printer_model_id, printer_preset_base, nozzles[], system_presets[] }
var selectedPreset   = null; // { name, filament_preset }
var selectedNozzles  = new Set();
var paramData        = {};   // { preset_name: { params } }
var activeNozzleTab  = null;
var activeParamCat   = 'filament'; // 当前参数分类

$(document).ready(function () {
    if (typeof TranslatePage === 'function') TranslatePage();

    $('#btn-prev').on('click', function () { window.location.href = 'index.html' + getLangParam(); });
    $('#btn-cancel, #btn-close-window').on('click', function () {
        if (typeof SendWXMessage !== 'undefined')
            SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'close_page' }));
    });

    $('#btn-next').on('click', function () {
        if ($(this).prop('disabled')) return;
        var step1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
        var printerNozzles = [];
        // Use the exact printer/filament preset names C++ already sent (device_info's
        // nozzle_printers map, and the selected preset's nozzle_presets map) instead of
        // guessing them by regex-substituting the nozzle into a template name — a nozzle
        // without a real matching preset must be skipped, not sent as a guessed name.
        selectedNozzles.forEach(function (nozzle) {
            var printerName = (deviceInfo.nozzle_printers && deviceInfo.nozzle_printers[nozzle]) || '';
            var filamentPreset = (selectedPreset && selectedPreset.nozzle_presets && selectedPreset.nozzle_presets[nozzle]) || '';
            if (!printerName || !filamentPreset) return;
            printerNozzles.push({ printer: printerName, base_preset: filamentPreset });
        });
        var payload = {
            command: 'create_filament_confirm',
            mode: 'current_printer',
            vendor: step1.vendor || '',
            type:   step1.type   || '',
            serial: step1.serial || '',
            base_preset: selectedPreset ? selectedPreset.name : '',
            printer_nozzles: printerNozzles
        };
        sessionStorage.setItem('step2', JSON.stringify(payload));
        window.location.href = 'step3.html' + getLangParam();
    });

    // C++ 数据注入
    window.HandleStudio = function (msg) {
        try {
            var data = (typeof msg === 'string') ? JSON.parse(msg) : msg;
            if (data.command === 'device_info') {
                handleDeviceInfo(data);
            } else if (data.command === 'filament_params') {
                handleFilamentParams(data);
            }
        } catch (e) { console.error('HandleStudio error', e); }
    };

    // 请求设备信息
    var step1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
    if (typeof SendWXMessage !== 'undefined') {
        SendWXMessage(JSON.stringify({
            sequence_id: Math.round(Date.now() / 1000),
            command: 'get_device_info',
            type: step1.type || ''
        }));
    } else {
        // 浏览器预览 mock
        handleDeviceInfo({
            command: 'device_info',
            connected: true,
            device_name: 'My H2D',
            printer_model_id: 'BL-P004',
            printer_preset_base: 'Bambu Lab H2D 0.4 nozzle',
            nozzles: ['0.2', '0.4', '0.6', '0.8'],
            nozzle_printers: {
                '0.2': 'Bambu Lab H2D 0.2 nozzle', '0.4': 'Bambu Lab H2D 0.4 nozzle',
                '0.6': 'Bambu Lab H2D 0.6 nozzle', '0.8': 'Bambu Lab H2D 0.8 nozzle'
            },
            system_presets: [
                { name: 'Generic PETG', filament_preset: 'Generic PETG @BBL H2D 0.4 nozzle',
                  nozzle_presets: {
                      '0.2': 'Generic PETG @BBL H2D 0.2 nozzle', '0.4': 'Generic PETG @BBL H2D 0.4 nozzle',
                      '0.6': 'Generic PETG @BBL H2D 0.6 nozzle', '0.8': 'Generic PETG @BBL H2D 0.8 nozzle'
                  } },
                { name: 'Bambu PETG HF', filament_preset: 'Bambu PETG HF @BBL H2D 0.4 nozzle',
                  nozzle_presets: {
                      '0.2': 'Bambu PETG HF @BBL H2D 0.2 nozzle', '0.4': 'Bambu PETG HF @BBL H2D 0.4 nozzle',
                      '0.6': 'Bambu PETG HF @BBL H2D 0.6 nozzle', '0.8': 'Bambu PETG HF @BBL H2D 0.8 nozzle'
                  } }
            ]
        });
    }
});

function handleDeviceInfo(data) {
    if (!data.connected) {
        $('#device-name').text('No printer connected');
        $('#btn-next').prop('disabled', true);
        return;
    }
    deviceInfo = data;
    $('#device-name').text(data.device_name || data.printer_model_id || '');

    // 渲染喷嘴复选框（全选）
    var nozzlesHtml = '';
    (data.nozzles || []).forEach(function (n) {
        selectedNozzles.add(n);
        nozzlesHtml += '<label class="custom-checkbox">'
            + '<input type="checkbox" value="' + n + '" checked />'
            + '<span class="checkmark"></span>' + n + 'mm</label>';
    });
    $('#nozzle-checkboxes').html(nozzlesHtml);
    $('#nozzle-checkboxes input[type=checkbox]').on('change', function () {
        var v = $(this).val();
        if ($(this).prop('checked')) selectedNozzles.add(v);
        else selectedNozzles.delete(v);
        updateNextBtn();
        refreshParamTabs();
    });

    // 渲染基准预设下拉
    var presetsHtml = '';
    (data.system_presets || []).forEach(function (p, i) {
        presetsHtml += '<div class="dropdown-item" data-idx="' + i + '">' + p.name + '</div>';
    });
    $('#base-preset-options').html(presetsHtml || '<div class="dropdown-item disabled">No presets available</div>');

    $('#input-base-preset').on('click', function (e) {
        e.stopPropagation();
        $('#base-preset-dropdown').toggleClass('hidden');
    });
    $(document).on('click', function (e) {
        if (!$(e.target).closest('.input-wrapper').length)
            $('#base-preset-dropdown').addClass('hidden');
    });
    $(document).on('click', '#base-preset-options .dropdown-item:not(.disabled)', function () {
        var idx = $(this).data('idx');
        var p = data.system_presets[idx];
        selectedPreset = p;
        $('#input-base-preset').val(p.name);
        $('#base-preset-dropdown').addClass('hidden');
        updateNextBtn();
        var presetName = (p.nozzle_presets && p.nozzle_presets[activeNozzleTab])
            ? p.nozzle_presets[activeNozzleTab] : p.filament_preset;
        requestFilamentParams(presetName);
    });

    // 初始化喷嘴 tab（在 selectedNozzles 填好后）
    refreshParamTabs();

    // 默认选第一个预设
    if (data.system_presets && data.system_presets.length > 0) {
        var first = data.system_presets[0];
        selectedPreset = first;
        $('#input-base-preset').val(first.name);
        var initPreset = (first.nozzle_presets && first.nozzle_presets[activeNozzleTab])
            ? first.nozzle_presets[activeNozzleTab] : first.filament_preset;
        requestFilamentParams(initPreset);
    }

    updateNextBtn();
}

function requestFilamentParams(presetName) {
    if (!presetName) return;
    if (typeof SendWXMessage !== 'undefined') {
        SendWXMessage(JSON.stringify({
            sequence_id: Math.round(Date.now() / 1000),
            command: 'get_filament_params',
            preset: presetName,
            printer_preset: deviceInfo ? deviceInfo.printer_preset_base : ''
        }));
    } else {
        // mock
        handleFilamentParams({
            command: 'filament_params',
            preset: presetName,
            params: {
                filament_type: 'PETG', filament_vendor: 'Generic',
                filament_diameter: 1.75, filament_density: 1.27,
                filament_flow_ratio: 0.98, filament_max_volumetric_speed: 12,
                nozzle_temperature: 230, nozzle_temperature_initial_layer: 240,
                bed_temperature: 70, bed_temperature_initial_layer: 75,
                filament_shrink: '100%', default_filament_colour: '#FFFFFF'
            }
        });
    }
}

function handleFilamentParams(data) {
    if (!data.params) return;
    paramData[activeNozzleTab || '_current'] = data.params;
    renderParamList();
}

function refreshParamTabs() {
    var nozzleArr = Array.from(selectedNozzles).sort();
    if (!activeNozzleTab || !selectedNozzles.has(activeNozzleTab))
        activeNozzleTab = nozzleArr[0] || null;

    var tabHtml = '';
    nozzleArr.forEach(function (n) {
        var active = n === activeNozzleTab ? ' active' : '';
        tabHtml += '<div class="nozzle-tab' + active + '" data-nozzle="' + n + '">' + n + 'mm</div>';
    });
    $('#nozzle-tabs').html(tabHtml);
    $('#nozzle-tabs .nozzle-tab').on('click', function () {
        activeNozzleTab = $(this).attr('data-nozzle');
        // update active class without re-rendering tabs (avoids destroying event bindings)
        $('#nozzle-tabs .nozzle-tab').removeClass('active');
        $(this).addClass('active');
        // show cached params or request from C++
        if (paramData[activeNozzleTab]) {
            renderParamList();
        } else if (selectedPreset) {
            var presetName = (selectedPreset.nozzle_presets && selectedPreset.nozzle_presets[activeNozzleTab])
                ? selectedPreset.nozzle_presets[activeNozzleTab] : selectedPreset.filament_preset;
            requestFilamentParams(presetName);
        }
    });
}

function renderParamList() {
    var p = paramData[activeNozzleTab] || paramData['_current'];
    if (!p) {
        $('#param-list').html('<div class="param-row" style="color:#999">Please select a base preset first</div>');
        return;
    }

    var PARAM_CATS = {
        filament: [
            { key: 'filament_type',                 label: 'Type' },
            { key: 'filament_vendor',               label: 'Vendor' },
            { key: 'filament_diameter',             label: 'Diameter (mm)' },
            { key: 'filament_density',              label: 'Density (g/cm³)' },
            { key: 'filament_flow_ratio',           label: 'Flow Ratio' },
            { key: 'filament_max_volumetric_speed', label: 'Max Volumetric Speed' },
            { key: 'filament_shrink',               label: 'Shrinkage' },
            { key: 'default_filament_colour',       label: 'Default Color' },
        ],
        temperature: [
            { key: 'nozzle_temperature',              label: 'Nozzle Temp (°C)' },
            { key: 'nozzle_temperature_initial_layer',label: 'Initial Layer Nozzle Temp (°C)' },
            { key: 'bed_temperature',                 label: 'Bed Temp (°C)' },
            { key: 'bed_temperature_initial_layer',   label: 'Initial Layer Bed Temp (°C)' },
        ]
    };

    var cats = PARAM_CATS[activeParamCat] || PARAM_CATS['filament'];
    var html = '';
    cats.forEach(function (item) {
        var val = p[item.key];
        if (val === undefined || val === null || val === '') val = '—';
        if (item.key === 'default_filament_colour' && val !== '—') {
            val = '<span style="display:inline-flex;align-items:center;gap:6px;">'
                + '<span style="display:inline-block;width:14px;height:14px;border-radius:3px;background:' + val
                + ';border:1px solid #ccc"></span>' + val + '</span>';
        }
        html += '<div class="param-row"><span class="p-name">' + item.label
              + '</span><span class="p-val">' + val + '</span></div>';
    });
    $('#param-list').html(html);

    // 渲染参数分类 tab
    var catLabels = { filament: 'Filament', temperature: 'Temperature' };
    var catHtml = '';
    Object.keys(catLabels).forEach(function (k) {
        var active = k === activeParamCat ? ' active' : '';
        catHtml += '<div class="param-category' + active + '" data-cat="' + k + '">' + catLabels[k] + '</div>';
    });
    $('#param-categories').html(catHtml);
    $('#param-categories .param-category').on('click', function () {
        activeParamCat = $(this).data('cat');
        renderParamList();
    });
}

function updateNextBtn() {
    var ok = deviceInfo && deviceInfo.connected && selectedNozzles.size > 0 && selectedPreset;
    $('#btn-next').prop('disabled', !ok);
}
