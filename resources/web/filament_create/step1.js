
const VENDOR_LIST = ['Polymaker', 'OVERTURE', 'Kexcelled', 'HATCHBOX', 'eSUN', 'SUNLU', 'Prusament', 'Creality', 'Protopasta', 'Anycubic', 'Basf', 'ELEGOO', 'INLAND', 'FLASHFORGE', 'FusRock', 'AMOLEN', 'MIKA3D', '3DXTECH', 'Duramic', 'Priline', 'Eryone', '3Dgenius', 'Novamaker', 'Justmaker', 'Giantarm', 'iProspect', 'LDO'];
var TYPE_LIST = []; // filled dynamically from C++ init_data
var SUPPORTED_TYPES = null; // filled from C++ get_supported_types; only meaningful for current_printer mode
let customVendors = [];

function escapeHtml(str) {
    return String(str).replace(/[&<>"']/g, function (c) {
        return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
}

$(document).ready(function () {
    if (typeof TranslatePage === 'function') TranslatePage();

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
        html += '<div class="dropdown-category">Customized</div>';
        if (customVendors.length > 0) {
            customVendors.forEach(v => { html += `<div class="dropdown-item" data-val="${escapeHtml(v)}">${escapeHtml(v)}</div>`; });
        } else {
            html += `<div class="dropdown-item disabled">Custom material</div>`;
        }
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
                    if (typeof SendWXMessage !== 'undefined') {
                        SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'get_supported_types' }));
                    }
                } else {
                    SUPPORTED_TYPES = null;
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

    // Only the current_printer mode is restricted to what the connected printer model
    // actually supports (see get_supported_types) — other modes browse the full type list.
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
    });

    $('#vendor-dialog-close, #vendor-btn-cancel').on('click', function() {
        $('#vendor-dialog-mask, #vendor-dialog').addClass('hidden');
    });

    $('#new-vendor-input').on('input', function() {
        const val = $(this).val();
        $('#vendor-char-count').text(val.length + '/50');
        if (val.length === 50) {
            $('#new-vendor-error').text('Max 50 characters').removeClass('hidden');
            $(this).addClass('error');
        } else {
            $('#new-vendor-error').addClass('hidden');
            $(this).removeClass('error');
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
