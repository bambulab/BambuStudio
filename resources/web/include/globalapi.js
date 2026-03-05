
/*------------------ Date Function ------------------------*/
function GetFullToday( )
{
	var d=new Date();
	
	var nday=d.getDate();
	var nmonth=d.getMonth()+1;
	var nyear=d.getFullYear();
	
	var strM=nmonth+'';
	if( nmonth<10 )
		strM='0'+nmonth;

    var strD=nday+'';
    if( nday<10 )
	    strD='0'+nday;
		
	return nyear+'-'+strM+'-'+strD;
}

function GetFullDate()
{
	var d=new Date();
	
	var tDate={};
	
	tDate.nyear=d.getFullYear();
	tDate.nmonth=d.getMonth()+1;
	tDate.nday=d.getDate();
	
	tDate.nhour=d.getHours();
	tDate.nminute=d.getMinutes();
	tDate.nsecond=d.getSeconds();	
	
	tDate.nweek=d.getDay();
	tDate.ndate=d.getDate();
	
	var strM=tDate.nmonth+'';
	if( tDate.nmonth<10 )
		strM='0'+tDate.nmonth;

    var strD=tDate.nday+'';
    if( tDate.nday<10 )
	    strD='0'+tDate.nday;
	
	var strH=tDate.nhour+'';
	if( tDate.nhour<10 )
		strH='0'+tDate.nhour;

	var strMin=tDate.nminute+'';
	if( tDate.nminute<10 )
		strMin='0'+tDate.nminute;

	var strS=tDate.nsecond+'';
	if( tDate.nsecond<10 )
		strS='0'+tDate.nsecond;					
	
	tDate.strdate=tDate.nyear+'-'+strM+'-'+strD;
	tDate.strFulldate=tDate.strdate+' '+strH+':'+strMin+':'+strS;
	
	return tDate;
}

//return YYYY-MM-DD
function Unixtimestamp2Date( nSecond )
{
	var d=new Date(nSecond*1000);
	
	var tDate={};
	
	tDate.nyear=d.getFullYear();
	tDate.nmonth=d.getMonth()+1;
	tDate.nday=d.getDate();
	
	tDate.nhour=d.getHours();
	tDate.nminute=d.getMinutes();
	tDate.nsecond=d.getSeconds();	
	
	tDate.nweek=d.getDay();
	tDate.ndate=d.getDate();
	
	var strM=tDate.nmonth+'';
	if( tDate.nmonth<10 )
		strM='0'+tDate.nmonth;

    var strD=tDate.nday+'';
    if( tDate.nday<10 )
	    strD='0'+tDate.nday;
				
	tDate.strdate=tDate.nyear+'-'+strM+'-'+strD;
	
	return tDate.strdate;
}

function DateToUnixstamp( strDate )
{
	const date = new Date(strDate);
	return Math.floor(date.getTime() / 1000);	
}

function DateToUnixstampMS( strDate )
{
	const date = new Date(strDate);
	return date.getTime();
}


//------------Array Function-------------
Array.prototype.in_array = function (e) {
    let sArray= ',' + this.join(this.S) + ',';
	let skey=','+e+',';
	
	if(sArray.indexOf(skey)>=0)
		return true;
	else
		return false;
 }



//------------String Function------------------
/**
* Delete Left/Right Side Blank
*/
String.prototype.trim=function()
{
     return this.replace(/(^\s*)|(\s*$)/g, '');
}
/**
* Delete Left Side Blank
*/
String.prototype.ltrim=function()
{
     return this.replace(/(^\s*)/g,'');
}
/**
* Delete Right Side Blank
*/
String.prototype.rtrim=function()
{
     return this.replace(/(\s*$)/g,'');
}


//----------------Get Param-------------
function GetQueryString(name) 
{
	var reg = new RegExp("(^|&)"+ name +"=([^&]*)(&|$)"); 
    var r = window.location.search.substr(1).match(reg); 
    if (r!=null)
	{
		return unescape(r[2]);
	}
    else
	{
		return null; 
    }
} 

