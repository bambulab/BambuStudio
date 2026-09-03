
/* =====================================================================
 *  23.js - System & Custom Filament Selection (Refactored)
 *  Compatible with the same C++ backend commands:
 *    request_userguide_profile / response_userguide_profile
 *    save_userguide_filaments / user_guide_finish / user_guide_cancel
 *    request_custom_filaments / update_custom_filaments
 *    create_custom_filament / modify_custom_filament
 * ===================================================================== */

var m_ProfileItem = null;

// Parsed data
var g_models = [];       // [{model, nozzle_selected, vendor, materials}]
var g_filaments = [];    // [{key, name, shortName, vendor, type, models, selected, filalist}]
var g_printerList = [];  // unique printer names
var g_vendorList = [];   // unique vendor names
var g_typeList = [];     // unique type names

// Filter state: selected items (empty = all selected)
var g_selectedPrinters = [];  // selected printer model names
var g_selectedVendors = [];   // selected vendor names
var g_selectedTypes = [];     // selected type names

// UI state
var g_searchKeywords = []; // active search keyword tags

var FilamentPriority = ["PLA", "ABS", "PETG", "PET", "TPU", "PC", "PA", "ASA"];
var VendorPriority = ["Bambu Lab", "BambuLab", "BBL", "Generic", "Kexcelled", "Polymaker", "eSUN"];

// IME composing state
var g_isComposing = false;
var g_searchTimer = null;

// ===================== Init =====================
function OnInit() {
    TranslatePage();
    // "custom=1" is only set when Plater reopens this page right after creating/
    // editing a custom filament (see GuideFrame::SetStartPage's BBL_FILAMENT_ONLY
    // branch, forwarded through guide/0's JumpToTarget()) — land on the Custom tab
    // so the user sees what they just made. Every other entry point (first-run
    // wizard, "add filament" from a preset combobox, ShowOnlyFilament on startup)
    // still defaults to the System tab.
    OnSelectMenu(GetQueryString('custom') === '1' ? 2 : 1);
    RequestProfile();
    RequestCustomFilaments();


    

        // Apply translated placeholder for search input
    var searchEl = document.getElementById('searchInput');
    if (searchEl) {
        var lang = localStorage.getItem('BambuWebLang') || 'en';
        if (!LangText.hasOwnProperty(lang)) lang = 'en';
        var ph = (LangText[lang] && LangText[lang]['t253']) || (LangText['en'] && LangText['en']['t253']) || 'Search filament name and type';
        searchEl.placeholder = ph;
    }
    if (searchEl) {
        searchEl.addEventListener('compositionstart', function() {
            g_isComposing = true;
        });
        searchEl.addEventListener('compositionend', function() {
            g_isComposing = false;
            doLiveFilter();
        });
        searchEl.addEventListener('input', function() {
            if (!g_isComposing) {
                doLiveFilterDebounced();
            }
        });
        searchEl.addEventListener('keydown', function(e) {
            if (e.keyCode === 13 && !g_isComposing) {
                e.preventDefault();
                commitSearch();
            }
        });
    }
}

function doLiveFilterDebounced() {
    if (g_searchTimer) clearTimeout(g_searchTimer);
    g_searchTimer = setTimeout(doLiveFilter, 150);
}

function doLiveFilter() {
    // Live filter without committing as tag
    renderFilamentList();
}

function commitSearch() {
    // Commit current input as a search keyword tag
    var val = ($('#searchInput').val() || '').trim();
    if (val && g_searchKeywords.indexOf(val) < 0) {
        g_searchKeywords.push(val);
        $('#searchInput').val('');
        updateTags();
        renderFilamentList();
    }
}

function RequestProfile() {
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "request_userguide_profile";
    SendWXMessage(JSON.stringify(tSend));
}

