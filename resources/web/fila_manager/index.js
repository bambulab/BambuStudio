/* Filament Manager — index.js */

var g_spools = [];
var g_view = "my";
var g_tab = "all";
var g_sort_key = "";
var g_sort_asc = true;
var g_selected_ids = {};
var g_filters = {};
var g_editing_spool_id = null;
var g_dialog_mode = "manual";
var g_quantity = 1;
var g_ams_data = null;
var g_ams_selected_unit = null;
var g_ams_selected_slot = null;
var g_grouped = false;
var g_collapsed_groups = {};
var g_page = 1;
var g_page_size = 50;

var BAMBU_COLORS = [
    "#000000","#333333","#555555","#808080","#BBBBBB","#FFFFFF",
    "#FF0000","#CC3333","#FF6666","#FF3300","#FF6600","#FF9900",
    "#FFCC00","#FFFF00","#CCFF00","#66FF00","#00CC00","#009933",
    "#006633","#00CCCC","#0099CC","#0066CC","#0033CC","#0000FF",
    "#3300CC","#6600CC","#9900CC","#CC00CC","#FF00FF","#FF66CC",
    "#FF99CC","#CC6666","#996633","#663300","#CCCC99","#99CC66",
    "#66CCCC","#6699FF","#CC99FF","#FFCC99"
];

var CURRENCY_SYMBOLS = {"USD":"US$","CNY":"¥","EUR":"€","JPY":"¥"};
var g_preset_vendors = [];

/* ===== C++ ↔ JS Bridge ===== */
/*
 * JS → C++:  chrome.webview.postMessage / window.wx.postMessage
 * C++ → JS:  window.__cppPush(pkt) → CustomEvent('cpp:fila')
 *
 * __cppPush is defined here so it's available before any C++ response.
 * C++ re-injects it on wxEVT_WEBVIEW_LOADED as a safety net (idempotent).
 */
var FM_VERSION = "1.0";
var FM_REQUEST_TIMEOUT_MS = 15000;

window.__cppPush = function(pkt) {
    try {
        document.dispatchEvent(new CustomEvent("cpp:fila", { detail: pkt }));
    } catch(e) { console.error("[FM] __cppPush error:", e); }
};

var g_req_seq = 0;
var g_pending_reqs = {};

function postMessage(msg) {
    var json = JSON.stringify(msg);
    if (window.chrome && window.chrome.webview)
        window.chrome.webview.postMessage(json);
    else if (window.wx)
        window.wx.postMessage(json);
    else
        console.log("[FM] postMessage (no bridge):", json);
}

/**
 * Send a request to C++ and register a response callback.
 * @param {string}   command
 * @param {object}   data
 * @param {function} onResponse  - callback(code, data)
 * @param {number}   [timeoutMs] - override default timeout; 0 = no timeout
 */
function sendRequest(command, data, onResponse, timeoutMs) {
    var seq = ++g_req_seq;
    var timer = null;

    function settle(code, respData) {
        if (timer) { clearTimeout(timer); timer = null; }
        delete g_pending_reqs[seq];
        if (onResponse) onResponse(code, respData);
    }

    g_pending_reqs[seq] = settle;

    var ms = (timeoutMs === undefined) ? FM_REQUEST_TIMEOUT_MS : timeoutMs;
    if (ms > 0) {
        timer = setTimeout(function() {
            if (g_pending_reqs[seq]) {
                console.warn("[FM] request timeout seq=" + seq + " cmd=" + command);
                settle(-2, { error: "request timeout" });
            }
        }, ms);
    }

    postMessage({ type: "request", v: FM_VERSION, seq: seq, command: command, data: data || {} });
}

/* ===== Push subscription registry ===== */
var g_push_handlers = {};

/**
 * Subscribe to a push command from C++. Returns an unsubscribe function.
 * @param {string}   command
 * @param {function} handler - handler(data)
 * @returns {function} unsubscribe
 */
function onPush(command, handler) {
    if (!g_push_handlers[command]) g_push_handlers[command] = [];
    g_push_handlers[command].push(handler);
    return function() {
        var arr = g_push_handlers[command];
        if (arr) g_push_handlers[command] = arr.filter(function(h) { return h !== handler; });
    };
}

function onSpoolsUpdated(code, data) {
    if (code === 0) { g_spools = data || []; refresh(); }
}

function onMachineListResponse(code, data) {
    var deviceArea = document.getElementById("ams-device-area");
    var emptyArea  = document.getElementById("ams-empty");
    var emptyText  = document.querySelector(".ams-empty-text");

    if (code !== 0) {
        deviceArea.style.display = "none";
        emptyArea.style.display = "flex";
        if (emptyText) emptyText.textContent = "获取设备列表失败，请重试";
        document.getElementById("dialog-body").style.display = "none";
        console.error("[FM] get_machine_list failed, code=" + code);
        return;
    }

    var machines = (data && data.machines) || [];

    if (machines.length === 0) {
        deviceArea.style.display = "none";
        emptyArea.style.display = "flex";
        if (emptyText) emptyText.textContent = "未发现可用打印机，请确保已登录并绑定设备";
        document.getElementById("dialog-body").style.display = "none";
        return;
    }

    emptyArea.style.display = "none";
    deviceArea.style.display = "block";

    var sel = document.getElementById("ams-device-select");
    sel.innerHTML = "";
    var firstOnlineId = "";
    machines.forEach(function(m) {
        var opt = document.createElement("option");
        opt.value = m.dev_id;
        opt.textContent = m.dev_name || m.dev_id;
        if (m.is_online) opt.textContent += " (在线)";
        sel.appendChild(opt);
        if (!firstOnlineId && m.is_online) firstOnlineId = m.dev_id;
    });

    var defaultId = firstOnlineId || machines[0].dev_id;
    sel.value = defaultId;

    g_ams_selected_unit = null;
    g_ams_selected_slot = null;
    document.getElementById("ams-unit-icons").innerHTML = "";
    document.getElementById("ams-slots").innerHTML =
        '<div class="ams-empty-inline">正在加载 AMS 数据…</div>';
    sendRequest("get_ams_data", { dev_id: defaultId }, onAmsDataResponse);
}

function onAmsDataResponse(code, data) {
    if (code !== 0) {
        console.error("[FM] get_ams_data failed, code=" + code);
        document.getElementById("ams-unit-icons").innerHTML = "";
        document.getElementById("ams-slots").innerHTML =
            '<div class="ams-empty-inline">获取 AMS 数据失败</div>';
        document.getElementById("dialog-body").style.display = "none";
        return;
    }
    g_ams_data = data || null;
    try {
        renderAmsSection();
    } catch(e) {
        console.error("[FM] renderAmsSection error:", e);
        document.getElementById("ams-unit-icons").innerHTML = "";
        document.getElementById("ams-slots").innerHTML =
            '<div class="ams-empty-inline">渲染错误: ' + e.message + '</div>';
        document.getElementById("dialog-body").style.display = "none";
    }
}

/* ===== C++ → JS message dispatcher ===== */
/*
 * C++ calls:  window.__cppPush({ v, ts, type, seq, code, command, data })
 * __cppPush dispatches:  CustomEvent('cpp:fila', { detail: pkt })
 * Router: responses → pending callbacks; pushes → registered handlers.
 */
document.addEventListener("cpp:fila", function(e) {
    var msg = e.detail;
    if (!msg) return;

    var type = msg.type || "";

    /* Response — resolve pending request callback */
    if (type === "response") {
        var cb = g_pending_reqs[msg.seq];
        if (cb) cb(msg.code || 0, msg.data);
        return;
    }

    /* Push — dispatch to registered handlers */
    if (type === "push") {
        var cmd = msg.command;
        var handlers = g_push_handlers[cmd];
        if (handlers && handlers.length) {
            var data = msg.data;
            for (var i = 0; i < handlers.length; i++) {
                try { handlers[i](data); } catch(err) { console.error("[FM] push handler error cmd=" + cmd, err); }
            }
        }
    }
});

