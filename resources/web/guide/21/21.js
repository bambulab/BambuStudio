function OnInit()
{
	//let strInput=JSON.stringify(cData);
	//HandleModelList(cData);
	
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
//	alert(strInput);
//	alert(JSON.stringify(strInput));
//	
//	let pVal=IsJson(strInput);
//	if(pVal==null)
//	{
//		alert("Msg Format Error is not Json");
//		return;
//	}
	
	let strCmd=pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='response_userguide_profile')
	{
		HandleModelList(pVal['response']);
	}
}


function ShowPrinterThumb(pItem, strImg)
{
	$(pItem).attr('src',strImg);
	$(pItem).attr('onerror',null);
}

function ChooseModel( ModelName )
{
	let ChooseItem=$(".ModelCheckBox[model='"+ModelName+"']");
	
	if(ChooseItem!=null)
	{
		if( $(ChooseItem).hasClass('ModelCheckBoxSelected') )
			$(ChooseItem).removeClass('ModelCheckBoxSelected');
		else
			$(ChooseItem).addClass('ModelCheckBoxSelected');		
	}		
}

function HandleModelList( pVal )
{
	if( !pVal.hasOwnProperty("model") )
		return;

    let pModel=pVal['model'];
	
	let nTotal=pModel.length;
	let ModelHtml={};
	for(let n=0;n<nTotal;n++)
	{
		let OneModel=pModel[n];
		
		let strVendor=OneModel['vendor'];
		
		//Add Vendor Html Node
		if($(".OneVendorBlock[vendor='"+strVendor+"']").length==0)
		{
			let sVV=strVendor;
			if( sVV=="BBL" )
				sVV="Bambu Lab";
			
			let HtmlNewVendor='<div class="OneVendorBlock" Vendor="'+strVendor+'">'+
'<div class="BlockBanner">'+
'	<a>'+sVV+'</a>'+				
'	<div class="BannerBtns">'+
'		<div class="SmallBtn_Green trans" tid="t11" onClick="SelectPrinterAll('+"\'"+strVendor+"\'"+')">all</div>'+
'		<div class="SmallBtn trans" tid="t12" onClick="SelectPrinterNone('+"\'"+strVendor+"\'"+')">none</div>'+
'	</div>'+
'</div>'+
'<div class="PrinterArea">	'+
'</div>'+
'</div>';
			
			if(sVV=='Bambu Lab')
				$('#Content').html( HtmlNewVendor + $('#Content').html() );
			else
				$('#Content').append( HtmlNewVendor );
		}
		
		let ModelName=OneModel['model'];
		
		//Collect Html Node Nozzel Html
		if( !ModelHtml.hasOwnProperty(strVendor))
			ModelHtml[strVendor]='';
			
		let CoverImage="../../image/printer/"+OneModel['model']+"_cover.png";
		let	CoverImage2="../../../profiles/"+strVendor+"/"+OneModel['model']+"_cover.png";
		let CoverImage3=pVal['configpath']+"/system/"+strVendor+"/"+OneModel['model']+"_cover.png";
		
		//alert( 'FinalCover: '+FinalCover );
		ModelHtml[strVendor]+='<div class="PrinterBlock">'+
        '<div class="PImg">'+
		    '<img class="ModelThumbnail" src="'+CoverImage3+'" onerror="ShowPrinterThumb(this,\''+CoverImage2+'\')" />'+
			'<div class="ModelCheckBox" model="'+OneModel['model']+'" onClick="ChooseModel(\''+OneModel['model']+'\')"></div>'+
		'</div>'+
        '    <div class="PName">'+OneModel['model']+'</div>'+ 
		'</div>';
		
	}
	
	//Update Nozzel Html Append
	for( let key in ModelHtml )
	{
		$(".OneVendorBlock[vendor='"+key+"'] .PrinterArea").append( ModelHtml[key] );
	}
	
	
	//Update Checkbox
	for(let m=0;m<nTotal;m++)
	{
		let OneModel=pModel[m];
	
		let SelectList=OneModel['nozzle_selected'];
		if(SelectList!='')
		{
			ChooseModel(OneModel['model']);			
		}
	}	

	let AlreadySelect=$(".ModelCheckBoxSelected");
	let nSelect=AlreadySelect.length;
	if(nSelect==0)
	{	
		$("div.OneVendorBlock[vendor='BBL'] .ModelCheckBox").addClass('ModelCheckBoxSelected');
	}
	
	TranslatePage();
}


function SelectPrinterAll( sVendor )
{
	$("div.OneVendorBlock[vendor='"+sVendor+"'] .ModelCheckBox").addClass('ModelCheckBoxSelected');
}


function SelectPrinterNone( sVendor )
{
	$("div.OneVendorBlock[vendor='"+sVendor+"'] .ModelCheckBox").removeClass('ModelCheckBoxSelected');
}


//
function GotoFilamentPage()
{
	let nChoose=OnExit();
	
	if(nChoose>0)
		window.open('../22/index.html','_self');
}

function OnExit()
{	
	let ModelAll={};
	
	let ModelSelect=$(".ModelCheckBoxSelected");
	let nTotal=ModelSelect.length;

	if( nTotal==0 )
	{
		ShowNotice(1);
		
		return 0;
	}
	
	for(let n=0;n<nTotal;n++)
	{
	    let OneItem=ModelSelect[n];
		
		let strModel=OneItem.getAttribute("model");
			
		//alert(strModel+strVendor+strNozzel);
		
		if(!ModelAll.hasOwnProperty(strModel))
		{
			//alert("ADD: "+strModel);
			
			ModelAll[strModel]={};
		
			ModelAll[strModel]["model"]=strModel;
		}
	}
		
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_userguide_models";
	tSend['data']=ModelAll;
	
	SendWXMessage( JSON.stringify(tSend) );

    return nTotal;
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