function HandleStudio(pVal) {
    let strCmd = pVal['command'];
    if (strCmd == 'response_userguide_profile') {
        m_ProfileItem = pVal['response'];
        parseProfileData();
        buildUI();

        // Fresh session (no filament carries a saved selection yet): mirror the
        // legacy ConfigWizard's on-printer-pick auto-default behavior by
        // pre-checking the recommended default filaments for the selected models.
        var anySelected = false;
        for (var i = 0; i < g_filaments.length; i++) {
            if (g_filaments[i].selected) { anySelected = true; break; }
        }
        if (!anySelected && g_models.length > 0) {
            ChooseDefaultFilament();
        }
    } else if (strCmd == 'update_custom_filaments') {
        UpdateCustomFilaments(pVal['data']);
    }
}

// ===================== Data Parsing =====================
function GetFilamentShortname(sName) {
    return sName.split('@')[0].trim();
}

function parseProfileData() {
    g_models = [];
    g_filaments = [];
    var printerSet = {};
    var vendorSet = {};
    var typeSet = {};

    // Parse models
    var nMode = m_ProfileItem["model"].length;
    for (var n = 0; n < nMode; n++) {
        var OneMode = m_ProfileItem["model"][n];
        if (OneMode["nozzle_selected"] != "") {
            g_models.push(OneMode);
            printerSet[OneMode['model']] = true;
        }
    }

    // Build model match strings
    var modelMatchStrings = [];
    for (var m = 0; m < g_models.length; m++) {
        var mdl = g_models[m];
        var nozzles = mdl['nozzle_selected'].split(';');
        for (var b = 0; b < nozzles.length; b++) {
            if (nozzles[b]) modelMatchStrings.push('[' + mdl['model'] + '++' + nozzles[b] + ']');
        }
    }

    // Parse filaments
    var shortNameMap = {}; // key: vendor+type+shortName -> index in g_filaments
    for (var key in m_ProfileItem['filament']) {
        var OneFila = m_ProfileItem['filament'][key];
        var fWholeName = OneFila['name'].trim();
        var fShortName = GetFilamentShortname(OneFila['name']);
        var fVendor = OneFila['vendor'];
        var fType = OneFila['type'];
        var fSelect = OneFila['selected'];
        var fModel = OneFila['models'];

        // Check model compatibility
        var bFind = false;
        if (fModel == '') {
            bFind = true;
        } else {
            for (var mi = 0; mi < modelMatchStrings.length; mi++) {
                if (fModel.indexOf(modelMatchStrings[mi]) >= 0) {
                    bFind = true;
                    break;
                }
            }
        }

        if (!bFind) continue;

        vendorSet[fVendor] = true;
        typeSet[fType] = true;

        var mapKey = fVendor + '|' + fType + '|' + fShortName;
        if (shortNameMap.hasOwnProperty(mapKey)) {
            var idx = shortNameMap[mapKey];
            g_filaments[idx].models += fModel;
            g_filaments[idx].filalist += fWholeName + ';';
        } else {
            shortNameMap[mapKey] = g_filaments.length;
            g_filaments.push({
                key: key,
                name: fWholeName,
                shortName: fShortName,
                vendor: fVendor,
                type: fType,
                models: fModel,
                selected: (fSelect * 1 == 1),
                filalist: fWholeName + ';'
            });
        }
    }

    // Build sorted lists
    g_printerList = Object.keys(printerSet).sort();
    g_vendorList = sortByPriority(Object.keys(vendorSet), VendorPriority);
    g_typeList = sortByPriority(Object.keys(typeSet), FilamentPriority);

    // Initialize filter: all selected
    g_selectedPrinters = g_printerList.slice();
    g_selectedVendors = g_vendorList.slice();
    g_selectedTypes = g_typeList.slice();
}

function sortByPriority(arr, priority) {
    var priorityLower = priority.map(function(p) { return p.toLowerCase(); });
    return arr.sort(function(a, b) {
        var ai = priorityLower.indexOf(a.toLowerCase());
        var bi = priorityLower.indexOf(b.toLowerCase());
        if (ai === -1) ai = 999;
        if (bi === -1) bi = 999;
        if (ai !== bi) return ai - bi;
        return a.localeCompare(b);
    });
}

// ===================== UI Building =====================
function buildUI() {
    updateFilterCounts();
    updateTags();
    renderFilamentList();
}