/* ===== Register push handlers ===== */
onPush("spool_list", function(data) { g_spools = data || []; refresh(); });
onPush("preset_options", function(data) { g_preset_vendors = (data && data.vendors) || []; });
onPush("ams_data", function(data) { onAmsDataResponse(0, data); });
onPush("theme_changed", function(data) {
    var theme = (data && data.theme) || "dark";
    document.documentElement.dataset.theme = theme;
    if (g_view === "stats") renderStats();
});

/* ===== Filtering & Sorting ===== */
function getFilteredSpools() {
    var keyword = (document.getElementById("search-input").value || "").toLowerCase();
    var list = g_spools.filter(function(s) {
        if (g_view === "archived") { if (s.status !== "archived") return false; }
        else { if (s.status === "archived") return false; }
        if (g_tab === "favorite" && !s.favorite) return false;
        if (g_tab === "ams" && s.entry_method !== "ams_sync") return false;
        if (keyword) {
            var hay = ((s.brand||"")+" "+(s.material_type||"")+" "+(s.series||"")+" "+(s.color_name||"")).toLowerCase();
            if (hay.indexOf(keyword) === -1) return false;
        }
        for (var k in g_filters) {
            if (g_filters[k] && s[k] !== g_filters[k]) return false;
        }
        return true;
    });
    if (g_sort_key) {
        list.sort(function(a, b) {
            var va = a[g_sort_key], vb = b[g_sort_key];
            if (typeof va === "number" && typeof vb === "number") return g_sort_asc ? va - vb : vb - va;
            va = String(va || ""); vb = String(vb || "");
            return g_sort_asc ? va.localeCompare(vb) : vb.localeCompare(va);
        });
    }
    return list;
}

function refresh() {
    var spools = getFilteredSpools();
    renderTable(spools);
    updateBatchUI();
    if (g_view === "stats") renderStats();
}

/* ===== Status tags ===== */
function getStatusTags(s) {
    var tags = [];
    var remain = s.remain_percent || 0;
    if (remain === 0) tags.push({text: "已用尽", cls: "status-empty"});
    else if (remain < 20) tags.push({text: "低余量", cls: "status-low"});
    if (s.dry_reminder_days > 0 && s.dry_date) {
        var diff = (Date.now() - new Date(s.dry_date).getTime()) / 86400000;
        if (diff >= s.dry_reminder_days) tags.push({text: "需烘干", cls: "status-dry"});
    }
    return tags;
}

/* ===== Price formatting ===== */
var CURRENCY_SYMBOLS = {CNY:"¥", USD:"$", EUR:"€", JPY:"¥"};
function formatPrice(price, currency) {
    var sym = CURRENCY_SYMBOLS[currency] || "$";
    return sym + " " + (price || 0).toFixed(2);
}

/* ===== Grouping ===== */
function groupSpools(list) {
    var map = {};
    list.forEach(function(s) {
        var key = (s.brand||"?") + "/" + (s.material_type||"") + (s.series ? " "+s.series : "") + "/" + (s.color_name || s.color_code || "?");
        if (!map[key]) map[key] = {key: key, count: 0, totalWeight: 0, spools: [], collapsed: !!g_collapsed_groups[key]};
        map[key].count++;
        map[key].totalWeight += Math.round((s.initial_weight||0) * (s.remain_percent||0) / 100);
        map[key].spools.push(s);
    });
    return Object.values(map);
}

/* ===== Table rendering ===== */
function renderTable(allSpools) {
    var tbody = document.getElementById("spool-tbody");
    var empty = document.getElementById("empty-state");
    tbody.innerHTML = "";

    var totalCount = allSpools.length;
    if (!totalCount) {
        empty.style.display = "flex";
        renderPagination(0);
        return;
    }
    empty.style.display = "none";

    if (g_grouped) {
        var groups = groupSpools(allSpools);
        var flatRows = [];
        groups.forEach(function(g) {
            flatRows.push({_groupHeader: true, group: g});
            if (!g.collapsed) g.spools.forEach(function(s) { flatRows.push(s); });
        });
        var start = (g_page - 1) * g_page_size;
        var page = flatRows.slice(start, start + g_page_size);
        page.forEach(function(item) {
            if (item._groupHeader) tbody.appendChild(createGroupHeader(item.group));
            else tbody.appendChild(createRow(item));
        });
        renderPagination(flatRows.length);
    } else {
        var start = (g_page - 1) * g_page_size;
        var page = allSpools.slice(start, start + g_page_size);
        page.forEach(function(s) { tbody.appendChild(createRow(s)); });
        renderPagination(totalCount);
    }
}

function createGroupHeader(g) {
    var tr = document.createElement("tr");
    tr.className = "group-header";
    tr.innerHTML = '<td colspan="6"><div class="group-info">' +
        '<span class="group-arrow'+(g.collapsed?" collapsed":"")+'">▾</span>' +
        '<span>'+esc(g.key)+'</span>' +
        '<span class="group-count">'+g.count+'</span>' +
        '<span class="group-weight">'+g.totalWeight+' g</span>' +
    '</div></td>';
    tr.addEventListener("click", function() {
        g.collapsed = !g.collapsed;
        g_collapsed_groups[g.key] = g.collapsed;
        refresh();
    });
    return tr;
}

function buildSpoolSvg(color) {
    var c = color || "#888";
    return '<svg width="40" height="40" viewBox="0 0 40 40" fill="none">' +
        '<path d="M5 8C5 5.8 6.8 4 9 4h5v32H9C6.8 36 5 34.2 5 32V8z" fill="'+c+'" opacity="0.55"/>' +
        '<rect x="10" y="7" width="8" height="26" rx="1" fill="'+c+'" opacity="0.75"/>' +
        '<rect x="14" y="4" width="5" height="32" rx="1" fill="'+c+'" opacity="0.45"/>' +
        '<ellipse cx="24" cy="20" rx="12" ry="16" fill="'+c+'"/>' +
        '<ellipse cx="24" cy="20" rx="8" ry="10.5" fill="'+c+'" opacity="0.55"/>' +
        '<ellipse cx="25" cy="20" rx="3" ry="4" fill="#1a1a1a" opacity="0.85"/>' +
        '<ellipse cx="24.5" cy="19" rx="1.5" ry="2" fill="rgba(255,255,255,0.1)"/>' +
    '</svg>';
}

