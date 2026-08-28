
function getLangParam() {
    var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
             || localStorage.getItem('BambuWebLang') || '';
    return lang ? '?lang=' + lang : '';
}

function _t3(tid) {
    if (typeof LangText === 'undefined') return null;
    var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
             || localStorage.getItem('BambuWebLang') || 'en';
    if (!LangText.hasOwnProperty(lang)) lang = 'en';
    return (LangText[lang] && LangText[lang][tid]) || (LangText['en'] && LangText['en'][tid]) || null;
}
$(document).ready(function () {
    if (typeof TranslatePage === 'function') TranslatePage();

    // 上一步返回对应的第二页
    $('#btn-prev').on('click', function () {
        var step2 = JSON.parse(sessionStorage.getItem('step2') || '{}');
        var mode = step2.mode || 'based_on_type';
        var page = mode === 'copy_presets' ? 'step2_copy.html'
                 : mode === 'current_printer' ? 'step2.html'
                 : 'step2_type.html';
        window.location.href = page + getLangParam();
    });

    $('#btn-cancel, #btn-close-window').on('click', function () {
        if (typeof SendWXMessage !== 'undefined')
            SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'close_page' }));
    });

    // 确定：发送创建命令给 C++
    $('#btn-confirm').on('click', function () {
        var $btn = $(this);
        $btn.data('orig-text', $btn.text());
        $btn.prop('disabled', true).text(_t3('t258') || 'Creating...');
        $('#btn-prev, #btn-cancel').prop('disabled', true);

        var step2 = JSON.parse(sessionStorage.getItem('step2') || '{}');
        var payload = JSON.stringify(
            Object.assign({ sequence_id: Math.round(Date.now() / 1000) }, step2)
        );
        if (typeof SendWXMessage === 'function') {
            SendWXMessage(payload);
        } else if (window.wx && window.wx.postMessage) {
            window.wx.postMessage(payload);
        }
    });

    window.HandleStudio = function (msg) {
        try {
            var data = (typeof msg === 'string') ? JSON.parse(msg) : msg;
            if (data.command === 'create_success') {
                showSuccess(data.need_sync || false);
            } else if (data.command === 'create_fail') {
                var $btn = $('#btn-confirm');
                $btn.prop('disabled', false).text($btn.data('orig-text') || 'Confirm');
                $('#btn-prev, #btn-cancel').prop('disabled', false);
            }
        } catch (e) { console.error('HandleStudio error', e); }
    };

    // 渲染页面内容
    renderSummary();
});

function renderSummary() {
    var step1  = JSON.parse(sessionStorage.getItem('step1')  || '{}');
    var step2  = JSON.parse(sessionStorage.getItem('step2')  || '{}');

    // 材料名称
    var name = [step1.vendor, step1.type, step1.serial].filter(Boolean).join(' ');
    $('#s3-filament-name').text(name || '—');

    // 打印机预设列表
    var printerNozzles = step2.printer_nozzles || [];

    // 按机型分组：{ modelName: [nozzleLabel, ...] }
    var groups = {};
    var order  = [];
    printerNozzles.forEach(function (item) {
        var presetName = item.printer; // 完整预设名，如 "Bambu Lab H2D 0.4 nozzle"
        // 机型 = 去掉最后两个 token（"0.4 nozzle"）
        var parts = presetName.split(' ');
        var nozzleLabel = parts.length >= 2
            ? parts[parts.length - 2]          // "0.4"
            : presetName;
        var model = parts.length >= 3
            ? parts.slice(0, parts.length - 2).join(' ')  // "Bambu Lab H2D"
            : presetName;
        if (!groups[model]) { groups[model] = []; order.push(model); }
        groups[model].push(nozzleLabel);
    });

    var total = printerNozzles.length;
    $('#s3-count').text(total ? ' (' + total + ')' : '');

    // 两列布局：每个机型占一个单元格
    var html = '';
    order.forEach(function (model) {
        var nozzles = groups[model];
        var nozzleStr = nozzles.join(' / ');
        html += '<div class="s3-preset-item">'
              + model + ' (' + nozzles.length + ': ' + nozzleStr + 'mm)'
              + '</div>';
    });
    $('#s3-preset-list').html(html || '<div class="s3-preset-item" style="color:#999">' + (_t3('t259') || 'No data') + '</div>');
}

function showSuccess(needSync) {
    // 隐藏确认前的内容，显示成功状态
    $('.s3-row, #s3-preset-list').hide();
    $('#btn-prev, #btn-confirm').hide();

    var syncText = needSync
        ? (_t3('t260') || 'User preset sync is not enabled, which may cause filament settings on the device page to not take effect.\nClick "Sync user presets" to enable sync.')
        : (_t3('t261') || 'Go to the filament settings page to edit your preset if needed.\nNote: Nozzle temperature, bed temperature, and max volumetric speed significantly affect print quality. Please set them carefully.');

    var step1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
    var name = [step1.vendor, step1.type, step1.serial].filter(Boolean).join(' ');

    var html = '<div class="success-area">'
        + '<div class="success-title">' + (_t3('t262') || 'Filament Created') + '</div>'
        + '<div class="success-name">' + (name || '') + '</div>'
        + '<div class="success-desc">' + syncText.replace(/\n/g, '<br>') + '</div>'
        + '</div>';
    $('#s3-preset-list').after(html).parent().find('.success-area').show();

    // 更新底部按钮
    var btnLabel = needSync ? (_t3('t263') || 'Sync user presets') : (_t3('t264') || 'OK');
    var $newBtn = $('<button class="btn-primary" id="btn-success-ok">' + btnLabel + '</button>');
    $('#btn-cancel').before($newBtn);
    $('#btn-cancel').prop('disabled', false).text(_t3('t265') || 'Close');

    $('#btn-success-ok').on('click', function () {
        var cmd = needSync ? 'sync_and_close' : 'success_close';
        if (typeof SendWXMessage !== 'undefined')
            SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: cmd }));
    });
    $('#btn-cancel').off('click').on('click', function () {
        if (typeof SendWXMessage !== 'undefined')
            SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'success_close' }));
    });
}