function updateFilterCounts() {
    $('#printerCount').text('(' + g_selectedPrinters.length + '/' + g_printerList.length + ')');
    $('#vendorCount').text('(' + g_selectedVendors.length + '/' + g_vendorList.length + ')');
    $('#filatypeCount').text('(' + g_selectedTypes.length + '/' + g_typeList.length + ')');
}

// ===================== Tags =====================

function updateTags() {
    if (g_searchKeywords.length === 0) {
        $('#tagsArea').removeClass('has-tags');
        $('#tagsList').html('');
        return;
    }

    $('#tagsArea').addClass('has-tags');

    var html = '';
    for (var i = 0; i < g_searchKeywords.length; i++) {
        html += '<span class="tag" data-idx="' + i + '">' + escapeHtml(g_searchKeywords[i]) +
                '<span class="tag-close" data-value="' + escapeHtml(g_searchKeywords[i]) + '" onClick="removeSearchTag(this)">&times;</span></span>';
    }
        html += '<span class="tags-clear" onClick="clearAllFilters()">\u6e05\u7a7a\u7b5b\u9009</span>';
    $('#tagsList').html(html);

    // Check if needs collapsing
    setTimeout(limitTagsToRows, 0);
}

var g_tagsExpanded = false;

function limitTagsToRows() {
    var container = document.getElementById('tagsList');
    if (!container) return;

    // Remove old control buttons
    $(container).find('.tags-ellipsis, .tags-collapse').remove();

    var tags = container.querySelectorAll('.tag');
    var clearEl = container.querySelector('.tags-clear');
    if (tags.length === 0) return;

    // Show all tags
    for (var i = 0; i < tags.length; i++) {
        tags[i].style.display = '';
    }
    if (clearEl) clearEl.style.display = '';

    if (g_tagsExpanded) {
        // Insert collapse before clear
        if (clearEl) $(clearEl).before('<span class="tags-collapse" onClick="collapseSearchTags()">\u6536\u8d77</span>');
        return;
    }

    // Measure: is clear button on row 3+?
    var firstTop = tags[0].offsetTop;
    var rowHeight = tags[0].offsetHeight;
    var row2Bottom = firstTop + rowHeight * 2 + 6; // 2 rows + gap

    if (!clearEl || clearEl.offsetTop < row2Bottom) {
        return; // everything fits in 2 rows
    }

    // Need to collapse: find how many tags fit in ~1.5 rows (leave space for ... + clear)
    var visibleCount = tags.length;
    for (var i = 0; i < tags.length; i++) {
        if (tags[i].offsetTop >= firstTop + rowHeight + 6) {
            // This tag is on row 2+, start checking from here
            visibleCount = i;
            break;
        }
    }

    // Try showing tags until ... + clear would overflow row 2
    // Hide all from visibleCount, add ..., check if it fits
    for (var tryCount = tags.length - 1; tryCount >= 0; tryCount--) {
        // Hide tags after tryCount
        for (var i = 0; i < tags.length; i++) {
            tags[i].style.display = (i < tryCount) ? '' : 'none';
        }
        // Remove old ellipsis
        $(container).find('.tags-ellipsis').remove();
        // Insert ellipsis before clear
        $(clearEl).before('<span class="tags-ellipsis" title="\u663e\u793a\u5168\u90e8" onClick="expandSearchTags()">\u2026</span>');

        // Check if clear is now within row 2
        if (clearEl.offsetTop < row2Bottom) {
            return; // done
        }
    }
}

function expandSearchTags() {
    g_tagsExpanded = true;
    limitTagsToRows();
}

function collapseSearchTags() {
    g_tagsExpanded = false;
    limitTagsToRows();
}

function removeSearchTag(el) {
    var value = $(el).data('value');
    g_searchKeywords = g_searchKeywords.filter(function(v) { return v !== value; });
    updateTags();
    renderFilamentList();
}



function clearAllFilters() {
    g_searchKeywords = [];
    $('#searchInput').val('');
    updateTags();
    renderFilamentList();
}