function createRow(s) {
    var tr = document.createElement("tr");
    if (g_selected_ids[s.spool_id]) tr.className = "selected";
    var remain = s.remain_percent || 0;
    var pClass = remain === 0 ? "empty" : (remain < 20 ? "low" : "");
    var remainWeight = Math.round((s.initial_weight||0) * remain / 100);
    var totalWeight = Math.round(s.initial_weight||0);
    var tags = getStatusTags(s);
    var tagsHtml = tags.length ? tags.map(function(t) { return '<span class="status-tag '+t.cls+'">'+t.text+'</span>'; }).join("") : "";
    var diam = s.diameter || 1.75;
    var nameParts = (s.material_type||"") + (s.series ? " "+s.series : "");

    tr.innerHTML =
        '<td class="col-check"><input type="checkbox" data-id="'+esc(s.spool_id)+'"'+(g_selected_ids[s.spool_id]?" checked":"")+'></td>' +
        '<td class="col-filament"><div class="cell-filament">' +
            '<div class="spool-icon" style="--spool-color:'+esc(s.color_code||"#888")+'">' + buildSpoolSvg(s.color_code) + '<div class="color-indicator"></div></div>' +
            '<div class="filament-info">' +
                '<div class="name-row">' +
                    '<svg class="fila-brand-icon" width="12" height="12" viewBox="0 0 12 12"><rect x="1" y="1" width="10" height="10" rx="1.5" fill="currentColor" opacity="0.5"/></svg>' +
                    '<span class="fila-name">'+esc(nameParts||"—")+'</span>' +
                    '<button class="fav-star'+(s.favorite?" active":"")+'" onclick="toggleFav(\''+s.spool_id+'\')" title="收藏"><svg width="16" height="16" viewBox="0 0 16 16" fill="none"><path d="M8 2l1.8 3.6 4 .6-2.9 2.8.7 4L8 11.2 4.4 13l.7-4L2.2 6.2l4-.6L8 2z" stroke="currentColor" stroke-width="1" fill="'+(s.favorite?"currentColor":"none")+'"/></svg></button>' +
                '</div>' +
                '<div class="sub-row">'+diam+' mm | '+esc(s.color_name||"—")+'</div>' +
            '</div>' +
        '</div></td>' +
        '<td class="col-remain"><div class="remain-cell">' +
            '<div class="remain-text-row"><span class="remain-current">'+remainWeight+' g</span><span class="remain-sep"> / </span><span class="remain-total">'+totalWeight+' g</span></div>' +
            '<div class="progress-track"><div class="progress-fill '+pClass+'" style="width:'+remain+'%"></div></div>' +
        '</div></td>' +
        '<td class="col-status"><div class="status-cell">'+tagsHtml+'</div></td>' +
        '<td class="col-price">'+formatPrice(s.unit_price, s.price_currency)+'</td>' +
        '<td class="col-actions"><div class="row-actions">' +
            '<button class="action-icon" onclick="editSpool(\''+s.spool_id+'\')" title="详情"><svg viewBox="0 0 16 16" fill="none"><path d="M3 2.5h6.5L13 6v7.5H3V2.5z" stroke="currentColor" stroke-width="1.1"/><path d="M9.5 2.5V6H13" stroke="currentColor" stroke-width="1.1"/><circle cx="7.5" cy="9.5" r="2" stroke="currentColor" stroke-width="1.1"/><line x1="9" y1="11" x2="11" y2="13" stroke="currentColor" stroke-width="1.1"/></svg></button>' +
            '<button class="action-icon" onclick="addSimilar(\''+s.spool_id+'\')" title="添加"><svg viewBox="0 0 16 16" fill="none"><path d="M3 2.5h6.5L13 6v7.5H3V2.5z" stroke="currentColor" stroke-width="1.1"/><path d="M9.5 2.5V6H13" stroke="currentColor" stroke-width="1.1"/><line x1="8" y1="7.5" x2="8" y2="11.5" stroke="currentColor" stroke-width="1.2"/><line x1="6" y1="9.5" x2="10" y2="9.5" stroke="currentColor" stroke-width="1.2"/></svg></button>' +
            '<button class="action-icon" onclick="archiveOrDelete(\''+s.spool_id+'\')" title="删除"><svg viewBox="0 0 16 16" fill="none"><path d="M4 5h8l-.6 8H4.6L4 5z" stroke="currentColor" stroke-width="1.1"/><path d="M6 3h4" stroke="currentColor" stroke-width="1.1"/><path d="M3 5h10" stroke="currentColor" stroke-width="1.1"/><path d="M6.5 7v4M9.5 7v4" stroke="currentColor" stroke-width="1"/></svg></button>' +
        '</div></td>';
    return tr;
}

/* ===== Pagination ===== */
function renderPagination(total) {
    var el = document.getElementById("pagination");
    if (total <= g_page_size) { el.innerHTML = ""; return; }
    var pages = Math.ceil(total / g_page_size);
    if (g_page > pages) g_page = pages;
    var html = '<button class="page-btn" data-p="'+(g_page-1)+'"'+(g_page===1?" disabled":"")+'>‹</button>';
    var range = buildPageRange(g_page, pages);
    range.forEach(function(p) {
        if (p === "...") html += '<span class="page-dots">…</span>';
        else html += '<button class="page-btn'+(p===g_page?" active":"")+'" data-p="'+p+'">'+p+'</button>';
    });
    html += '<button class="page-btn" data-p="'+(g_page+1)+'"'+(g_page===pages?" disabled":"")+'>›</button>';
    html += '<select class="page-size-select" id="page-size-sel"><option value="20"'+(g_page_size===20?" selected":"")+'>20/页</option><option value="50"'+(g_page_size===50?" selected":"")+'>50/页</option><option value="100"'+(g_page_size===100?" selected":"")+'>100/页</option></select>';
    el.innerHTML = html;
}

function buildPageRange(cur, total) {
    if (total <= 7) { var a=[]; for(var i=1;i<=total;i++) a.push(i); return a; }
    var r = [1];
    if (cur > 3) r.push("...");
    for (var i=Math.max(2,cur-1); i<=Math.min(total-1,cur+1); i++) r.push(i);
    if (cur < total-2) r.push("...");
    if (total > 1) r.push(total);
    return r;
}

/* ===== Batch selection ===== */
function updateBatchUI() {
    var cnt = Object.keys(g_selected_ids).length;
    document.getElementById("btn-delete").disabled = cnt === 0;
    document.getElementById("check-all").checked = false;
}

/* ===== Row actions ===== */
function editSpool(id) {
    openDetail(id);
}

function addSimilar(id) {
    var s = g_spools.find(function(x) { return x.spool_id === id; });
    if (!s) return;
    g_editing_spool_id = null;
    openDialog({
        brand: s.brand, material_type: s.material_type, series: s.series,
        color_code: s.color_code, color_name: s.color_name,
        initial_weight: s.initial_weight, spool_weight: s.spool_weight,
        price_currency: s.price_currency, unit_price: s.unit_price
    });
}

function toggleFav(id) { sendRequest("toggle_favorite", { spool_id: id }, onSpoolsUpdated); }

function archiveOrDelete(id) {
    if (g_view === "archived") {
        if (confirm("确定删除这条耗材记录？")) sendRequest("remove_spool", { spool_id: id }, onSpoolsUpdated);
    } else {
        sendRequest("archive_spool", { spool_id: id }, onSpoolsUpdated);
    }
}

/* ===== Filter dropdown ===== */
function openFilterDropdown(btn, filterKey) {
    var dd = document.getElementById("filter-dropdown");
    var list = document.getElementById("filter-dropdown-list");
    var vals = {};
    g_spools.forEach(function(s) { if (s[filterKey]) vals[s[filterKey]] = true; });
    var items = Object.keys(vals).sort();

    list.innerHTML = '<div class="filter-dropdown-item'+((!g_filters[filterKey])?" active":"")+'" data-val="">全部</div>';
    items.forEach(function(v) {
        list.innerHTML += '<div class="filter-dropdown-item'+((g_filters[filterKey]===v)?" active":"")+'" data-val="'+esc(v)+'">'+esc(v)+'</div>';
    });

    var rect = btn.getBoundingClientRect();
    dd.style.left = rect.left + "px";
    dd.style.top = (rect.bottom + 4) + "px";
    dd.style.display = "block";
    dd._filterKey = filterKey;
    dd._btn = btn;
}

