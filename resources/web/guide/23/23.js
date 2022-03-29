
var m_ProfileItem;

function OnInit()
{
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

//function RequestModelSelect()
//{
//	var tSend={};
//	tSend['sequence_id']=Math.round(new Date() / 1000);
//	tSend['command']="request_userguide_modelselected";
//	
//	SendWXMessage( JSON.stringify(tSend) );
//}

function HandleStudio(pVal)
{
	let strCmd=pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='response_userguide_profile')
	{
		m_ProfileItem=pVal['response'];
		SortUI();
	}
}

function GetFilamentShortname( sName )
{
	let sShort=sName.split('@')[0].trim();
	
	return sShort;
}


function SortUI()
{
	var ModelList=new Array();
	
	let nMode=m_ProfileItem["model"].length;
	for(let n=0;n<nMode;n++)
	{
		let OneMode=m_ProfileItem["model"][n];
		
		if( OneMode["nozzle_selected"]!="" )
			ModelList.push(OneMode["model"]);
	}
	
	//machine
	let HtmlMachine='';
	
	let nMachine=m_ProfileItem['machine'].length;
	for(let n=0;n<nMachine;n++)
	{
		let OneMachine=m_ProfileItem['machine'][n];
		
		let sName=OneMachine['name'];
		let sModel=OneMachine['model'];
	
		if( ModelList.in_array(sModel) )
			HtmlMachine+='<div><input type="checkbox" mode="'+sModel+'" onChange="MachineClick()" />'+sName+'</div>';
	}
	
	$('#MachineList .CValues').append(HtmlMachine);	
	$('#MachineList .CValues input').prop("checked",true);
	if(nMachine<=1)
	{
		$('#MachineList').hide();
	}
	
	
	//Filament
	let HtmlFilament='';
	for( let key in m_ProfileItem['filament'] )
	{
		let OneFila=m_ProfileItem['filament'][key];
		
		let fShortName=GetFilamentShortname( OneFila['name'] );
		let fVendor=OneFila['vendor'];
		let fType=OneFila['type'];
		let fSelect=OneFila['selected'];
		let fModel=OneFila['models']
		
//		if(OneFila['name'].indexOf("K5 PLA Wood")>0)
//		{
//			let b=1+2;
//		}
		
        let bFind=false;		
		let bCheck=$("#MachineList input:first").prop("checked");
		if(bCheck)
		{
			bFind=true;
		}
		else
		{
			//check in modellist		    
		    let nModelAll=ModelList.length;
		    for(let m=0;m<nModelAll;m++)
		    {
	    		let sOne=ModelList[m];
			
		    	if(fModel.indexOf(sOne)>=0)
		    	{
		    		bFind=true;
				    break;
			    }			
			}
		}
		
		if(bFind)
		{
			//Type
			let pType=$("#FilatypeList .CValues input[filatype='"+fType+"']");
		    if(pType.length==0)
		    {
			    let HtmlType='<div><input type="checkbox" filatype="'+fType+'" onChange="FilaClick()"   />'+fType+'</div>';
			
			    $("#FilatypeList .CValues").append(HtmlType);
				$("#FilatypeList .CValues input[filatype='"+fType+"']").prop("checked",true);
		    }
			
			//Vendor
			let pVendor=$("#VendorList .CValues input[vendor='"+fVendor+"']");
	        if(pVendor.length==0)
		    {
			    let HtmlVendor='<div><input type="checkbox" vendor="'+fVendor+'"  onChange="VendorClick()" />'+fVendor+'</div>';
			
			    $("#VendorList .CValues").append(HtmlVendor);
				$("#VendorList .CValues input[vendor='"+fVendor+"']").prop("checked",true);
		    }
			
			//Filament
			let pFila=$("#ItemBlockArea input[vendor='"+fVendor+"'][model='"+fModel+"'][filatype='"+fType+"'][name='"+fShortName+"']");
	        if(pFila.length==0)
		    {
			    let HtmlFila='<div class="MItem"><input type="checkbox" vendor="'+fVendor+'"  filatype="'+fType+'" model="'+fModel+'" name="'+key+'" />'+fShortName+'</div>';
			
			    $("#ItemBlockArea").append(HtmlFila);
				
				if(fSelect==1)
					$("#ItemBlockArea input[vendor='"+fVendor+"'][model='"+fModel+"'][filatype='"+fType+"'][name='"+key+"']").prop("checked",true);
				else
					$("#ItemBlockArea input[vendor='"+fVendor+"'][model='"+fModel+"'][filatype='"+fType+"'][name='"+key+"']").prop("checked",false);
		    } 
		}
		
		$("#FilatypeList .CValues input").prop("checked",true);
		$("#VendorList .CValues input").prop("checked",true);
	}

}


function ChooseAllMachine()
{
	let bCheck=$("#MachineList input:first").prop("checked");
	
	$("#MachineList input").prop("checked",bCheck);
	
	SortFilament();
}

function MachineClick()
{
	let nChecked=$("#MachineList input:gt(0):checked").length
	let nAll    =$("#MachineList input:gt(0)").length
	
	if(nAll==nChecked)
	{
		$("#MachineList input:first").prop("checked",true);
	}
	else
	{
		$("#MachineList input:first").prop("checked",false);
	}
	
	SortFilament();
}

function ChooseAllFilament()
{
	let bCheck=$("#FilatypeList input:first").prop("checked");	
	$("#FilatypeList input").prop("checked",bCheck);	
	
	SortFilament();
}