function GetGetStr()
{
	let strGet="";
	
	//获取当前URL
    let url = document.location.href;

    //获取?的位置
    let index = url.indexOf("?")
    if(index != -1) {
        //截取出?后面的字符串
        strGet = url.substr(index + 1);	
	}
	
	return strGet;
}

/*--------------File Function--------------*/
function getFileName(path) 
{ 
    var pos1 = path.lastIndexOf('/'); 
    var pos2 = path.lastIndexOf('\\'); 
    var pos = Math.max(pos1, pos2); 
    if (pos < 0) { 
      return null; 
    } 
    else 
	{ 
      return path.substring(pos + 1); 
    } 
}

function getFileTail(path) 
{ 
    var pos = path.lastIndexOf('.');
    if (pos < 0) { 
		return null; 
    } 
    else 
	{ 
        return path.substring(pos + 1); 
    } 
}

/*--------------String Function-----------*/
function html_encode(str) 
{
	var s = ""; 
    if (str.length == 0) return ""; 
    s = str.replace(/&/g, "&amp;"); 
    s = s.replace(/</g, "&lt;"); 
    s = s.replace(/>/g, "&gt;"); 
    s = s.replace(/ /g, "&nbsp;"); 
    s = s.replace(/\'/g, "&#39;"); 
    s = s.replace(/\"/g, "&quot;"); 
    s = s.replace(/\n/g, "<br/>"); 
    
	return s; 
} 

function html_decode(str) 
{ 
	var s = ""; 
	if (str.length == 0) return ""; 
	s = str.replace(/&amp;/g, "&"); 
	s = s.replace(/&lt;/g, "<"); 
	s = s.replace(/&gt;/g, ">"); 
	s = s.replace(/&nbsp;/g, " "); 
	s = s.replace(/&#39;/g, "\'"); 
	s = s.replace(/&quot;/g, "\""); 
	s = s.replace(/<br\/>/g, "\n"); 

	return s; 
} 

/*--------------------JSON  Function------------*/

/*
功能：检查一个字符串是不是标准的JSON格式
参数： strJson          被检查的字符串
返回值： 如果字符串是一个标准的JSON格式，则返回JSON对象
        如果字符串不是标准JSON格式，则返回null
*/
function IsJson( strJson )
{
	var tJson=null;
	try
	{
		tJson=JSON.parse(strJson);
	}
	catch(exception)
	{
	    return null;
	}	
	
	return tJson;
}

function DecodeJsonObject( pJson )
{
	let tmpJson=JSON.stringify(pJson);
	tmpJson=decodeURIComponent(tmpJson);
	
	pJson=JSON.parse(tmpJson);
	
	return pJson;
}

/*-----------------------Ajax Function--------------------*/
/*对JQuery的Ajax函数的封装，只支持异步
参数说明：
    url      目标地址
	action   post/get
	data     字符串格式的发送内容
	asyn     true---异步模式;false-----同步模式;
*/
function HttpReq( url,action, data,callbackfunc)
{
	var strAction=action.toLowerCase();
	
	if( strAction=="post")
	{
		$.post(url,data,callbackfunc);			
	}
	else if( strAction=="get")
    {
		$.get(url,callbackfunc);
	}
}

/*---------------Cookie Function-------------------*/ 
function setCookie(name, value, time='',path='') {
    if(time && path){
        var strsec = time * 1000;
        var exp = new Date();
        exp.setTime(exp.getTime() + strsec * 1);
        document.cookie = name + "=" + escape(value) + ";expires=" + exp.toGMTString() + ";path="+path;
    }else if(time){
        var strsec = time * 1000;
        var exp = new Date();
        exp.setTime(exp.getTime() + strsec * 1);
        document.cookie = name + "=" + escape(value) + ";expires=" + exp.toGMTString();
    }else if(path){
        document.cookie = name + "=" + escape(value) + ";path="+path;
    }else{
        document.cookie = name + "=" + escape(value);
    }
}

function getCookie(c_name) 
{
	if(document.cookie.length > 0) {
		c_start = document.cookie.indexOf(c_name + "=");//获取字符串的起点
	    if(c_start != -1) {
			c_start = c_start + c_name.length + 1;//获取值的起点
			c_end = document.cookie.indexOf(";", c_start);//获取结尾处
			if(c_end == -1) c_end = document.cookie.length;//如果是最后一个，结尾就是cookie字符串的结尾
			return decodeURI(document.cookie.substring(c_start, c_end));//截取字符串返回
	    }
	}
	
	return "";
}

function checkCookie(c_name) {     
    username = getCookie(c_name);     
    console.log(username);     
    if (username != null && username != "")     
    { return true; }     
    else     
    { return false;  }
}

function clearCookie(name) {     
    setCookie(name, "", -1); 
}


/*--------Studio WX Message-------*/
function IsInSlicer()
{
	let bMatch=navigator.userAgent.match(  RegExp('BBL-Slicer','i') );
	
	return bMatch;
}



function SendWXMessage( strMsg )
{
	let bCheck=IsInSlicer();
	
	if(bCheck!=null)
	{
		setTimeout("window.wx.postMessage("+strMsg+")",1);
	}
}

function SendWXDebugInfo( strMsg )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="debug_info";
	tSend['msg']=strMsg;

	SendWXMessage( JSON.stringify(tSend) );		
}

function OpenUrlInLocalBrowser( strUrl )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="common_openurl";
	tSend['url']=strUrl;

	SendWXMessage( JSON.stringify(tSend) );		
}


/*------CSS Link Control----*/
function RemoveCssLink( LinkPath )
{
	let pNow=$("head link[href='"+LinkPath+"']");
	
	let nTotal=pNow.length;
    for( let n=0;n<nTotal;n++ )
	{
		pNow[n].remove();
	}	
}

function AddCssLink( LinkPath )
{	
	var head = document.getElementsByTagName('head')[0];
	var link = document.createElement('link');
	link.href = LinkPath;
	link.rel = 'stylesheet';
	link.type = 'text/css';
	head.appendChild(link);
}

function CheckCssLinkExist( LinkPath )
{
	let pNow=$("head link[href='"+LinkPath+"']");
	let nTotal=pNow.length;
	
	return nTotal;
}


/*------Dark Mode------*/

function SwitchDarkMode( DarkCssPath )
{		
	ExecuteDarkMode( DarkCssPath );
    setInterval("ExecuteDarkMode('"+DarkCssPath+"')",1000);	
}

function ExecuteDarkMode( DarkCssPath )
{
    let nMode=0;
	let bDarkMode=navigator.userAgent.match(  RegExp('dark','i') );	
	if( bDarkMode!=null )
		nMode=1;
	
	let nNow=CheckCssLinkExist(DarkCssPath);
	if( nMode==0 )
	{
		if(nNow>0)
			RemoveCssLink(DarkCssPath);
	}
	else
	{
		if(nNow==0)
			AddCssLink(DarkCssPath);
	}	
}

SwitchDarkMode( "./css/dark.css" );

/*-------KeyBoard------*/
function DisableCtrlHotkey()
{
	document.onkeydown = function(event) {
    event = event || window.event;
    if (event.ctrlKey ) {
        event.preventDefault();
    }	
    }
}

function OutputKey(keyCode, isCtrlDown, isShiftDown, isCmdDown) {
	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "get_web_shortcut";
	tSend['key_event'] = {};
	tSend['key_event']['key'] = keyCode;
	tSend['key_event']['ctrl'] = isCtrlDown;
	tSend['key_event']['shift'] = isShiftDown;
	tSend['key_event']['cmd'] = isCmdDown;

	SendWXMessage(JSON.stringify(tSend));
}

function DisableHotkey( b_CtrlP )
{
    document.onkeydown = function (event) {
		var e = event || window.event || arguments.callee.caller.arguments[0];

		if (e.ctrlKey && e.metaKey)
			OutputKey(e.keyCode, true, false, true);
		else if (e.ctrlKey)
            OutputKey(e.keyCode, true, false, false);
		else if (e.metaKey)
			OutputKey(e.keyCode, false, false, true);

		if (e.shiftKey && e.ctrlKey)
			OutputKey(e.keyCode, true, true, false);
		
		if (e.shiftKey && e.metaKey)
			OutputKey(e.keyCode, false, true, true);

		//F1--F12
		if ( e.keyCode>=112 && e.keyCode<=123 )	
		{
			e.preventDefault();
		}
		
//		if (window.event) {
//			try { e.keyCode = 0; } catch (e) { }
//			e.returnValue = false;
//		}
	};

	window.addEventListener('mousewheel', function (event) {
		if (event.ctrlKey === true || event.metaKey) {
			event.preventDefault();
		}
	}, { passive: false });	
	
	DisableDropAction();
}
	
DisableHotkey();

/*--------Disable Drop Action---------*/
function DisableDropAction()
{
	document.addEventListener("dragstart", (event) => {
    event.preventDefault();
	});

	document.addEventListener("dragover", (event) => {
		event.preventDefault();
	});
	
	document.addEventListener("drop", (event) => {
		event.preventDefault();
	});
}

function showToast(message, duration = 2500) {
  const old = document.querySelector('.toast');
  if (old) old.remove();

  const toast = document.createElement('div');
  toast.className = 'toast';
  toast.innerText = message;
  document.body.appendChild(toast);

  void toast.offsetWidth;
  toast.classList.add('show');

  setTimeout(() => {
    toast.classList.remove('show');
    setTimeout(() => toast.remove(), 300);
  }, duration);
}

function showConfirmDialog(options) {
  var opts = options || {};
  var title = opts.title || "Confirm";
  var message = opts.message || "";
  var okText = opts.okText || "OK";
  var cancelText = opts.cancelText || "Cancel";
  var allowHtml = opts.allowHtml === true;
  var showCancel = opts.showCancel !== false;
  var closeOnOverlay = opts.closeOnOverlay === true;
  var closeOnEsc = opts.closeOnEsc !== false;
  var onOk = typeof opts.onOk === "function" ? opts.onOk : null;
  var onCancel = typeof opts.onCancel === "function" ? opts.onCancel : null;
  var onClose = typeof opts.onClose === "function" ? opts.onClose : null;

  var old = document.getElementById('confirm-dialog-overlay');
  if (old) old.remove();

  var overlay = document.createElement('div');
  overlay.id = 'confirm-dialog-overlay';
  Object.assign(overlay.style, {
    position: 'fixed',
    inset: '0',
    background: 'rgba(0, 0, 0, 0.3)',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    zIndex: 9999,
    opacity: '0',
    transition: 'opacity 120ms ease-out'
  });

  var dialog = document.createElement('div');
  Object.assign(dialog.style, {
    width: '86%',
    maxWidth: '520px',
    background: '#ffffff',
    borderRadius: '8px',
    overflow: 'hidden',
    boxShadow: '0 10px 28px rgba(0, 0, 0, 0.25)',
    transform: 'scale(0.96)',
    transition: 'transform 120ms ease-out'
  });

  var header = document.createElement('div');
  header.innerText = title;
  Object.assign(header.style, {
    background: '#00a85a',
    color: '#ffffff',
    padding: '12px 16px',
    fontSize: '16px',
    textAlign: 'center'
  });

  var body = document.createElement('div');
  if (allowHtml)
    body.innerHTML = message;
  else
    body.innerText = message;
  Object.assign(body.style, {
    padding: '18px 24px 12px',
    color: '#222222',
    fontSize: '14px',
    lineHeight: '1.6',
    whiteSpace: 'pre-wrap'
  });

  var footer = document.createElement('div');
  Object.assign(footer.style, {
    display: 'flex',
    justifyContent: 'center',
    gap: '24px',
    padding: '0 24px 20px'
  });

  function buildButton(text) {
    var btn = document.createElement('button');
    btn.type = 'button';
    btn.innerText = text;
    Object.assign(btn.style, {
      minWidth: '84px',
      padding: '6px 18px',
      background: '#e0e0e0',
      border: '1px solid #c9c9c9',
      borderRadius: '6px',
      fontSize: '14px',
      cursor: 'pointer'
    });
    return btn;
  }

  var okBtn = buildButton(okText);
  var cancelBtn = buildButton(cancelText);

  footer.appendChild(okBtn);
  if (showCancel)
    footer.appendChild(cancelBtn);

  dialog.appendChild(header);
  dialog.appendChild(body);
  dialog.appendChild(footer);
  overlay.appendChild(dialog);
  document.body.appendChild(overlay);

  function cleanup() {
    window.removeEventListener('keydown', onKeydown);
  }

  function close(result) {
    overlay.style.opacity = '0';
    dialog.style.transform = 'scale(0.96)';
    setTimeout(function() {
      cleanup();
      overlay.remove();
      if (onClose) onClose(result);
    }, 160);
  }

  function onKeydown(evt) {
    if (evt.key === 'Escape' && closeOnEsc) {
      if (onCancel) onCancel();
      close(false);
    }
  }

  okBtn.addEventListener('click', function() {
    if (onOk) onOk();
    close(true);
  });

  cancelBtn.addEventListener('click', function() {
    if (onCancel) onCancel();
    close(false);
  });

  overlay.addEventListener('click', function(event) {
    if (closeOnOverlay && event.target === overlay) {
      if (onCancel) onCancel();
      close(false);
    }
  });

  window.addEventListener('keydown', onKeydown);

  requestAnimationFrame(function() {
    overlay.style.opacity = '1';
    dialog.style.transform = 'scale(1)';
  });

  return { close: function() { close(false); } };
}

function isBase64String(value) {
  if (typeof value !== 'string' || !value.trim()) return false;
  const trimmed = value.trim();
  if (trimmed.startsWith('data:') && trimmed.includes(';base64,')) return true;
  const base64Body = trimmed.replace(/\s+/g, '');
  return /^[A-Za-z0-9+/]+={0,2}$/.test(base64Body) && base64Body.length % 4 === 0;
}

function showBase64ImageLayer(base64Str) {
  if (!base64Str) return;

  const existingLayer = document.getElementById('base64-image-layer');
  if (existingLayer) existingLayer.remove();

  const overlay = document.createElement('div');
  overlay.id = 'base64-image-layer';
  Object.assign(overlay.style, {
    position: 'fixed',
    inset: '0',
    background: 'rgba(0, 0, 0, 0.65)',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    zIndex: 9999,
    padding: '24px'
  });

  const imgWrapper = document.createElement('div');
  Object.assign(imgWrapper.style, {
    maxWidth: '90%',
    maxHeight: '100%',
    boxShadow: '0 8px 24px rgba(0, 0, 0, 0.45)'
  });

  const img = document.createElement('img');
  img.src = base64Str;
  Object.assign(img.style, {
    display: 'block',
    maxWidth: '100%',
    maxHeight: '100%',
    borderRadius: '8px'
  });

  imgWrapper.appendChild(img);
  overlay.appendChild(imgWrapper);
  document.body.appendChild(overlay);

  const closeLayer = () => overlay.remove();
  overlay.addEventListener('click', event => {
    if (event.target === overlay) closeLayer();
  });
  window.addEventListener('keydown', function handler(evt) {
    if (evt.key === 'Escape') {
      window.removeEventListener('keydown', handler);
      closeLayer();
    }
  });
}