/* ===== Dialog ===== */
function openDialog(spool) {
    var isEdit = !!spool;
    document.getElementById("dialog-title").textContent = isEdit ? "编辑耗材" : "添加耗材";
    document.getElementById("dialog-confirm").textContent = isEdit ? "保存" : "添加";
    document.getElementById("quantity-control").style.display = isEdit ? "none" : "flex";
    g_quantity = 1;
    document.getElementById("qty-value").textContent = "1";

    populateDropdowns();
    renderColorPalette(spool ? spool.color_code : "");

    if (isEdit) {
        setVal("form-brand", spool.brand);
        populateTypeDropdown();
        setVal("form-type", spool.material_type);
        populateSeriesDropdown();
        setVal("form-series", spool.series);
        document.getElementById("form-color-code").value = spool.color_code || "";
        setVal("form-color-name", spool.color_name);
        setVal("form-total-weight", spool.initial_weight || 1000);
        setVal("form-spool-weight", spool.spool_weight || 250);
        calcNetWeight();
        setVal("form-remain-alert", spool.remain_alert_pct || 0);
        setVal("form-dry-date", spool.dry_date);
        setVal("form-dry-reminder", spool.dry_reminder_days || 0);
        setVal("form-currency", spool.price_currency);
        setVal("form-price", spool.unit_price || "");
        setVal("form-note", spool.note);
        updateCharCount();
    } else {
        ["form-brand","form-type","form-series","form-color-name","form-dry-date","form-currency","form-price","form-note"].forEach(function(id) { setVal(id, ""); });
        document.getElementById("form-color-code").value = "";
        setVal("form-total-weight", 1000);
        setVal("form-spool-weight", 250);
        calcNetWeight();
        setVal("form-remain-alert", 0);
        setVal("form-dry-reminder", 0);
        updateCharCount();
    }

    document.getElementById("advanced-section").style.display = "none";
    document.getElementById("advanced-toggle").classList.remove("open");
    g_ams_selected_slot = null;
    g_ams_selected_unit = null;
    setAmsFormReadonly(false);
    switchDialogMode("manual");
    document.getElementById("dialog-overlay").style.display = "flex";
    validateForm();
}

function closeDialog() {
    document.getElementById("dialog-overlay").style.display = "none";
    g_editing_spool_id = null;
    g_ams_selected_slot = null;
    setAmsFormReadonly(false);
}

function switchDialogMode(mode) {
    g_dialog_mode = mode;
    document.querySelectorAll(".dialog-tab").forEach(function(t) {
        t.classList.toggle("active", t.getAttribute("data-mode") === mode);
    });
    document.getElementById("ams-section").style.display = mode === "ams" ? "block" : "none";
    document.getElementById("dialog-body").style.display = mode === "manual" ? "block" : (g_ams_selected_slot ? "block" : "none");

    if (mode === "ams") {
        g_ams_selected_slot = null;
        document.getElementById("ams-device-area").style.display = "none";
        document.getElementById("ams-empty").style.display = "flex";
        var emptyText = document.querySelector(".ams-empty-text");
        if (emptyText) emptyText.textContent = "正在获取设备信息…";
        sendRequest("get_machine_list", {}, onMachineListResponse);
    }
}

function submitDialog() {
    var data = {
        brand:           getVal("form-brand"),
        material_type:   getVal("form-type"),
        series:          getVal("form-series"),
        color_code:      document.getElementById("form-color-code").value,
        color_name:      getVal("form-color-name"),
        initial_weight:  parseFloat(getVal("form-total-weight")) || 1000,
        spool_weight:    parseFloat(getVal("form-spool-weight")) || 0,
        net_weight:      parseFloat(getVal("form-net-weight")) || 0,
        remain_alert_pct: parseInt(getVal("form-remain-alert")) || 0,
        dry_date:        getVal("form-dry-date"),
        dry_reminder_days: parseInt(getVal("form-dry-reminder")) || 0,
        price_currency:  getVal("form-currency"),
        unit_price:      parseFloat(getVal("form-price")) || 0,
        note:            getVal("form-note")
    };
    if (g_editing_spool_id) {
        data.spool_id = g_editing_spool_id;
        sendRequest("update_spool", data, onSpoolsUpdated);
    } else if (g_dialog_mode === "ams" && g_ams_selected_slot) {
        var tray = g_ams_selected_slot.tray;
        data.entry_method = "ams_sync";
        data.tag_uid = tray.tag_uid || "";
        data.setting_id = tray.setting_id || "";
        data.bound_ams_id = g_ams_selected_slot.ams_id || "";
        data.bound_dev_id = (g_ams_data && g_ams_data.dev_id) || "";
        data.remain_percent = tray.remain || 0;
        sendRequest("add_spool", data, onSpoolsUpdated);
    } else if (g_quantity > 1) {
        data.entry_method = "manual";
        sendRequest("batch_add", { spool: data, quantity: g_quantity }, onSpoolsUpdated);
    } else {
        data.entry_method = "manual";
        sendRequest("add_spool", data, onSpoolsUpdated);
    }
    closeDialog();
}

/* ===== Color palette ===== */
function renderColorPalette(selectedColor) {
    var el = document.getElementById("color-palette");
    el.innerHTML = "";
    var addBtn = document.createElement("div");
    addBtn.className = "color-swatch-add";
    addBtn.innerHTML = '+<input type="color" id="custom-color-picker">';
    el.appendChild(addBtn);

    BAMBU_COLORS.forEach(function(c) {
        var sw = document.createElement("div");
        sw.className = "color-swatch" + (c.toUpperCase() === (selectedColor||"").toUpperCase() ? " selected" : "");
        sw.style.background = c;
        sw.setAttribute("data-color", c);
        sw.addEventListener("click", function() { selectColor(c); });
        el.appendChild(sw);
    });

    addBtn.querySelector("input").addEventListener("change", function(e) {
        selectColor(e.target.value);
    });
}

function selectColor(c) {
    document.getElementById("form-color-code").value = c;
    document.querySelectorAll(".color-swatch").forEach(function(sw) {
        sw.classList.toggle("selected", sw.getAttribute("data-color") && sw.getAttribute("data-color").toUpperCase() === c.toUpperCase());
    });
    validateForm();
}

/* ===== Helpers ===== */
function populateDropdowns() {
    var brands = g_preset_vendors.map(function(v) { return v.name; }).sort();
    fillSelect("form-brand", brands, "选择品牌");
    populateTypeDropdown();
}

function getTypesForBrand(brand) {
    if (brand) {
        var vendor = g_preset_vendors.find(function(v) { return v.name === brand; });
        return vendor ? vendor.types || [] : [];
    }
    var seen = {}, result = [];
    g_preset_vendors.forEach(function(v) {
        (v.types || []).forEach(function(t) {
            if (!seen[t.name]) { seen[t.name] = true; result.push(t); }
        });
    });
    return result;
}

function populateTypeDropdown() {
    var brand = getVal("form-brand");
    var typeObjs = getTypesForBrand(brand);
    var names = typeObjs.map(function(t) { return t.name; }).sort();
    var prev = getVal("form-type");
    fillSelect("form-type", names, "选择类型");
    if (names.indexOf(prev) !== -1) setVal("form-type", prev);
    populateSeriesDropdown();
}

function populateSeriesDropdown() {
    var brand = getVal("form-brand");
    var type  = getVal("form-type");
    var seriesList = [];
    var seen = {};

    var typeObjs = getTypesForBrand(brand);
    typeObjs.forEach(function(t) {
        if (type && t.name !== type) return;
        (t.series || []).forEach(function(s) {
            if (s && !seen[s]) { seen[s] = true; seriesList.push(s); }
        });
    });
    seriesList.sort();

    var prev = getVal("form-series");
    fillSelect("form-series", seriesList, "选择系列");
    if (seriesList.indexOf(prev) !== -1) setVal("form-series", prev);
}

function fillSelect(id, items, placeholder) {
    var sel = document.getElementById(id);
    sel.innerHTML = '<option value="">'+placeholder+'</option>';
    items.forEach(function(v) { sel.innerHTML += '<option value="'+esc(v)+'">'+esc(v)+'</option>'; });
}

function calcNetWeight() {
    var tw = parseFloat(getVal("form-total-weight")) || 0;
    var sw = parseFloat(getVal("form-spool-weight")) || 0;
    setVal("form-net-weight", Math.max(0, tw - sw));
    validateForm();
}

function validateForm() {
    var brand = getVal("form-brand");
    var type  = getVal("form-type");
    var series = getVal("form-series");
    var color = document.getElementById("form-color-code").value;
    var weight = parseFloat(getVal("form-total-weight")) || 0;
    var valid = brand && type && series && color && weight > 0;
    var btn = document.getElementById("dialog-confirm");
    btn.disabled = !valid;
}