function FilaClick()
{
	let nChecked=$("#FilatypeList input:gt(0):checked").length
	let nAll    =$("#FilatypeList input:gt(0)").length
	
	if(nAll==nChecked)
	{
		$("#FilatypeList input:first").prop("checked",true);
	}
	else
	{
		$("#FilatypeList input:first").prop("checked",false);
	}
	
	SortFilament();	
}

function ChooseAllVendor()
{
	let bCheck=$("#VendorList input:first").prop("checked");	
	$("#VendorList input").prop("checked",bCheck);	
	
	SortFilament();
}

function VendorClick()
{
	let nChecked=$("#VendorList input:gt(0):checked").length
	let nAll    =$("#VendorList input:gt(0)").length
	
	if(nAll==nChecked)
	{
		$("#VendorList input:first").prop("checked",true);
	}
	else
	{
		$("#VendorList input:first").prop("checked",false);
	}
	
	SortFilament();
}



function SortFilament()
{
	let FilaNodes=$("#ItemBlockArea .MItem");
	let nFilament=FilaNodes.length;
	//$("#ItemBlockArea .MItem").hide();
	
	//ModelList
	let pModel=$("#MachineList input:checked");
	let nModel=pModel.length;
	let ModelList=new Array();
	for(let n=0;n<nModel;n++)
	{
		let OneModel=pModel[n];
		ModelList.push(  OneModel.getAttribute("mode") );
	}
	
	//TypeList
	let pType=$("#FilatypeList input:gt(0):checked");
	let nType=pType.length;
	let TypeList=new Array();
	for(let n=0;n<nType;n++)
	{
		let OneType=pType[n];
		TypeList.push(  OneType.getAttribute("filatype") );
	}	
	
	//VendorList
	let pVendor=$("#VendorList input:gt(0):checked");
	let nVendor=pVendor.length;
	let VendorList=new Array();
	for(let n=0;n<nVendor;n++)
	{
		let OneVendor=pVendor[n];
		VendorList.push(  OneVendor.getAttribute("vendor") );
	}		
	
	
	//Update Filament UI
	for(let m=0;m<nFilament;m++)
	{
		let OneNode=FilaNodes[m];
		let OneFF=OneNode.getElementsByTagName("input")[0];
		
	    let fModel=OneFF.getAttribute("model");
		let fVendor=OneFF.getAttribute("vendor");
		let fType=OneFF.getAttribute("filatype");
		let fName=OneFF.getAttribute("name");
		
		if(TypeList.in_array(fType) && VendorList.in_array(fVendor))
		{
			let HasModel=false;
			for(let m=0;m<nModel;m++)
			{
				let ModelSrc=ModelList[m];
				
				if( ModelSrc=="all" || fModel.indexOf(ModelSrc)>=0)
				{
					HasModel=true;
					break;
				}
			}
			
			if(HasModel)
			    $(OneFF).prop("checked",true);
			else
				$(OneFF).prop("checked",false);
		}
		else
			$(OneFF).prop("checked",false);
	}
}

function ChooseDefaultFilament()
{
	//ModelList
	let pModel=$("#MachineList input:gt(0):checked");
	let nModel=pModel.length;
	let ModelList=new Array();
	for(let n=0;n<nModel;n++)
	{
		let OneModel=pModel[n];
		ModelList.push(  OneModel.getAttribute("mode") );
	}	
	
	//Filament
	let FilaNodes=$("#ItemBlockArea .MItem");
    let nFilament=FilaNodes.length;
    for(let m=0;m<nFilament;m++)
	{
		let OneNode=FilaNodes[m];
		let OneFF=OneNode.getElementsByTagName("input")[0];
		$(OneFF).prop("checked",false);
		
	    let fModel=OneFF.getAttribute("model");
		
		let HasModel=false;
		for(let m=0;m<nModel;m++)
		{
			let ModelSrc=ModelList[m];
		
			if( fModel.indexOf(ModelSrc)>=0)
			{
				HasModel=true;
				break;
			}
		}
			
		if(HasModel)
		    $(OneFF).prop("checked",true);
	}
	
	ShowNotice(0);
}


function ShowNotice( nShow )
{
	if(nShow==0)
	{
		$("#NoticeMask").hide();
		$("#NoticeBody").hide();
	}
	else
	{
		$("#NoticeMask").show();
		$("#NoticeBody").show();
	}
}


function ResponseFilamentResult()
{
	let FilaSelectedList= $("#ItemBlockArea input:checked");
	let nAll=FilaSelectedList.length;

	if( nAll==0 )
	{
		ShowNotice(1);
		return false;
	}
	
	let FilaArray=new Array();
	for(let n=0;n<nAll;n++)
	{
		let sName=FilaSelectedList[n].getAttribute("name");
		FilaArray.push(sName);
	}
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_userguide_filaments";
	tSend['data']={};
	tSend['data']['filament']=FilaArray;
	
	SendWXMessage( JSON.stringify(tSend) );
	
	return true;
}


function CancelSelect()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="user_guide_cancel";
	tSend['data']={};
		
	SendWXMessage( JSON.stringify(tSend) );			
}


function ConfirmSelect()
{
	let bRet=ResponseFilamentResult();
	
	if(bRet)
    {
		var tSend={};
		tSend['sequence_id']=Math.round(new Date() / 1000);
		tSend['command']="user_guide_finish";
		tSend['data']={};
		tSend['data']['action']="finish";
		
		SendWXMessage( JSON.stringify(tSend) );			
	}
}




