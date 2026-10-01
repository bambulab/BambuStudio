#include "DeviceWebHealth.hpp"

namespace Slic3r { namespace GUI { namespace DeviceWebHealth {

namespace {

constexpr std::size_t MAX_PAGE_INSTANCE_ID_LENGTH = 128;
constexpr std::size_t MAX_TEXT_LENGTH             = 2048;
constexpr std::size_t MAX_TAG_LENGTH              = 32;

std::optional<std::string> string_member(const nlohmann::json &value, const char *key)
{
    const auto it = value.find(key);
    if (it == value.end() || !it->is_string())
        return std::nullopt;
    return it->get<std::string>();
}

int int_member(const nlohmann::json &value, const char *key)
{
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer())
        return 0;
    try {
        return it->get<int>();
    } catch (const nlohmann::json::exception &) {
        return 0;
    }
}

std::string bounded_string_member(const nlohmann::json &value, const char *key, std::size_t max_length)
{
    const auto result = string_member(value, key);
    if (!result)
        return {};
    return result->substr(0, max_length);
}

std::string without_fragment(const std::string &url)
{
    return url.substr(0, url.find('#'));
}

} // namespace

std::optional<Message> Parse(const nlohmann::json &body)
{
    if (!body.is_object())
        return std::nullopt;

    const auto module = string_member(body, "module");
    const auto submod = string_member(body, "submod");
    if (!module || *module != "device_host" || !submod || *submod != "health" ||
        !body.contains("payload") || !body["payload"].is_object())
        return std::nullopt;

    const auto &payload = body["payload"];
    const auto page_instance_id = string_member(payload, "page_instance_id");
    if (!page_instance_id || page_instance_id->empty() ||
        page_instance_id->size() > MAX_PAGE_INSTANCE_ID_LENGTH)
        return std::nullopt;
    const auto document_url = string_member(payload, "document_url");
    if (document_url && document_url->size() > MAX_TEXT_LENGTH)
        return std::nullopt;

    const auto action_name = string_member(body, "action");
    if (!action_name)
        return std::nullopt;

    Action action;
    if (*action_name == "boot")
        action = Action::Boot;
    else if (*action_name == "ready")
        action = Action::Ready;
    else if (*action_name == "js_error")
        action = Action::JsError;
    else if (*action_name == "unhandled_rejection")
        action = Action::UnhandledRejection;
    else if (*action_name == "resource_error")
        action = Action::ResourceError;
    else
        return std::nullopt;

    Message message;
    message.action           = action;
    message.page_instance_id = *page_instance_id;
    message.document_url     = document_url ? *document_url : std::string();

    switch (action) {
    case Action::JsError:
        message.message = bounded_string_member(payload, "message", MAX_TEXT_LENGTH);
        message.source  = bounded_string_member(payload, "source", MAX_TEXT_LENGTH);
        message.line    = int_member(payload, "line");
        message.column  = int_member(payload, "column");
        break;
    case Action::UnhandledRejection:
        message.reason = bounded_string_member(payload, "reason", MAX_TEXT_LENGTH);
        break;
    case Action::ResourceError:
        message.tag = bounded_string_member(payload, "tag", MAX_TAG_LENGTH);
        message.url = bounded_string_member(payload, "url", MAX_TEXT_LENGTH);
        break;
    default: break;
    }

    return message;
}

bool MatchesCurrentDocument(const std::string &message_url, const std::string &current_url)
{
    return !message_url.empty() && !current_url.empty() &&
           without_fragment(message_url) == without_fragment(current_url);
}

std::string DiagnosticShim()
{
    return R"JS((function(){
try{
if(window.__bambuWebViewHealth)return;
var pageInstanceId=(window.crypto&&typeof window.crypto.randomUUID==='function')
?window.crypto.randomUUID()
:Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);
var sequence=0,maxTextLength=2048,maxConsoleEntries=50,maxConsoleArgs=10;
var maxPendingPackets=50,maxFlushAttempts=10,flushDelayMs=50;
var consoleBuffer=[],pendingPackets=[],flushAttempts=0,flushTimer=null;
function text(value){
try{
var kind=typeof value;
var result;
if(kind==='string')result=value;
else if(value===null)result='null';
else if(kind==='undefined')result='undefined';
else if(kind==='number'||kind==='boolean'||kind==='bigint')result=''+value;
else if(kind==='function')result='<function>';
else result='<object>';
return result.length>maxTextLength?result.slice(0,maxTextLength):result;
}catch(_){return '<unprintable>';}
}
function post(packet){
try{
var serialized=JSON.stringify(packet);
var edgePost=window.chrome&&window.chrome.webview&&window.chrome.webview.postMessage;
if(typeof edgePost==='function'){
edgePost.call(window.chrome.webview,serialized);
return true;
}
var wxPost=window.webkit&&window.webkit.messageHandlers&&window.webkit.messageHandlers.wx&&
window.webkit.messageHandlers.wx.postMessage;
if(typeof wxPost==='function'){
wxPost.call(window.webkit.messageHandlers.wx,serialized);
return true;
}
}catch(_){}
return false;
}
function flushPending(){
while(pendingPackets.length&&post(pendingPackets[0]))pendingPackets.shift();
if(!pendingPackets.length)flushAttempts=0;
}
function scheduleFlush(){
if(flushTimer!==null||flushAttempts>=maxFlushAttempts||!pendingPackets.length)return;
flushTimer=setTimeout(function(){
flushTimer=null;
flushAttempts+=1;
flushPending();
scheduleFlush();
},flushDelayMs);
}
function deliver(packet){
if(pendingPackets.length){
if(pendingPackets.length>=maxPendingPackets)pendingPackets.shift();
pendingPackets.push(packet);
flushPending();
scheduleFlush();
return;
}
if(post(packet))return;
pendingPackets.push(packet);
scheduleFlush();
}
function send(action,payload){
try{
deliver({
head:{version:'1.0',type:'request',seq:sequence++,ts:Date.now()},
body:{module:'device_host',submod:'health',action:action,
payload:Object.assign({},payload||{},{
page_instance_id:pageInstanceId,
document_url:text(window.location.href)
})}
});
}catch(_){}
}
function wrapConsole(level){
try{
var original=console[level];
if(typeof original!=='function')return;
console[level]=function(){
var args=Array.prototype.slice.call(arguments);
try{
consoleBuffer.push({level:level,ts:Date.now(),args:args.slice(0,maxConsoleArgs).map(text)});
if(consoleBuffer.length>maxConsoleEntries)consoleBuffer.shift();
}catch(_){}
return original.apply(console,args);
};
}catch(_){}
}
wrapConsole('warn');
wrapConsole('error');
window.addEventListener('error',function(event){
try{
var target=event.target;
var tagName=target&&target.tagName?text(target.tagName).toUpperCase():'';
if(tagName==='SCRIPT'||tagName==='LINK'){
send('resource_error',{tag:tagName.toLowerCase(),url:text(target.src||target.href||'')});
return;
}
send('js_error',{
message:text(event.message||''),
source:text(event.filename||''),
line:Number(event.lineno)||0,
column:Number(event.colno)||0
});
}catch(_){}
},true);
window.addEventListener('unhandledrejection',function(event){
try{
send('unhandled_rejection',{
reason:text(event.reason)
});
}catch(_){}
});
Object.defineProperty(window,'__bambuWebViewHealth',{
value:Object.freeze({
pageInstanceId:pageInstanceId,
getConsoleEntries:function(){
return consoleBuffer.map(function(entry){
return {level:entry.level,ts:entry.ts,args:entry.args.slice()};
});
}
}),
writable:false,
configurable:false
});
send('boot',{});
window.addEventListener('DOMContentLoaded',function(){
flushAttempts=0;
flushPending();
send('boot',{});
},{once:true});
}catch(_){}
})();)JS";
}

}}} // namespace Slic3r::GUI::DeviceWebHealth