function updateCharCount() {
    var note = getVal("form-note") || "";
    document.getElementById("char-count").textContent = note.length + "/50";
}

function setVal(id, v) { document.getElementById(id).value = v == null ? "" : v; }
function getVal(id) { return document.getElementById(id).value; }
function esc(s) { return s == null ? "" : String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;"); }

/* ===== Detail Dialog ===== */
var g_detail_spool_id = null;

function openDetail(id) {
    var s = g_spools.find(function(x) { return x.spool_id === id; });
    if (!s) return;
    g_detail_spool_id = id;

    var nameParts = (s.material_type||"") + (s.series ? " "+s.series : "");
    document.getElementById("det-spool-name").textContent = nameParts || "—";
    var icon = document.getElementById("det-spool-icon");
    icon.style.setProperty("--spool-color", s.color_code || "#888");
    icon.innerHTML = buildSpoolSvg(s.color_code) + '<div class="color-indicator"></div>';
    var entryTag = document.getElementById("det-entry-tag");
    var entryMap = {manual:"手动", ams_sync:"自动", rfid:"RFID"};
    entryTag.textContent = entryMap[s.entry_method] || "";
    document.getElementById("det-spool-sub").textContent = (s.diameter || 1.75) + " mm｜" + (s.color_name || "—");

    document.getElementById("det-v-brand").textContent = s.brand || "—";
    document.getElementById("det-v-type").textContent = s.material_type || "—";
    document.getElementById("det-v-series").textContent = s.series || "—";
    var colorEl = document.getElementById("det-v-color");
    colorEl.style.background = s.color_code || "#888";
    var tw = s.initial_weight || 0, sw = s.spool_weight || 0;
    document.getElementById("det-v-tw").textContent = tw + " g";
    document.getElementById("det-v-sw").textContent = sw + " g";
    document.getElementById("det-v-nw").textContent = Math.max(0, tw - sw) + " g";
    document.getElementById("det-v-param1").textContent = s.brand ? (s.brand + " " + (s.material_type||"") + " " + (s.series||"")) : "—";
    var alertMap = {0:"—", 10:"≦ 10g", 20:"≦ 20g", 30:"≦ 30g", 50:"≦ 50g"};
    document.getElementById("det-v-remain-alert").textContent = alertMap[s.remain_alert_pct] || "—";
    document.getElementById("det-v-dry-date").textContent = s.dry_date || "—";
    var dryMap = {0:"—", 7:"每周", 14:"每两周", 30:"每月", 60:"每两月"};
    document.getElementById("det-v-dry-reminder").textContent = dryMap[s.dry_reminder_days] || "—";
    var priceStr = "—";
    if (s.unit_price) {
        var csym = CURRENCY_SYMBOLS[s.price_currency] || "";
        var cname = s.price_currency ? s.price_currency + " (" + csym + ")" : "";
        priceStr = cname + " " + s.unit_price.toFixed(2) + "/kg";
    }
    document.getElementById("det-v-price").textContent = priceStr;
    document.getElementById("det-v-note").textContent = s.note || "—";

    updateDetailNav();
    switchDetailTab("info");
    document.getElementById("detail-overlay").style.display = "flex";
}

function closeDetail() {
    document.getElementById("detail-overlay").style.display = "none";
    g_detail_spool_id = null;
}

function updateDetailNav() {
    var list = getFilteredSpools();
    var idx = -1;
    for (var i = 0; i < list.length; i++) {
        if (list[i].spool_id === g_detail_spool_id) { idx = i; break; }
    }
    document.getElementById("det-prev").disabled = idx <= 0;
    document.getElementById("det-next").disabled = idx < 0 || idx >= list.length - 1;
}

function navigateDetail(dir) {
    var list = getFilteredSpools();
    var idx = -1;
    for (var i = 0; i < list.length; i++) {
        if (list[i].spool_id === g_detail_spool_id) { idx = i; break; }
    }
    var next = idx + dir;
    if (next >= 0 && next < list.length) {
        openDetail(list[next].spool_id);
    }
}

function switchDetailTab(tab) {
    document.querySelectorAll(".detail-tab").forEach(function(t) {
        t.classList.toggle("active", t.getAttribute("data-dtab") === tab);
    });
    document.getElementById("dtab-info").style.display = tab === "info" ? "block" : "none";
    document.getElementById("dtab-usage").style.display = tab === "usage" ? "block" : "none";
}


/* ===== AMS Section ===== */
function buildSmallSpoolSvg(color) {
    var c = color || "#888";
    return '<svg width="32" height="32" viewBox="0 0 40 40" fill="none">' +
        '<path d="M5 8C5 5.8 6.8 4 9 4h5v32H9C6.8 36 5 34.2 5 32V8z" fill="'+c+'" opacity="0.55"/>' +
        '<rect x="10" y="7" width="8" height="26" rx="1" fill="'+c+'" opacity="0.75"/>' +
        '<ellipse cx="24" cy="20" rx="12" ry="16" fill="'+c+'"/>' +
        '<ellipse cx="24" cy="20" rx="8" ry="10.5" fill="'+c+'" opacity="0.55"/>' +
        '<ellipse cx="25" cy="20" rx="3" ry="4" fill="#1a1a1a" opacity="0.85"/>' +
    '</svg>';
}

function buildAmsUnitIcon(amsUnit, isActive) {
    var trays = amsUnit.trays || [];
    var colors = trays.map(function(t) { return (t.is_exists && t.color) ? t.color : "rgba(255,255,255,0.1)"; });
    while (colors.length < 4) colors.push("rgba(255,255,255,0.1)");
    return '<svg width="20" height="20" viewBox="0 0 20 20" fill="none">' +
        '<rect x="1" y="1" width="18" height="18" rx="3" stroke="' + (isActive ? "var(--brand)" : "currentColor") + '" stroke-width="1.2" fill="none"/>' +
        '<rect x="3" y="3" width="6" height="6" rx="1" fill="'+colors[0]+'"/>' +
        '<rect x="11" y="3" width="6" height="6" rx="1" fill="'+colors[1]+'"/>' +
        '<rect x="3" y="11" width="6" height="6" rx="1" fill="'+colors[2]+'"/>' +
        '<rect x="11" y="11" width="6" height="6" rx="1" fill="'+colors[3]+'"/>' +
    '</svg>';
}

function renderAmsSection() {
    var units = (g_ams_data && g_ams_data.ams_units) || [];
    if (units.length === 0) {
        document.getElementById("ams-unit-icons").innerHTML = "";
        document.getElementById("ams-slots").innerHTML =
            '<div class="ams-empty-inline">该设备未识别到 AMS</div>';
        document.getElementById("dialog-body").style.display = "none";
        return;
    }

    if (!g_ams_selected_unit || !units.find(function(u) { return u.ams_id === g_ams_selected_unit; })) {
        g_ams_selected_unit = units[0].ams_id;
    }

    var iconContainer = document.getElementById("ams-unit-icons");
    iconContainer.innerHTML = "";
    units.forEach(function(u) {
        var btn = document.createElement("button");
        btn.className = "ams-unit-btn" + (u.ams_id === g_ams_selected_unit ? " active" : "");
        btn.title = "AMS " + (parseInt(u.ams_id) + 1);
        btn.innerHTML = buildAmsUnitIcon(u, u.ams_id === g_ams_selected_unit);
        btn.addEventListener("click", function() {
            g_ams_selected_unit = u.ams_id;
            g_ams_selected_slot = null;
            renderAmsSection();
            document.getElementById("dialog-body").style.display = "none";
        });
        iconContainer.appendChild(btn);
    });

    var currentUnit = units.find(function(u) { return u.ams_id === g_ams_selected_unit; });
    renderAmsSlots(currentUnit);
}

function renderAmsSlots(amsUnit) {
    var container = document.getElementById("ams-slots");
    container.innerHTML = "";
    if (!amsUnit) return;

    var trays = amsUnit.trays || [];
    var slotLabels = ["A1","A2","A3","A4"];

    trays.forEach(function(tray, i) {
        var div = document.createElement("div");
        var label = slotLabels[i] || ("A" + (i+1));
        var isSelected = g_ams_selected_slot && g_ams_selected_slot.slot_id === tray.slot_id && g_ams_selected_slot.ams_id === amsUnit.ams_id;

        if (tray.is_exists) {
            div.className = "ams-slot" + (isSelected ? " active" : "");
            var colorDot = '<span class="ams-fila-dot" style="background:' + esc(tray.color || "#888") + '"></span>';
            div.innerHTML =
                '<div class="ams-slot-header">' + label + '</div>' +
                '<div class="ams-slot-body">' +
                    '<div class="ams-spool-icon">' + buildSmallSpoolSvg(tray.color) + '</div>' +
                    '<div class="ams-slot-info">' +
                        '<div class="ams-slot-name">' + colorDot + esc(tray.fila_type || "—") + '</div>' +
                        '<div class="ams-slot-weight">' + esc(tray.weight ? tray.weight + "g" : "—") + '</div>' +
                    '</div>' +
                '</div>';

            div.addEventListener("click", function() {
                g_ams_selected_slot = {
                    ams_id: amsUnit.ams_id,
                    slot_id: tray.slot_id,
                    tray: tray
                };
                renderAmsSlots(amsUnit);
                fillFormFromAms(tray);
            });
        } else {
            div.className = "ams-slot empty-slot";
            div.innerHTML =
                '<div class="ams-slot-header">' + label + '</div>' +
                '<div class="ams-slot-body ams-slot-body-empty">' +
                    '<div class="ams-spool-icon ams-spool-empty">' + buildSmallSpoolSvg("#555") + '</div>' +
                    '<div class="ams-slot-info"><div class="ams-slot-name" style="color:var(--text-detail)">空</div></div>' +
                '</div>';
        }
        container.appendChild(div);
    });
}

function fillFormFromAms(tray) {
    document.getElementById("dialog-body").style.display = "block";
    var hasUuid = !!(tray.tag_uid && tray.tag_uid.length > 0);

    var brandName = tray.sub_brands || "";
    var fullType  = tray.fila_type || "";

    populateDropdowns();
    setVal("form-brand", brandName);
    populateTypeDropdown();

    var typeEl = document.getElementById("form-type");
    var matchedType = "";
    var seriesRemainder = "";
    for (var i = 0; i < typeEl.options.length; i++) {
        var opt = typeEl.options[i].value;
        if (!opt) continue;
        if (fullType === opt) { matchedType = opt; break; }
        if (fullType.indexOf(opt) === 0) {
            if (!matchedType || opt.length > matchedType.length) {
                matchedType = opt;
                seriesRemainder = fullType.substring(opt.length).trim();
            }
        }
    }
    setVal("form-type", matchedType);
    populateSeriesDropdown();
    if (seriesRemainder) setVal("form-series", seriesRemainder);

    if (tray.color) {
        document.getElementById("form-color-code").value = tray.color;
        renderColorPalette(tray.color);
    }

    var w = parseInt(tray.weight) || 1000;
    setVal("form-total-weight", w);
    setVal("form-spool-weight", 250);
    calcNetWeight();

    setAmsFormReadonly(hasUuid);
    validateForm();
}

function setAmsFormReadonly(readonly) {
    var fields = ["form-brand","form-type","form-series","form-color-name",
                  "form-total-weight","form-spool-weight"];
    fields.forEach(function(id) {
        var el = document.getElementById(id);
        if (!el) return;
        if (readonly) {
            el.setAttribute("disabled", "disabled");
            el.classList.add("ams-readonly");
        } else {
            el.removeAttribute("disabled");
            el.classList.remove("ams-readonly");
        }
    });
    var palette = document.getElementById("color-palette");
    if (palette) {
        palette.style.pointerEvents = readonly ? "none" : "";
        palette.style.opacity = readonly ? "0.5" : "";
    }
}

/* ===== Event bindings ===== */
document.addEventListener("DOMContentLoaded", function() {
    // Sidebar
    document.querySelectorAll(".nav-item").forEach(function(el) {
        el.addEventListener("click", function() {
            g_view = this.getAttribute("data-view");
            document.querySelectorAll(".nav-item").forEach(function(n) { n.classList.remove("active"); });
            this.classList.add("active");
            g_selected_ids = {};
            document.getElementById("stats-view").style.display = (g_view === "stats") ? "" : "none";
            document.getElementById("list-view").style.display = (g_view === "stats") ? "none" : "";
            refresh();
        });
    });

    // Tabs
    document.querySelectorAll(".tab-pill").forEach(function(el) {
        el.addEventListener("click", function() {
            g_tab = this.getAttribute("data-tab");
            document.querySelectorAll(".tab-pill").forEach(function(t) { t.classList.remove("active"); });
            this.classList.add("active");
            refresh();
        });
    });

    // Sort
    document.querySelectorAll("th.sortable").forEach(function(th) {
        th.addEventListener("click", function() {
            var key = this.getAttribute("data-sort");
            if (g_sort_key === key) { g_sort_asc = !g_sort_asc; }
            else { g_sort_key = key; g_sort_asc = true; }
            document.querySelectorAll("th.sortable").forEach(function(h) { h.classList.remove("sort-asc","sort-desc"); });
            this.classList.add(g_sort_asc ? "sort-asc" : "sort-desc");
            refresh();
        });
    });

    // Check all
    document.getElementById("check-all").addEventListener("change", function() {
        var checked = this.checked;
        var rows = getFilteredSpools();
        g_selected_ids = {};
        if (checked) rows.forEach(function(s) { g_selected_ids[s.spool_id] = true; });
        refresh();
    });

    // Row checkbox delegation
    document.getElementById("spool-tbody").addEventListener("change", function(e) {
        if (e.target.type === "checkbox") {
            var id = e.target.getAttribute("data-id");
            if (e.target.checked) g_selected_ids[id] = true;
            else delete g_selected_ids[id];
            e.target.closest("tr").classList.toggle("selected", e.target.checked);
            updateBatchUI();
        }
    });

    // Batch delete
    document.getElementById("btn-delete").addEventListener("click", function() {
        var ids = Object.keys(g_selected_ids);
        if (!ids.length) return;
        if (confirm("确定删除选中的 " + ids.length + " 条记录？")) {
            sendRequest("batch_remove", { spool_ids: ids }, onSpoolsUpdated);
            g_selected_ids = {};
        }
    });

    // Search
    document.getElementById("search-input").addEventListener("input", function() { refresh(); });

    // Filter buttons
    document.querySelectorAll(".filter-btn").forEach(function(btn) {
        btn.addEventListener("click", function(e) {
            e.stopPropagation();
            var key = this.getAttribute("data-filter");
            var dd = document.getElementById("filter-dropdown");
            if (dd.style.display === "block" && dd._filterKey === key) {
                dd.style.display = "none"; return;
            }
            openFilterDropdown(this, key);
        });
    });

    // Filter dropdown item click
    document.getElementById("filter-dropdown").addEventListener("click", function(e) {
        var item = e.target.closest(".filter-dropdown-item");
        if (!item) return;
        var val = item.getAttribute("data-val");
        var key = this._filterKey;
        if (val) g_filters[key] = val; else delete g_filters[key];
        if (this._btn) this._btn.classList.toggle("active", !!val);
        this.style.display = "none";
        refresh();
    });

    // Close dropdown on outside click
    document.addEventListener("click", function() {
        document.getElementById("filter-dropdown").style.display = "none";
    });

    // Group toggle
    document.getElementById("btn-group").addEventListener("click", function() {
        g_grouped = !g_grouped;
        g_collapsed_groups = {};
        g_page = 1;
        this.classList.toggle("btn-group-active", g_grouped);
        refresh();
    });

    // Pagination delegation
    document.getElementById("pagination").addEventListener("click", function(e) {
        var btn = e.target.closest(".page-btn");
        if (!btn || btn.disabled) return;
        g_page = parseInt(btn.getAttribute("data-p")) || 1;
        refresh();
    });
    document.getElementById("pagination").addEventListener("change", function(e) {
        if (e.target.id === "page-size-sel") {
            g_page_size = parseInt(e.target.value) || 50;
            g_page = 1;
            refresh();
        }
    });

    // Add button
    document.getElementById("btn-add").addEventListener("click", function() {
        g_editing_spool_id = null;
        openDialog(null);
    });

    // Dialog
    document.getElementById("dialog-close").addEventListener("click", closeDialog);
    document.getElementById("dialog-cancel").addEventListener("click", closeDialog);
    document.getElementById("dialog-confirm").addEventListener("click", submitDialog);

    // AMS device selector
    document.getElementById("ams-device-select").addEventListener("change", function() {
        var devId = this.value;
        g_ams_selected_unit = null;
        g_ams_selected_slot = null;
        document.getElementById("ams-unit-icons").innerHTML = "";
        document.getElementById("ams-slots").innerHTML =
            '<div class="ams-empty-inline">正在加载 AMS 数据…</div>';
        document.getElementById("dialog-body").style.display = "none";
        sendRequest("get_ams_data", { dev_id: devId }, onAmsDataResponse);
    });

    document.querySelectorAll(".dialog-tab").forEach(function(t) {
        t.addEventListener("click", function() { switchDialogMode(this.getAttribute("data-mode")); });
    });

    // Brand → Type → Series linkage
    document.getElementById("form-brand").addEventListener("change", function() {
        setVal("form-type", "");
        setVal("form-series", "");
        populateTypeDropdown();
        validateForm();
    });
    document.getElementById("form-type").addEventListener("change", function() {
        setVal("form-series", "");
        populateSeriesDropdown();
        validateForm();
    });
    document.getElementById("form-series").addEventListener("change", function() {
        validateForm();
    });

    // Weight calc
    document.getElementById("form-total-weight").addEventListener("input", calcNetWeight);
    document.getElementById("form-spool-weight").addEventListener("input", calcNetWeight);

    // Note char count
    document.getElementById("form-note").addEventListener("input", updateCharCount);

    // Quantity
    document.getElementById("qty-minus").addEventListener("click", function() {
        g_quantity = Math.max(1, g_quantity - 1);
        document.getElementById("qty-value").textContent = g_quantity;
    });
    document.getElementById("qty-plus").addEventListener("click", function() {
        g_quantity = Math.min(99, g_quantity + 1);
        document.getElementById("qty-value").textContent = g_quantity;
    });

    // Advanced toggle
    document.getElementById("advanced-toggle").addEventListener("click", function() {
        var sec = document.getElementById("advanced-section");
        var open = sec.style.display !== "none";
        sec.style.display = open ? "none" : "block";
        this.classList.toggle("open", !open);
    });

    // Detail dialog
    document.getElementById("det-close").addEventListener("click", closeDetail);
    document.getElementById("det-prev").addEventListener("click", function() { navigateDetail(-1); });
    document.getElementById("det-next").addEventListener("click", function() { navigateDetail(1); });
    document.querySelectorAll(".detail-tab").forEach(function(t) {
        t.addEventListener("click", function() { switchDetailTab(this.getAttribute("data-dtab")); });
    });
    document.getElementById("det-edit-btn").addEventListener("click", function() {
        if (!g_detail_spool_id) return;
        var s = g_spools.find(function(x) { return x.spool_id === g_detail_spool_id; });
        closeDetail();
        if (s) {
            g_editing_spool_id = s.spool_id;
            openDialog(s);
        }
    });

    // Reminder tabs
    document.querySelectorAll(".reminder-tab").forEach(function(t) {
        t.addEventListener("click", function() {
            document.querySelectorAll(".reminder-tab").forEach(function(r) { r.classList.remove("active"); });
            this.classList.add("active");
            renderReminderList(this.getAttribute("data-rtab"));
        });
    });

    // Heatmap month selector
    var hmSel = document.getElementById("heatmap-month");
    if (hmSel) {
        var now = new Date();
        for (var m = 0; m < 6; m++) {
            var d = new Date(now.getFullYear(), now.getMonth() - m, 1);
            var opt = document.createElement("option");
            opt.value = d.getFullYear() + "-" + String(d.getMonth()+1).padStart(2,"0");
            opt.textContent = d.getFullYear() + "/" + (d.getMonth()+1);
            hmSel.appendChild(opt);
        }
        hmSel.addEventListener("change", function() { renderHeatmap(); });
    }

    // Init: single request, C++ responds with theme + spools + presets
    sendRequest("init", {}, function(code, data) {
        if (code !== 0) return;
        var theme = (data && data.theme) || "dark";
        document.documentElement.dataset.theme = theme;
        g_spools = (data && data.spools) || [];
        g_preset_vendors = (data && data.presets && data.presets.vendors) || [];
        refresh();
    });
});

/* ===== Statistics ===== */
function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}
var PIE_COLORS = ["#8BC34A","#4CAF50","#009688","#3F51B5","#FF9800","#F44336","#9C27B0","#00BCD4","#FFC107","#795548"];

