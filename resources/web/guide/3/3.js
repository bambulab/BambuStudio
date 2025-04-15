
m_Region='US';

function OnInit()
{
	TranslatePage();
	
	//SendPrivacySelect();
	
	let strRegion=GetQueryString('region');
	if( strRegion!=null )
		m_Region=strRegion.toLowerCase();
}

//function SendPrivacySelect()
//{	
//	let nVal="refuse";
//	if( $('#ChoosePrivacy').is(':checked') ) 
//		nVal="agree";
//	
//	var tSend={};
//	tSend['sequence_id']=Math.round(new Date() / 1000);
//	tSend['command']="user_private_choice";
//	tSend['data']={};
//	tSend['data']['action']=nVal;
//	
//	SendWXMessage( JSON.stringify(tSend) );	
//}

function SendPrivacyValue( strVal )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="user_private_choice";
	tSend['data']={};
	tSend['data']['action']=strVal;
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function GotoSkipPage()
{
	SendPrivacyValue('refuse');
	
	RequestProfile();
}

function GotoNextPage()
{
	SendPrivacyValue('agree');
	
	RequestProfile();	
}

function RequestProfile()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_userguide_profile";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function HandleStudio( pVal )
{	
	let strCmd=pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='response_userguide_profile')
	{
		HandleModelInfo(pVal['response']);
	}
}

function HandleModelInfo( pVal )
{
	let Modellist=pVal["model"];
	let nModel=Modellist.length;
	
	if(nModel==1)
	{
//		let pModel=Modellist[0];
//		
//		var tSend={};
//		tSend['sequence_id']=Math.round(new Date() / 1000);
//		tSend['command']="save_userguide_models";
//		tSend['data']={};
//		
//		let ModelName=pModel['model'];
//		
//		tSend['data'][ModelName]={};
//		tSend['data'][ModelName]['model']=pModel['model'];
//		tSend['data'][ModelName]['nozzle_diameter']=pModel['nozzle_diameter'];
//		tSend['data'][ModelName]['vendor']=pModel['vendor'];
//		
//		SendWXMessage( JSON.stringify(tSend) );
			
		window.location.href="../22/index.html";
			
		return;
	}

	window.location.href="../21/index.html";	
}

function OpenPrivacyPolicy()
{
	let PolicyUrl='';
	if( m_Region=='china' )
		PolicyUrl="https://bambulab.cn/policies/software-privacy";
	else
	    PolicyUrl="https://bambulab.com/policies/privacy";
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="common_openurl";
	tSend['url']=PolicyUrl;
	
	SendWXMessage( JSON.stringify(tSend) );		
}
