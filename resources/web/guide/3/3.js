
function OnInit()
{
	TranslatePage();
	
	SendPrivacySelect();
}


function SendPrivacySelect()
{	
	let nVal="refuse";
	if( $('#ChoosePrivacy').is(':checked') ) 
		nVal="agree";
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="user_private_choice";
	tSend['data']={};
	tSend['data']['action']=nVal;
	
	SendWXMessage( JSON.stringify(tSend) );	
}