// ===================== Filter Sections (Accordion) =====================
function toggleFilterSection(type) {
    var section = $('#section_' + type);
    var isOpen = section.hasClass('open');

    if (isOpen) {
        // Close this section
        section.removeClass('open');
    } else {
        // Open this section, render dropdown content
        section.addClass('open');
        renderFilterDropdown(type);
    }
}

function renderFilterDropdown(type) {
    var dropdownId = '#dropdown_' + type;
    var list, selected;
    if (type === 'printer') {
        list = g_printerList; selected = g_selectedPrinters;
    } else if (type === 'vendor') {
        list = g_vendorList; selected = g_selectedVendors;
    } else {
        list = g_typeList; selected = g_selectedTypes;
    }

    var html = '';
    var allSelected = (selected.length === list.length && list.length > 0);
    html += '<div class="filter-dd-item select-all"><input type="checkbox" id="dd_all_' + type + '" ' +
            (allSelected ? 'checked' : '') + ' onChange="onDdAllChange(\'' + type + '\')" />' +
            '<label for="dd_all_' + type + '">Select All</label></div>';
    for (var i = 0; i < list.length; i++) {
        var checked = selected.indexOf(list[i]) >= 0;
        var uid = 'dd_' + type + '_' + i;
        html += '<div class="filter-dd-item"><input type="checkbox" id="' + uid + '" value="' + escapeHtml(list[i]) + '" ' +
                (checked ? 'checked' : '') + ' onChange="onDdItemChange(\'' + type + '\')" />' +
                '<label for="' + uid + '" title="' + escapeHtml(list[i]) + '">' + escapeHtml(list[i]) + '</label></div>';
    }
    $(dropdownId).html(html);
}

function onDdAllChange(type) {
    var checked = $('#dd_all_' + type).prop('checked');
    var dropdown = $('#dropdown_' + type);
    dropdown.find('input[type="checkbox"]').prop('checked', checked);

    if (type === 'printer') {
        g_selectedPrinters = checked ? g_printerList.slice() : [];
    } else if (type === 'vendor') {
        g_selectedVendors = checked ? g_vendorList.slice() : [];
    } else {
        g_selectedTypes = checked ? g_typeList.slice() : [];
    }
    updateFilterCounts();
    updateTags();
    renderFilamentList();
}

function onDdItemChange(type) {
    var dropdown = $('#dropdown_' + type);
    var items = dropdown.find('input[type="checkbox"][value]');
    var selected = [];
    items.each(function() {
        if ($(this).prop('checked')) selected.push($(this).val());
    });

    if (type === 'printer') {
        g_selectedPrinters = selected;
    } else if (type === 'vendor') {
        g_selectedVendors = selected;
    } else {
        g_selectedTypes = selected;
    }

    // Update "Select All" checkbox
    var list = type === 'printer' ? g_printerList : (type === 'vendor' ? g_vendorList : g_typeList);
    $('#dd_all_' + type).prop('checked', selected.length === list.length);

    updateFilterCounts();
    updateTags();
    renderFilamentList();
}