function renderStats() {
    var spools = g_spools.filter(function(s) { return s.status !== "archived"; });
    var totalValue = 0, colorSet = {};
    spools.forEach(function(s) {
        var nw = (s.initial_weight || 0) - (s.spool_weight || 0);
        totalValue += (s.unit_price || 0) * Math.max(0, nw) / 1000;
        if (s.color_name) colorSet[s.color_name] = true;
    });
    document.getElementById("stat-total-value").textContent = "$ " + totalValue.toFixed(2);
    document.getElementById("stat-total-count").textContent = spools.length;
    document.getElementById("stat-color-count").textContent = Object.keys(colorSet).length;

    renderPieChart("pie-type", "legend-type", countBy(spools, "material_type"), null);
    var colorData = countByColor(spools);
    renderPieChart("pie-color", "legend-color", colorData, null);
    renderReminderList("low");
    renderHeatmap();
    renderLineChart();
    renderBarChart();
}

function countByColor(arr) {
    var map = {};
    arr.forEach(function(s) {
        var name = s.color_name || "其他";
        if (!map[name]) map[name] = {count: 0, color: s.color_code || "#888"};
        map[name].count++;
    });
    var result = [];
    for (var k in map) result.push({name: k, value: map[k].count, color: map[k].color});
    result.sort(function(a, b) { return b.value - a.value; });
    return result;
}

