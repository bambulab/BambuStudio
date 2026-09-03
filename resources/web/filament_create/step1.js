
const VENDOR_LIST = ['Polymaker', 'OVERTURE', 'Kexcelled', 'HATCHBOX', 'eSUN', 'SUNLU', 'Prusament', 'Creality', 'Protopasta', 'Anycubic', 'Basf', 'ELEGOO', 'INLAND', 'FLASHFORGE', 'FusRock', 'AMOLEN', 'MIKA3D', '3DXTECH', 'Duramic', 'Priline', 'Eryone', '3Dgenius', 'Novamaker', 'Justmaker', 'Giantarm', 'iProspect', 'LDO'];
var TYPE_LIST = []; // filled dynamically from C++ init_data
// Types the connected printer actually has compatible presets for — filled from C++
// get_supported_types (see send_supported_types in CreateFilamentWebDialog.cpp).
// Only used in current_printer mode; null means "no signal yet, don't filter".
var SUPPORTED_TYPES = null;
let customVendors = [];

function escapeHtml(str) {
    return String(str).replace(/[&<>"']/g, function (c) {
        return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
}

// Hover text for the "为当前打印机创建" radio card:
//   - No printer connected  → t272 "暂无打印机，请连接打印机" / "No printer connected. Please connect a printer."
//   - Printer connected     → t273 prefix + printer name
// Uses the native title attribute so it works even when the card is disabled and
// pointer-events would normally block interaction on child elements.
function updateCurrentPrinterTooltip(connected, deviceName) {
    var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
             || localStorage.getItem('BambuWebLang') || 'en';
    if (typeof LangText === 'undefined' || !LangText.hasOwnProperty(lang)) lang = 'en';
    function _t(tid) { return (LangText[lang] && LangText[lang][tid]) || (LangText['en'] && LangText['en'][tid]) || ''; }
    var tip = connected
        ? (_t('t273') + (deviceName || ''))
        : _t('t272');
    // Use a data attribute + CSS pseudo-element instead of the native `title` so we can
    // style the tooltip (rounded, no OS-chrome border). See step1.css .tooltip-hint rules.
    $('#opt-current-printer').attr('data-tooltip', tip);
}

$(document).ready(function () {
    if (typeof TranslatePage === 'function') TranslatePage();

    // Landing on step1 always drops any lingering step2 selection. That way the step2
    // restore we do on Back-from-step3 only survives the step3 → step2 round-trip;
    // any other entry into step2 (fresh open, or step2 → step1 → step2) starts clean.
    sessionStorage.removeItem('step2');

    // ── 下一步按鈕禁用逻辑：vendor + type + serial 全填完才可点 ──
    function updateNextBtn() {
        const vendor = ($('#input-vendor').val() || '').trim();
        const type   = ($('#input-type').val()   || '').trim();
        const serial = ($('#input-series').val()  || '').trim();
        $('#btn-next').prop('disabled', !(vendor && type && serial));
    }
    // 监听三个字段变化
    $('#input-vendor, #input-type').on('change', updateNextBtn);
    $('#input-series').on('input', updateNextBtn);
    // vendor/type 是点击下拉选的，监听自定义 input 事件
    $(document).on('input-changed', updateNextBtn);
    // ── 回填 sessionStorage 中保存的数据（在所有事件绑定之前）──
    var _s1 = JSON.parse(sessionStorage.getItem('step1') || '{}');
    if (_s1.vendor) $('#input-vendor').val(_s1.vendor);
    if (_s1.type)   $('#input-type').val(_s1.type);
    if (_s1.serial) $('#input-series').val(_s1.serial);
    if (_s1.mode) {
        $('.radio-card').removeClass('active');
        $('.radio-card input[name="creation_mode"][value="' + _s1.mode + '"]')
            .closest('.radio-card').addClass('active');
    }
    updateNextBtn(); // 回填后立即检查按鈕状态

    // Check if printer is connected — disabled by default until C++ confirms
    $('#opt-current-printer').addClass('disabled');
    $('#opt-current-printer input').prop('disabled', true);
    // Prime the hover tooltip so it says the right thing before device_status arrives.
    updateCurrentPrinterTooltip(false, '');

    // Radio card selection logic
    $('.radio-card').on('click', function(e) {
        if ($(this).hasClass('disabled')) {
            e.preventDefault();
            return;
        }
        $('.radio-card').removeClass('active');
        $(this).addClass('active');
        $(this).find('input').prop('checked', true);
        refreshTypeDropdown();
    });

    // Close window
    $('#btn-cancel, #btn-close-window').on('click', function() {
        if (typeof SendWXMessage !== 'undefined') {
            SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: "close_page" }));
        }
    });

    // Next step -> route based on selected creation mode
    $('#btn-next').on('click', function() {
        const vendor = ($('#input-vendor').val() || '').trim();
        const type   = ($('#input-type').val()   || '').trim();
        const serial = ($('#input-series').val()  || '').trim();

        // 读 active card 里的 input value
        const mode = $('.radio-card.active').find('input[name="creation_mode"]').val();

        // Save step1 data for later steps（含 mode，回退时可恢复）
        sessionStorage.setItem('step1', JSON.stringify({ vendor, type, serial: serial, mode }));

        var lang = (typeof GetQueryString === 'function' ? GetQueryString('lang') : null)
                 || localStorage.getItem('BambuWebLang') || '';
        var langParam = lang ? '?lang=' + lang : '';
        if (mode === 'current_printer') {
            window.location.href = 'step2.html' + langParam;
        } else if (mode === 'copy_presets') {
            window.location.href = 'step2_copy.html' + langParam;
        } else {
            window.location.href = 'step2_type.html' + langParam;
        }
    });

    // Dropdown Toggles
    $('#input-vendor').on('click', function(e) {
        e.stopPropagation();
        $('#vendor-dropdown-list').toggleClass('hidden');
        $('#type-dropdown-list').addClass('hidden');
    });

    $('#input-type').on('click', function(e) {
        e.stopPropagation();
        $('#type-dropdown-list').toggleClass('hidden');
        $('#vendor-dropdown-list').addClass('hidden');
    });

    $(document).on('click', function(e) {
        if (!$(e.target).closest('.input-wrapper').length) {
            $('.dropdown-list').addClass('hidden');
        }
    });

    function renderVendors() {
        let html = '<div class="dropdown-category">System</div>';
        VENDOR_LIST.forEach(v => { html += `<div class="dropdown-item" data-val="${escapeHtml(v)}">${escapeHtml(v)}</div>`; });
        // Append any user-added custom vendors flat at the tail — no separate section header,
        // no placeholder row when empty. "+ 添加供应商品牌" below the list already conveys
        // where to add more, so the extra "Customized" divider was just visual noise.
        customVendors.forEach(v => { html += `<div class="dropdown-item" data-val="${escapeHtml(v)}">${escapeHtml(v)}</div>`; });
        $('#vendor-options').html(html);
    }
    renderVendors();
    renderTypes();

    // 向 C++ 请求动态类型列表
    window.HandleStudio = function (msg) {
        try {
            var data = (typeof msg === 'string') ? JSON.parse(msg) : msg;
            if (data.command === 'init_data') {
                TYPE_LIST = data.types || [];
                refreshTypeDropdown();
                var selectedVendor = (data.selected_vendor || '').trim();
                var selectedType   = (data.selected_type || '').trim();
                var selectedSerial = (data.selected_serial || '').trim();
                if (selectedVendor || selectedType || selectedSerial) {
                    if (selectedVendor) {
                        var knownVendors = VENDOR_LIST.concat(customVendors);
                        if (knownVendors.indexOf(selectedVendor) < 0) {
                            customVendors.push(selectedVendor);
                            renderVendors();
                        }
                    }
                    if (selectedType && TYPE_LIST.indexOf(selectedType) < 0) {
                        TYPE_LIST.push(selectedType);
                        refreshTypeDropdown();
                    }
                    $('#input-vendor').val(selectedVendor);
                    $('#input-type').val(selectedType);
                    $('#input-series').val(selectedSerial);
                    sessionStorage.removeItem('step1');
                }
                updateNextBtn();
            } else if (data.command === 'device_status') {
                if (data.connected) {
                    $('#opt-current-printer').removeClass('disabled');
                    $('#opt-current-printer input').prop('disabled', false);
                    var label = $('#opt-current-printer .device-name');
                    if (label.length) label.text(data.device_name || '');
                    updateCurrentPrinterTooltip(true, data.device_name || '');
                    // First-visit default: with a printer connected, the "current printer"
                    // mode is the most useful landing choice. Only preselect it if the user
                    // has no saved mode from a previous visit — otherwise respect their
                    // earlier pick (sessionStorage restore ran synchronously at page load).
                    var savedMode = (JSON.parse(sessionStorage.getItem('step1') || '{}').mode || '');
                    if (!savedMode) {
                        $('.radio-card').removeClass('active');
                        $('.radio-card input[name="creation_mode"]').prop('checked', false);
                        $('#opt-current-printer').addClass('active');
                        $('#opt-current-printer input').prop('checked', true);
                        refreshTypeDropdown();
                    }
                    if (typeof SendWXMessage !== 'undefined') {
                        SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'get_supported_types' }));
                    }
                } else {
                    SUPPORTED_TYPES = null;
                    refreshTypeDropdown();
                    updateCurrentPrinterTooltip(false, '');
                }
            } else if (data.command === 'supported_types') {
                SUPPORTED_TYPES = data.types || [];
                refreshTypeDropdown();
            }
        } catch(e) { console.error('HandleStudio error', e); }
    };
    if (typeof SendWXMessage !== 'undefined') {
        SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'request_init_data' }));
        SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'get_device_status' }));
    }

    $(document).on('click', '#vendor-options .dropdown-item:not(.disabled)', function() {
        $('#input-vendor').val($(this).data('val'));
        $('#vendor-dropdown-list').addClass('hidden');
        updateNextBtn();
    });

    function renderTypes(list) {
        var typeHtml = '';
        (list || TYPE_LIST).forEach(function(t) { typeHtml += '<div class="dropdown-item" data-val="' + t + '">' + t + '</div>'; });
        $('#type-options').html(typeHtml || '<div class="dropdown-item disabled">No data</div>');
    }

    // Filter the Type dropdown to what the connected printer's compatible presets cover —
    // only in current_printer mode (the other two modes are printer-agnostic at Type-select
    // time). NOTE: the C++ side deliberately does NOT gate on preset visibility; hiding a
    // system material in the panel must not shrink this list. See send_supported_types().
    function refreshTypeDropdown() {
        var mode = $('.radio-card.active').find('input[name="creation_mode"]').val();
        var list = TYPE_LIST;
        if (mode === 'current_printer' && SUPPORTED_TYPES !== null) {
            list = TYPE_LIST.filter(function(t) { return SUPPORTED_TYPES.indexOf(t) !== -1; });
        }
        renderTypes(list);
        var cur = ($('#input-type').val() || '').trim();
        if (cur && list.indexOf(cur) === -1) {
            $('#input-type').val('');
        }
        updateNextBtn();
    }

    $(document).on('click', '#type-options .dropdown-item', function() {
        $('#input-type').val($(this).data('val'));
        $('#type-dropdown-list').addClass('hidden');
        updateNextBtn();
    });

    // Vendor Dialog
    $('#add-vendor-btn').on('click', function(e) {
        e.stopPropagation();
        $('#vendor-dropdown-list').addClass('hidden');
        $('#vendor-dialog-mask, #vendor-dialog').removeClass('hidden');
        $('#new-vendor-input').val('');
        $('#vendor-char-count').text('0/50');
        $('#new-vendor-error').text('').addClass('hidden');
        $('#new-vendor-input').removeClass('error');
        $('#vendor-btn-confirm').prop('disabled', false);
    });

    $('#vendor-dialog-close, #vendor-btn-cancel').on('click', function() {
        $('#vendor-dialog-mask, #vendor-dialog').addClass('hidden');
    });

    $('#new-vendor-input').on('input', function() {
        const val = $(this).val();
        $('#vendor-char-count').text(val.length + '/50');
        // maxlength on the input is 51, one over the actual limit, so the user gets to
        // TYPE the 51st char and see explicit feedback ("Max 50 characters" + Confirm
        // greyed out) that they've overshot. Deleting back to 50 or less makes the
        // Confirm button usable again without them having to guess "why can't I type".
        if (val.length > 50) {
            $('#new-vendor-error').text('Max 50 characters').removeClass('hidden');
            $(this).addClass('error');
            $('#vendor-btn-confirm').prop('disabled', true);
        } else {
            $('#new-vendor-error').addClass('hidden');
            $(this).removeClass('error');
            $('#vendor-btn-confirm').prop('disabled', false);
        }
    });

    $('#vendor-btn-confirm').on('click', function() {
        const val = $('#new-vendor-input').val().trim();
        const errorEl = $('#new-vendor-error');
        const inputEl = $('#new-vendor-input');
        if (!val) {
            errorEl.text('Please enter a name').removeClass('hidden');
            inputEl.addClass('error');
            return;
        }
        const isDuplicate = VENDOR_LIST.concat(customVendors).some(v => v.toLowerCase() === val.toLowerCase());
        if (isDuplicate) {
            errorEl.text('Name already exists').removeClass('hidden');
            inputEl.addClass('error');
            return;
        }
        customVendors.push(val);
        renderVendors();
        $('#input-vendor').val(val);
        $('#vendor-dialog-mask, #vendor-dialog').addClass('hidden');
    });
});