// ===================== Filament List Rendering =====================
function getFilteredFilaments() {
    var searchText = ($('#searchInput').val() || '').toLowerCase();

    // Build model match list from selected printers
    var modelMatchStrings = [];
    for (var m = 0; m < g_models.length; m++) {
        var mdl = g_models[m];
        if (g_selectedPrinters.indexOf(mdl['model']) < 0) continue;
        var nozzles = mdl['nozzle_selected'].split(';');
        for (var b = 0; b < nozzles.length; b++) {
            if (nozzles[b]) modelMatchStrings.push('[' + mdl['model'] + '++' + nozzles[b] + ']');
        }
    }

    var result = [];
    for (var i = 0; i < g_filaments.length; i++) {
        var f = g_filaments[i];

                // Filter logic: AND across active filters
                // A filter with 0 selections = skip (don't apply)
                // All three at 0 = empty result
                var hasAnyFilter = (g_selectedPrinters.length > 0 || g_selectedVendors.length > 0 || g_selectedTypes.length > 0);
                if (!hasAnyFilter) continue; // all empty = no results

                // Vendor: skip if all deselected, otherwise must match
                if (g_selectedVendors.length > 0 && g_selectedVendors.indexOf(f.vendor) < 0) continue;

                // Type: skip if all deselected, otherwise must match
                if (g_selectedTypes.length > 0 && g_selectedTypes.indexOf(f.type) < 0) continue;

                // Printer: skip if all deselected, otherwise must match
                if (g_selectedPrinters.length > 0 && f.models !== '') {
                    var hasModel = false;
                    for (var mi = 0; mi < modelMatchStrings.length; mi++) {
                        if (f.models.indexOf(modelMatchStrings[mi]) >= 0) {
                            hasModel = true;
                            break;
                        }
                    }
                    if (!hasModel) continue;
                }
                // Filter by search: committed keywords (OR union) + live input
                var liveSearch = ($('#searchInput').val() || '').trim().toLowerCase();
                var allKeywords = g_searchKeywords.slice();
                if (liveSearch) allKeywords.push(liveSearch);

                if (allKeywords.length > 0) {
                    var matched = false;
                    for (var si = 0; si < allKeywords.length; si++) {
                        var kw = allKeywords[si].toLowerCase();
                        // Match filament name, type, or vendor
                        if (f.shortName.toLowerCase().indexOf(kw) >= 0 ||
                            f.type.toLowerCase().indexOf(kw) >= 0 ||
                            f.vendor.toLowerCase().indexOf(kw) >= 0) {
                            matched = true;
                            break;
                        }
                        // Match printer name: show compatible filaments
                        if (f.models !== '' && searchMatchesPrinter(kw, f.models)) {
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) continue;
                }

        result.push(f);
    }
    return result;
}

function renderFilamentList() {
    var filtered = getFilteredFilaments();

    // Group by type
    var groups = {};
    var groupOrder = [];
    for (var i = 0; i < filtered.length; i++) {
        var f = filtered[i];
        if (!groups.hasOwnProperty(f.type)) {
            groups[f.type] = [];
            groupOrder.push(f.type);
        }
        groups[f.type].push(f);
    }

    // Sort group order by FilamentPriority
    groupOrder = sortByPriority(groupOrder, FilamentPriority);

    // Render
    var html = '';
    for (var g = 0; g < groupOrder.length; g++) {
        var type = groupOrder[g];
        var items = groups[type];
        html += '<div class="fila-group" data-type="' + escapeHtml(type) + '">';
        html += '<div class="fila-group-header" onClick="toggleGroup(this)">';
        html += '<span class="fila-group-arrow">▼</span>';
        html += '<span class="fila-group-title">' + escapeHtml(type) + '</span>';
        html += '<span class="fila-group-count">(' + items.length + ')</span>';
        html += '</div>';
        html += '<div class="fila-group-items">';
        for (var fi = 0; fi < items.length; fi++) {
            var fila = items[fi];
            var uid = 'fila_' + g + '_' + fi;
            html += '<div class="fila-item">';
            html += '<input type="checkbox" id="' + uid + '" data-idx="' + g_filaments.indexOf(fila) + '" ' +
                    (fila.selected ? 'checked' : '') + ' onChange="onFilaCheckChange(this)" />';
            html += '<label for="' + uid + '" title="' + escapeHtml(fila.shortName) + '">' + escapeHtml(fila.shortName) + '</label>';
            html += '</div>';
        }
        html += '</div></div>';
    }

    $('#ItemBlockArea').html(html);

    // Update result count
    var lang2 = localStorage.getItem('BambuWebLang') || 'en';
    if (!LangText.hasOwnProperty(lang2)) lang2 = 'en';
    var filterTpl = (LangText[lang2] && LangText[lang2]['t254']) || (LangText['en'] && LangText['en']['t254']) || 'Filter results: {n} matches';
    $('#filterResultText').text(filterTpl.replace('{n}', filtered.length));

        // Show/hide empty state
        var hasAnyFilter = (g_selectedPrinters.length > 0 || g_selectedVendors.length > 0 || g_selectedTypes.length > 0);
    
        if (filtered.length === 0) {
            $('#emptyState').show();
            $('#ItemBlockArea').hide();
        
            // If empty because all base filters are unchecked, hide search
            // If empty because of search keyword, keep search bar visible
            if (!hasAnyFilter) {
                $('#filterBar').hide();
                $('#searchBar').hide();
            } else {
                $('#filterBar').show();
                $('#searchBar').show();
            }
        } else {
            $('#emptyState').hide();
            $('#ItemBlockArea').show();
            $('#filterBar').show();
            $('#searchBar').show();
        }

    // Update select-all checkbox
    updateSelectAllCheckbox();
}

function toggleGroup(el) {
    var $header = $(el);
    var $items = $header.next('.fila-group-items');
    var $arrow = $header.find('.fila-group-arrow');
    $items.toggleClass('collapsed');
    $arrow.toggleClass('collapsed');
}

function onFilaCheckChange(el) {
    var idx = parseInt($(el).data('idx'));
    g_filaments[idx].selected = $(el).prop('checked');
    updateSelectAllCheckbox();
}

function onSelectAllChange() {
    var checked = $('#SelectAllCheckbox').prop('checked');
    var filtered = getFilteredFilaments();
    for (var i = 0; i < filtered.length; i++) {
        filtered[i].selected = checked;
    }
    renderFilamentList();
}

function updateSelectAllCheckbox() {
    var filtered = getFilteredFilaments();
    var allChecked = filtered.length > 0;
    for (var i = 0; i < filtered.length; i++) {
        if (!filtered[i].selected) { allChecked = false; break; }
    }
    $('#SelectAllCheckbox').prop('checked', allChecked);
}

// onSearchInput kept for backward compat but no longer used inline
function onSearchInput() {
    if (!g_isComposing) {
        doLiveFilterDebounced();
    }
}

// ===================== Choose Default =====================
function ChooseDefaultFilament() {
    // Get default materials from all models
    var defaultMaterials = '';
    for (var n = 0; n < g_models.length; n++) {
        defaultMaterials += (g_models[n]['materials'] || '') + ';';
    }
    var defaultArr = defaultMaterials.split(';');

    for (var i = 0; i < g_filaments.length; i++) {
        var f = g_filaments[i];
        f.selected = false;
        var filalist = f.filalist.split(';');
        for (var p = 0; p < filalist.length; p++) {
            if (filalist[p] !== '' && defaultArr.indexOf(filalist[p]) > -1) {
                f.selected = true;
                break;
            }
        }
    }
    renderFilamentList();
    ShowNotice(0);
}

// ===================== Notice =====================
function ShowNotice(nShow) {
    if (nShow == 0) {
        $("#NoticeMask").hide();
        $("#NoticeBody").hide();
    } else {
        $("#NoticeMask").show();
        $("#NoticeBody").show();
    }
}

// ===================== Save / Cancel / Confirm =====================
function ResponseFilamentResult() {
    // Collect all selected filaments
    var selectedCount = 0;
    var FilaArray = [];
    for (var i = 0; i < g_filaments.length; i++) {
        if (g_filaments[i].selected) {
            selectedCount++;
            // Find all matching full names in m_ProfileItem
            var shortName = g_filaments[i].shortName;
            for (var key in m_ProfileItem['filament']) {
                var FName = GetFilamentShortname(key);
                if (FName == shortName) FilaArray.push(key);
            }
        }
    }

    if (selectedCount == 0) {
        ShowNotice(1);
        return false;
    }

    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "save_userguide_filaments";
    tSend['data'] = {};
    tSend['data']['filament'] = FilaArray;
    SendWXMessage(JSON.stringify(tSend));
    return true;
}

function CancelSelect() {
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "user_guide_cancel";
    tSend['data'] = {};
    SendWXMessage(JSON.stringify(tSend));
}

function ConfirmSelect() {
    var bRet = ResponseFilamentResult();
    if (bRet) {
        var tSend = {};
        tSend['sequence_id'] = Math.round(new Date() / 1000);
        tSend['command'] = "user_guide_finish";
        tSend['data'] = {};
        tSend['data']['action'] = "finish";
        SendWXMessage(JSON.stringify(tSend));
    }
}

// ===================== Tab Switch =====================
function OnSelectMenu(nIndex) {
    switch (nIndex) {
        case 1:
            $('#SystemFilamentBtn').addClass('TitleSelected').removeClass('TitleUnselected');
            $('#CustomFilamentBtn').addClass('TitleUnselected').removeClass('TitleSelected');
            $('#SystemFilamentsArea').css('display', 'flex');
            $('#CustomFilamentsArea').css('display', 'none');
            break;
        case 2:
            $('#CustomFilamentBtn').addClass('TitleSelected').removeClass('TitleUnselected');
            $('#SystemFilamentBtn').addClass('TitleUnselected').removeClass('TitleSelected');
            $('#CustomFilamentsArea').css('display', 'flex');
            $('#SystemFilamentsArea').css('display', 'none');
            break;
    }
}

// ===================== Custom Filaments =====================
function RequestCustomFilaments() {
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "request_custom_filaments";
    SendWXMessage(JSON.stringify(tSend));
}

var g_customFilaments = [];
var g_customSortOrder = 'desc'; // 'desc' or 'asc'

function UpdateCustomFilaments(CFList) {
    g_customFilaments = CFList || [];
    renderCustomFilaments();
}

function onCustomSortChange() {
    g_customSortOrder = $('#CFilament_Sort').val();
    renderCustomFilaments();
}

function renderCustomFilaments() {
    var list = g_customFilaments.slice();

    if (list.length === 0) {
        $('#CFilament_Header').hide();
        $('#CFilament_List').hide();
        $('#CFilament_Btn_Area').hide();
        $('#CFilament_Empty').show();
        return;
    }

    $('#CFilament_Header').show();
    $('#CFilament_List').show();
    $('#CFilament_Btn_Area').show();
    $('#CFilament_Empty').hide();
    var _lang = localStorage.getItem('BambuWebLang') || 'en';
    if (!LangText.hasOwnProperty(_lang)) _lang = 'en';
    function _t(tid) { return (LangText[_lang] && LangText[_lang][tid]) || (LangText['en'] && LangText['en'][tid]) || ''; }
    $('#CFilament_Count').text((_t('t242') || 'Custom filaments: 0').replace('0', list.length));

        // Sort logic
        if (g_customSortOrder === 'type') {
            list.sort(function(a, b) {
                var ta = (a['type'] || '').toLowerCase();
                var tb = (b['type'] || '').toLowerCase();
                if (ta !== tb) return ta.localeCompare(tb);
                var na = (a['name'] || '').toLowerCase();
                var nb = (b['name'] || '').toLowerCase();
                return na.localeCompare(nb);
            });
        } else {
            list.sort(function(a, b) {
                var da = a['create_time'] || a['date'] || '';
                var db = b['create_time'] || b['date'] || '';
                if (!da || !db) return 0; // cannot sort without date
                if (g_customSortOrder === 'asc') return da.localeCompare(db);
                return db.localeCompare(da);
            });
        }

        var strHtml = '';
    
        if (g_customSortOrder === 'desc') {
            var now = new Date();
            var todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
            var weekStart = todayStart - 7 * 24 * 60 * 60 * 1000;
        
            var groupToday = [];
            var groupWeek = [];
            var groupOlder = [];
        
            for (var n = 0; n < list.length; n++) {
                var dateStr = list[n]['create_time'] || list[n]['date'] || '';
                var ts = new Date(dateStr.replace(/-/g, '/')).getTime();
                if (!ts) {
                    groupOlder.push(list[n]);
                } else if (ts >= todayStart) {
                    groupToday.push(list[n]);
                } else if (ts >= weekStart) {
                    groupWeek.push(list[n]);
                } else {
                    groupOlder.push(list[n]);
                }
            }
        
            if (groupToday.length > 0) strHtml += renderCustomGroup(_t('t255') || 'Today', groupToday, false);
            if (groupWeek.length > 0) strHtml += renderCustomGroup(_t('t256') || 'This week', groupWeek, false);
            if (groupOlder.length > 0) strHtml += renderCustomGroup(_t('t257') || 'Earlier', groupOlder, false);
        
    } else if (g_customSortOrder === 'type') {
            var typeGroups = {};
            var typeOrder = [];
            for (var n = 0; n < list.length; n++) {
                var t = list[n]['type'] || 'Other';
                if (!typeGroups[t]) {
                    typeGroups[t] = [];
                    typeOrder.push(t);
                }
                typeGroups[t].push(list[n]);
            }
            typeOrder = sortByPriority(typeOrder, FilamentPriority);
            for (var i = 0; i < typeOrder.length; i++) {
                strHtml += renderCustomGroup(typeOrder[i], typeGroups[typeOrder[i]], true);
            }
    } else {
            // Ascending order: just flat list
            strHtml = renderCustomGroup('', list, false);
    }
    
    $('#CFilament_List').html(strHtml);
}

function toggleCustomGroup(el) {
    if (!$(el).hasClass('collapsible')) return;
    var $header = $(el);
    var $items = $header.next('.CFilament_GroupItems');
    var $arrow = $header.find('.CFilament_GroupArrow');
    $items.toggleClass('collapsed');
    $arrow.toggleClass('collapsed');
}

function renderCustomGroup(title, items, isCollapsible) {
    var html = '';
    if (title) {
            if (isCollapsible) {
                html += '<div class="CFilament_GroupTitle collapsible" onClick="toggleCustomGroup(this)">';
                html += '<span class="CFilament_GroupArrow"></span>' + escapeHtml(title) + '</div>';
            } else {
                html += '<div class="CFilament_GroupTitle">' + escapeHtml(title) + '</div>';
            }
            html += '<div class="CFilament_GroupItems">';
    } else {
            html += '<div class="CFilament_GroupItems">';
    }
        for (var n = 0; n < items.length; n++) {
            var pItem = items[n];
            var F_id = pItem['id'] || '';
            var F_name = pItem['name'] || '';
            var F_type = pItem['type'] || ''; // will be empty string
            var F_date = pItem['create_time'] || pItem['date'] || ''; // will be empty string
        
            if (F_date.length > 10) F_date = F_date.substring(0, 10);
        
            html += '<div class="CFilament_Item">' +
                '<span class="CFilament_Name" title="' + escapeHtml(F_name) + '">' + escapeHtml(F_name) + '</span>' +
                '<span class="CFilament_Type">' + escapeHtml(F_type) + '</span>' +
                '<span class="CFilament_Date">' + escapeHtml(F_date) + '</span>' +
                '<img onClick="CFEdit(\'' + F_id + '\')" class="CFilament_EditBtn" src="../../image/edit.svg" />' +
                '<img onClick="CFDelete(\'' + F_id + '\',\'' + escapeHtml(F_name).replace(/'/g, '&#39;') + '\')" class="CFilament_DeleteBtn" src="../../image/delete.svg" />' +
                '</div>';
        }
        html += '</div>';
        return html;
}

function OnClickCustomFilamentAdd() {
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "create_custom_filament";
    SendWXMessage(JSON.stringify(tSend));
}

function CFEdit(fid) {
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "modify_custom_filament";
    tSend['id'] = fid;
    SendWXMessage(JSON.stringify(tSend));
}

function CFDelete(fid, fname) {
    // C++ side runs the native confirm dialog + actual preset deletion, then re-sends
    // update_custom_filaments to refresh the list. Passing the name lets C++ display it
    // in the confirm prompt.
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "delete_custom_filament";
    tSend['id'] = fid;
    tSend['name'] = fname || '';
    SendWXMessage(JSON.stringify(tSend));
}

// ===================== Utilities =====================

// Check if a keyword matches a printer name and the filament is compatible with that printer
function searchMatchesPrinter(kw, filaModels) {
    for (var pi = 0; pi < g_models.length; pi++) {
        var printerName = g_models[pi]['model'].toLowerCase();
        if (printerName.indexOf(kw) >= 0) {
            // This keyword matches a printer, check if filament is compatible
            var nozzles = g_models[pi]['nozzle_selected'].split(';');
            for (var ni = 0; ni < nozzles.length; ni++) {
                if (nozzles[ni] && filaModels.indexOf('[' + g_models[pi]['model'] + '++' + nozzles[ni] + ']') >= 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}