function countBy(arr, key) {
    var map = {};
    arr.forEach(function(s) {
        var v = s[key] || "其他";
        map[v] = (map[v] || 0) + 1;
    });
    var result = [];
    for (var k in map) result.push({name: k, value: map[k]});
    result.sort(function(a, b) { return b.value - a.value; });
    return result;
}

function renderPieChart(canvasId, legendId, data) {
    var canvas = document.getElementById(canvasId);
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var w = canvas.width, h = canvas.height;
    var cx = w/2, cy = h/2, r = Math.min(w,h)/2 - 8, ri = r * 0.55;
    ctx.clearRect(0, 0, w, h);
    var total = 0;
    data.forEach(function(d) { total += d.value; });
    if (total === 0) return;
    var angle = -Math.PI / 2;
    data.forEach(function(d, i) {
        var slice = (d.value / total) * Math.PI * 2;
        ctx.beginPath();
        ctx.moveTo(cx + ri * Math.cos(angle), cy + ri * Math.sin(angle));
        ctx.arc(cx, cy, r, angle, angle + slice);
        ctx.arc(cx, cy, ri, angle + slice, angle, true);
        ctx.closePath();
        ctx.fillStyle = d.color || PIE_COLORS[i % PIE_COLORS.length];
        ctx.fill();
        d._color = ctx.fillStyle;
        angle += slice;
    });
    var legend = document.getElementById(legendId);
    if (!legend) return;
    legend.innerHTML = "";
    data.slice(0, 6).forEach(function(d) {
        var pct = ((d.value / total) * 100).toFixed(1);
        legend.innerHTML += '<div class="pie-legend-item"><span class="pie-legend-dot" style="background:'+d._color+'"></span><span class="pie-legend-name">'+esc(d.name)+'</span><span class="pie-legend-pct">'+pct+'%</span></div>';
    });
}

