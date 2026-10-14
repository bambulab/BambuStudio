
function OnInit()
{
	TranslatePage();
	
	SendInstallPluginCheck();
}



function SendInstallPluginCheck()
{
	let nVal="no";
	if( $('#InstallCheck').is(':checked') ) 
		nVal="yes";
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="network_plugin_install";
	tSend['data']={};
	tSend['data']['action']=nVal;
	
	SendWXMessage( JSON.stringify(tSend) );		
}



function FinishGuide()
{
	// Carry the language forward explicitly instead of relying solely on
	// localStorage, which the next page would otherwise have to fall back on.
	var lang = GetQueryString('lang') || localStorage.getItem(LANG_COOKIE_NAME);
	var langParam = lang ? ('?lang=' + lang) : '';
	window.open('../7/index.html' + langParam, '_self');
}
