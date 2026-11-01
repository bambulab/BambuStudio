
// Dropdown Data
const VENDOR_LIST = ['Polymaker', 'OVERTURE', 'Kexcelled', 'HATCHBOX', 'eSUN', 'SUNLU', 'Prusament', 'Creality', 'Protopasta', 'Anycubic', 'Basf', 'ELEGOO', 'INLAND', 'FLASHFORGE', 'FusRock', 'AMOLEN', 'MIKA3D', '3DXTECH', 'Duramic', 'Priline', 'Eryone', '3Dgenius', 'Novamaker', 'Justmaker', 'Giantarm', 'iProspect', 'LDO'];
const TYPE_LIST = ['PLA', 'PLA+', 'PLA Tough', 'PETG', 'ABS', 'ASA', 'FLEX', 'HIPS', 'PA', 'PACF', 'NYLON', 'PVA', 'PC', 'PCABS', 'PCTG', 'PCCF', 'PP', 'PEI', 'PET', 'PETG', 'PETGCF', 'PTBA', 'PTBA90A', 'PEEK', 'TPU93A', 'TPU75D', 'TPU', 'TPU-AMS', 'TPU92A', 'TPU98A', 'Misc', 'TPE', 'GLAZE', 'Nylon', 'CPE', 'METAL', 'ABST', 'Carbon Fiber'];
let customVendors = [];

$(document).ready(function() {
    
    // Check if printer is connected (simulated state for UI)
    var isPrinterConnected = false; // TODO: Get real status from C++
    
    if (!isPrinterConnected) {
        $('#opt-current-printer').addClass('disabled');
        $('#opt-current-printer input').prop('disabled', true);
    }

    // Radio card selection logic
    $('.radio-card').on('click', function(e) {
        if ($(this).hasClass('disabled')) {
            e.preventDefault();
            return;
        }
        $('.radio-card').removeClass('active');
        $(this).addClass('active');
        $(this).find('input').prop('checked', true);
    });

    // Close window
    $('#btn-cancel, #btn-close-window').on('click', function() {
        if (typeof SendWXMessage !== 'undefined') {
            SendWXMessage(JSON.stringify({
                sequence_id: Math.round(Date.now() / 1000),
                command: "close_page"
            }));
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

    // Populate Vendor Dropdown
    function renderVendors() {
        let html = '';
        const allVendors = VENDOR_LIST.concat(customVendors);
        
        // System vendors
        html += '<div class="dropdown-category">System</div>';
        VENDOR_LIST.forEach(v => {
            html += `<div class="dropdown-item" data-val="${v}">${v}</div>`;
        });
        
        // Custom vendors
        html += '<div class="dropdown-category">Customized</div>';
        if (customVendors.length > 0) {
            customVendors.forEach(v => {
                html += `<div class="dropdown-item" data-val="${v}">${v}</div>`;
            });
        } else {
            html += `<div class="dropdown-item disabled">用户自定义的材料</div>`;
        }
        
        $('#vendor-options').html(html);
    }
    renderVendors();

    // Populate Type Dropdown
    let typeHtml = '';
    TYPE_LIST.forEach(t => {
        typeHtml += `<div class="dropdown-item" data-val="${t}">${t}</div>`;
    });
    $('#type-options').html(typeHtml);

    // Dropdown Selection
    $(document).on('click', '#vendor-options .dropdown-item:not(.disabled)', function() {
        $('#input-vendor').val($(this).data('val'));
        $('#vendor-dropdown-list').addClass('hidden');
    });

    $(document).on('click', '#type-options .dropdown-item', function() {
        $('#input-type').val($(this).data('val'));
        $('#type-dropdown-list').addClass('hidden');
    });

    // Add Custom Vendor
    $('#add-vendor-btn').on('click', function(e) {
        e.stopPropagation();
        $('#vendor-dropdown-list').addClass('hidden');
        $('#vendor-dialog-mask, #vendor-dialog').removeClass('hidden');
        $('#new-vendor-input').val('');
        
        // Count update
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
            $('#new-vendor-error').text('最多支持50个字符').removeClass('hidden');
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
            errorEl.text('请输入名称').removeClass('hidden');
            inputEl.addClass('error');
            return;
        }
        if (val.length > 50) {
            errorEl.text('最多支持50个字符').removeClass('hidden');
            inputEl.addClass('error');
            return;
        }
        
        // Case-insensitive check for duplicates
        const valLower = val.toLowerCase();
        const isDuplicate = VENDOR_LIST.concat(customVendors).some(v => v.toLowerCase() === valLower);
        
        if (isDuplicate) {
            errorEl.text('不支持重复的命名').removeClass('hidden');
            inputEl.addClass('error');
            return;
        }

        customVendors.push(val);
        renderVendors();
        $('#input-vendor').val(val);
        $('#vendor-dialog-mask, #vendor-dialog').addClass('hidden');
    });

    
    // Request device status from C++ on load
    window.HandleStudio = function (msg) {
        try {
            var data = (typeof msg === 'string') ? JSON.parse(msg) : msg;
            if (data.command === 'device_status') {
                if (data.connected) {
                    $('#opt-current-printer').removeClass('disabled');
                    $('#opt-current-printer input').prop('disabled', false);
                    $('#opt-current-printer .device-name').text(data.device_name || '');
                }
            }
        } catch (e) { console.error('HandleStudio error', e); }
    };
    if (typeof SendWXMessage !== 'undefined') {
        SendWXMessage(JSON.stringify({ sequence_id: Math.round(Date.now() / 1000), command: 'get_device_status' }));
    }

    $('#btn-next').on('click', function () {
        var vendor = $('#input-vendor').val();
        var type   = $('#input-type').val();
        var serial = $('#input-series').val();
        var mode   = $('input[name="creation_mode"]:checked').val();

        if (!vendor || !type) return;

        var step1 = { vendor: vendor, type: type, serial: serial, mode: mode };
        sessionStorage.setItem('step1', JSON.stringify(step1));

        if (mode === 'current_printer') {
            window.location.href = 'step2.html';
        } else if (mode === 'based_on_type') {
            window.location.href = 'step2_type.html';
        } else if (mode === 'copy_presets') {
            window.location.href = 'step2_copy.html';
        }
    });
});
