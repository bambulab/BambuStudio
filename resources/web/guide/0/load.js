
var TargetPage=null;
var TargetCustom=null;

function OnInit()
{
	TranslatePage();
	TargetPage=GetQueryString("target");
	TargetCustom=GetQueryString("custom");

	//setTimeout("JumpToTarget()",20*1000);
}

function HandleStudio( pVal )
{
	let strCmd=pVal['command'];

	if(strCmd=='userguide_profile_load_finish')
	{
		JumpToTarget();
	}
}

function JumpToTarget()
{
	var url = '../'+TargetPage+'/index.html';
	if (TargetCustom) url += '?custom='+TargetCustom;
	window.open(url,'_self');
}