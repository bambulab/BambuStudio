// edit_filament.js
// 数据由 C++ 通过 HandleStudio({ command: 'filament_edit_data', ... }) 推入
// 结构: { filament_name, presets: [ { printer, base_preset_name, preset_name, series } ] }

var g_filamentName = '';
var g_presets = [];  // [{ printer, base_preset_name, preset_name, series }]

function GetTextByTid(tid) {
    if (typeof LangText === 'undefined') return null;
    var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
             || (typeof localStorage !== 'undefined' ? localStorage.getItem('BambuWebLang') : null)
             || 'en';
    if (!LangText.hasOwnProperty(lang)) lang = 'en';
    return LangText[lang][tid] || LangText['en'][tid] || null;
}

$(document).ready(function () {
    if (typeof TranslatePage === 'function') TranslatePage();

    $('#btn-ok').on('click', function () {
        send({ command: 'edit_filament_ok' });
    });

    $('#btn-cancel').on('click', function () {
        send({ command: 'close_page' });
    });

    $('#btn-add-preset').on('click', function () {
        send({ command: 'edit_filament_add_preset' });
    });

    window.HandleStudio = function (msg) {
        try {
            var data = (typeof msg === 'string') ? JSON.parse(msg) : msg;
            if (data.command === 'filament_edit_data') {
                g_filamentName = data.filament_name || '';
                g_presets = data.presets || [];
                render();
            }
        } catch (e) { console.error('HandleStudio error', e); }
    };

    if (typeof SendWXMessage !== 'undefined') {
        send({ command: 'request_filament_edit_data' });
    } else {
        // 浏览器预览 mock
        g_filamentName = 'Polymaker PETG Basic';
        g_presets = [
            { printer: 'Bambu Lab H2D 0.2 nozzle', base_preset_name: 'Bambu PETG HF @BBL H2D 0.2 nozzle', preset_name: 'Polymaker PETG Basic @Bambu Lab H2D 0.2 nozzle', series: 'H Series' },
            { printer: 'Bambu Lab H2D 0.4 nozzle', base_preset_name: 'Bambu PETG HF @BBL H2D',             preset_name: 'Polymaker PETG Basic @Bambu Lab H2D 0.4 nozzle', series: 'H Series' },
            { printer: 'Bambu Lab P1S 0.4 nozzle', base_preset_name: 'Bambu PETG HF @BBL P1S',             preset_name: 'Polymaker PETG Basic @Bambu Lab P1S 0.4 nozzle', series: 'P Series' },
            { printer: 'Bambu Lab X1C 0.4 nozzle', base_preset_name: 'Bambu PETG HF @BBL X1C',             preset_name: 'Polymaker PETG Basic @Bambu Lab X1C 0.4 nozzle', series: 'X Series' },
        ];
        render();
    }
});

function send(obj) {
    obj.sequence_id = Math.round(Date.now() / 1000);
    if (typeof SendWXMessage !== 'undefined')
        SendWXMessage(JSON.stringify(obj));
}

function render() {
    $('#ef-filament-name').text(g_filamentName);

    if (g_presets.length === 0) {
        $('#ef-preset-list').html('');
        $('#ef-empty').show();
        return;
    }
    $('#ef-empty').hide();

    var seriesWord = GetTextByTid('t248') || 'Series';
    var otherWord  = GetTextByTid('t249') || 'Other';

    function seriesLabel(key) {
        return key ? key + ' ' + seriesWord : otherWord;
    }

    // Group by series key (e.g. "H", "P", "X", "A", "")
    var groups = {};
    var order = [];
    g_presets.forEach(function (p) {
        var s = p.series || '';
        if (!groups[s]) { groups[s] = []; order.push(s); }
        groups[s].push(p);
    });

    var html = '';
    order.forEach(function (series) {
        var items = groups[series];
        var label = seriesLabel(series);
        html += '<div class="CFilament_GroupTitle collapsible" onclick="toggleGroup(this)">'
              + '<span class="CFilament_GroupArrow"></span>'
              + escapeHtml(label) + ' (' + items.length + ')'
              + '</div>';
        html += '<div class="CFilament_GroupItems">';
        items.forEach(function (p, i) {
            var idx = g_presets.indexOf(p);
            // 显示喷嘴规格：取倒数第二个 token（如 "0.4"）
            var parts = (p.printer || '').split(' ');
            var nozzleLabel = parts.length >= 2 ? parts[parts.length - 2] + ' nozzle' : p.printer;
            // 机型名 = 去掉最后两个 token
            var modelName = parts.length >= 3 ? parts.slice(0, parts.length - 2).join(' ') : p.printer;

            html += '<div class="ef-item">'
                  + '<span class="ef-item-printer">' + escapeHtml(modelName + ' ' + nozzleLabel) + '</span>'
                  + '<span class="ef-item-base-preset">' + escapeHtml(p.display_name || p.base_preset_name || '') + '</span>'
                  + '<div class="ef-item-actions">'
                  + '<button class="ef-btn-action" onclick="onEditPreset(' + idx + ')">' + (GetTextByTid('t266') || 'Edit Preset') + '</button>'
                  + '<button class="ef-btn-action remove" onclick="onRemovePreset(' + idx + ')">' + (GetTextByTid('t267') || 'Remove') + '</button>'
                  + '</div>'
                  + '</div>';
        });
        html += '</div>';
    });

    $('#ef-preset-list').html(html);
}

function toggleGroup(el) {
    var $items = $(el).next('.CFilament_GroupItems');
    var $arrow = $(el).find('.CFilament_GroupArrow');
    $items.toggleClass('collapsed');
    $arrow.toggleClass('collapsed');
}

function onEditPreset(idx) {
    var p = g_presets[idx];
    if (!p) return;
    send({ command: 'edit_filament_edit_preset', preset_name: p.preset_name });
}

function onRemovePreset(idx) {
    var p = g_presets[idx];
    if (!p) return;
    // C++ shows a Yes/No confirm before actually deleting, and pushes a fresh
    // filament_edit_data afterwards regardless of the answer — don't remove the
    // row here, or it would disappear even when the user clicks "No".
    send({ command: 'edit_filament_remove_preset', preset_name: p.preset_name });
}

function escapeHtml(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}
