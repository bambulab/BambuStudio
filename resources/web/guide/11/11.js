var m_OldRegion='';
var m_Region='';

function OnInit()
{
	//let strInput=JSON.stringify(cData);
	//HandleStudio(strInput);
	
	TranslatePage();
	
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
		HandleRegionlList(pVal['response']);
	}
}

function HandleRegionlList(  pItem )
{
	m_OldRegion=pItem['region'];

	let strRegion=pItem['region_selected'];
	if( !strRegion )
		strRegion=m_OldRegion;

	let nNum=$(".RegionItem[region='"+strRegion+"']").length;
	if( nNum==1 )
		ChooseRegion(strRegion);
	else
		ChooseRegion('Asia-Pacific');
}


function ChooseRegion( strRegion )
{
	m_Region=strRegion;
	
	$('.RegionItem').removeClass('RegionSelected');
	$(".RegionItem[region='"+strRegion+"']").addClass('RegionSelected');
}

function GotoPolicyPage()
{
	let ItemSelected=$('.RegionSelected')[0];
	let RegionFinal=$(ItemSelected).attr("region");
	if( !RegionFinal )
		RegionFinal='Asia-Pacific';
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_region";
	tSend['region']=RegionFinal;
	
	SendWXMessage( JSON.stringify(tSend) );
	
	window.location.href="../12/index.html?region="+encodeURIComponent(RegionFinal);
}