function renderReminderList(tab) {
    var list = document.getElementById("reminder-list");
    if (!list) return;
    var spools = g_spools.filter(function(s) { return s.status !== "archived"; });
    var filtered;
    if (tab === "low") {
        filtered = spools.filter(function(s) { return (s.remain_percent||0) > 0 && (s.remain_percent||0) <= 20; });
    } else if (tab === "dry") {
        filtered = spools.filter(function(s) {
            if (!s.dry_date || !s.dry_reminder_days) return false;
            var d = new Date(s.dry_date);
            d.setDate(d.getDate() + s.dry_reminder_days);
            return d <= new Date();
        });
    } else {
        filtered = spools.filter(function(s) { return (s.remain_percent||0) === 0; });
    }
    if (filtered.length === 0) {
        list.innerHTML = '<div style="padding:24px;text-align:center;color:var(--text-detail)">暂无提醒</div>';
        return;
    }
    list.innerHTML = "";
    filtered.forEach(function(s) {
        var nameParts = (s.material_type||"") + (s.series ? " "+s.series : "");
        var item = document.createElement("div");
        item.className = "reminder-item";
        item.innerHTML = '<div class="spool-icon" style="--spool-color:'+esc(s.color_code||"#888")+'">'+buildSpoolSvg(s.color_code)+'</div>' +
            '<div class="reminder-info"><div class="reminder-name">'+esc(nameParts||"—")+'</div><div class="reminder-sub">'+esc(s.color_name||"—")+' | '+(s.diameter||1.75)+' mm</div></div>';
        item.addEventListener("click", function() { openDetail(s.spool_id); });
        list.appendChild(item);
    });
}

function renderHeatmap() {
    var wrap = document.getElementById("heatmap-wrap");
    if (!wrap) return;
    var sel = document.getElementById("heatmap-month");
    var parts = (sel ? sel.value : "").split("-");
    var year = parseInt(parts[0]) || new Date().getFullYear();
    var month = parseInt(parts[1]) || (new Date().getMonth() + 1);
    var firstDay = new Date(year, month - 1, 1);
    var daysInMonth = new Date(year, month, 0).getDate();
    var startDow = (firstDay.getDay() + 6) % 7;
    var dayLabels = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"];

    var html = '<div class="heatmap-header">';
    dayLabels.forEach(function(d) { html += '<span>'+d+'</span>'; });
    html += '</div><div class="heatmap-body">';
    for (var i = 0; i < startDow; i++) html += '<div class="heatmap-cell" style="visibility:hidden"></div>';
    for (var d = 1; d <= daysInMonth; d++) {
        var level = Math.floor(Math.random() * 5);
        var cls = level > 0 ? " l"+level : "";
        html += '<div class="heatmap-cell'+cls+'" title="'+year+'/'+month+'/'+d+'"></div>';
    }
    html += '</div>';
    wrap.innerHTML = html;
}

function renderLineChart() {
    var canvas = document.getElementById("chart-line");
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var w = canvas.width, h = canvas.height;
    var pad = {top:20, right:20, bottom:30, left:40};
    ctx.clearRect(0, 0, w, h);

    var days = 7;
    var data = [];
    for (var i = 0; i < days; i++) {
        data.push(Math.round(Math.random() * 150 + 20));
    }
    var max = Math.max.apply(null, data) * 1.2;
    var chartW = w - pad.left - pad.right, chartH = h - pad.top - pad.bottom;

    var gridColor = cssVar("--chart-grid");
    var labelColor = cssVar("--chart-text");
    ctx.strokeStyle = gridColor;
    ctx.lineWidth = 1;
    for (var i = 0; i <= 4; i++) {
        var y = pad.top + (chartH / 4) * i;
        ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(w - pad.right, y); ctx.stroke();
        ctx.fillStyle = labelColor;
        ctx.font = "11px sans-serif";
        ctx.textAlign = "right";
        ctx.fillText(Math.round(max - (max/4)*i), pad.left - 6, y + 4);
    }

    var now = new Date();
    ctx.textAlign = "center";
    ctx.fillStyle = labelColor;
    data.forEach(function(v, i) {
        var d = new Date(now.getTime() - (days-1-i)*86400000);
        var x = pad.left + (chartW / (days-1)) * i;
        ctx.fillText((d.getMonth()+1)+"/"+d.getDate(), x, h - 6);
    });

    ctx.beginPath();
    ctx.strokeStyle = "#50e81d";
    ctx.lineWidth = 2;
    ctx.lineJoin = "round";
    data.forEach(function(v, i) {
        var x = pad.left + (chartW / (days-1)) * i;
        var y = pad.top + chartH - (v / max) * chartH;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
}

function renderBarChart() {
    var canvas = document.getElementById("chart-bar");
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var w = canvas.width, h = canvas.height;
    var pad = {top:10, right:20, bottom:30, left:40};
    ctx.clearRect(0, 0, w, h);

    var days = 7;
    var types = ["PLA","ABS","PETG"];
    var colors = ["#4CAF50","#FF9800","#2196F3"];
    var barData = [];
    for (var i = 0; i < days; i++) {
        var group = [];
        types.forEach(function() { group.push(Math.round(Math.random() * 120 + 30)); });
        barData.push(group);
    }
    var max = 0;
    barData.forEach(function(g) { g.forEach(function(v) { if (v > max) max = v; }); });
    max *= 1.2;
    var chartW = w - pad.left - pad.right, chartH = h - pad.top - pad.bottom;

    var gridColor2 = cssVar("--chart-grid");
    var labelColor2 = cssVar("--chart-text");
    ctx.strokeStyle = gridColor2;
    ctx.lineWidth = 1;
    for (var i = 0; i <= 4; i++) {
        var y = pad.top + (chartH / 4) * i;
        ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(w - pad.right, y); ctx.stroke();
        ctx.fillStyle = labelColor2;
        ctx.font = "11px sans-serif";
        ctx.textAlign = "right";
        ctx.fillText(Math.round(max - (max/4)*i), pad.left - 6, y + 4);
    }

    var groupW = chartW / days;
    var barW = Math.min(16, (groupW - 8) / types.length);
    var now = new Date();
    ctx.textAlign = "center";
    barData.forEach(function(group, gi) {
        var gx = pad.left + groupW * gi + groupW / 2;
        var d = new Date(now.getTime() - (days-1-gi)*86400000);
        ctx.fillStyle = labelColor2;
        ctx.fillText((d.getMonth()+1)+"/"+d.getDate(), gx, h - 6);
        var totalBarsW = barW * types.length + 2 * (types.length - 1);
        var startX = gx - totalBarsW / 2;
        group.forEach(function(v, bi) {
            var barH = (v / max) * chartH;
            var x = startX + bi * (barW + 2);
            var y = pad.top + chartH - barH;
            ctx.fillStyle = colors[bi % colors.length];
            ctx.beginPath();
            ctx.roundRect(x, y, barW, barH, [2, 2, 0, 0]);
            ctx.fill();
        });
    });
}
