

function OnInit()
{
	//let strInput=JSON.stringify(cData);
	//HandleStudio(strInput);
	
	TranslatePage();
	
	SendDownloadCmd();
}

function SendDownloadCmd()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="Begin_Download_network_plugin";
	
	SendWXMessage( JSON.stringify(tSend) );		
}


function HandleStudio( pVal )
{
	let strCmd=pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='ShowStatusPercent')
	{
		HandStatusPercent(pVal['data']);
	}
}


function HandStatusPercent( pVal )
{
	let nStatus=pVal['status'];
	let nPercent=pVal['percent'];
	
	if(nStatus==0)
	{
		//正常下载
		$('#DownStepText').attr("tid","t71");
		$('#RetryBtn').hide();
		
		$('#PercentTip').css("width",nPercent+'%');
	}
	else if(nStatus==1 || nStatus==3)
	{
		//下载失败 或 解压缩补丁包失败
		$('#DownStepText').attr("tid","t72");
		$('#PercentTip').css("width",0+'%');
		$('#RetryBtn').show();
	}
	else if(nStatus==2)
	{
		//下载完成
		$('#PercentTip').css("width",100+'%');
		
		SendInstallPluginCmd();
	}
	else if(nStatus==4)
	{
		//安装补丁包完成
		$('#DownArea').hide();
		$('#DownSuccessTip').show();
		
		$('#CancelBtn').hide();
		$('#RestartBtn').show();
		
		pTimer=setInterval("RunInverse()",1000);
	}
	
	TranslatePage();
}

var nCount=3;
var pTimer=null;
function RunInverse()
{
	$('#CountNumber').text(nCount+'');
	nCount--;
	
	if(nCount==-1)
	{
		RestartBambuStudio();
	}
	
}

function RetryDownload()
{
	$('#DownStepText').attr("tid","t71");	
	$('#PercentTip').css("width",0+'%');
	SendDownloadCmd();
}


function CancelDownload()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="netplugin_download_cancel";
	
	SendWXMessage( JSON.stringify(tSend) );		
	
}

function SendInstallPluginCmd()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="begin_install_plugin";
	
	SendWXMessage( JSON.stringify(tSend) );			
}



function RestartBambuStudio()
{
	if( pTimer!=null )
	{
		clearInterval(pTimer);
		pTimer=null;
	}
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="restart_studio";
	
	SendWXMessage( JSON.stringify(tSend) );	
}



