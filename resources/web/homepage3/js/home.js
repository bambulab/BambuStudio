//var TestData={"sequence_id":"0","command":"get_recent_projects","response":[{"path":"D:\\work\\Models\\Toy\\3d-puzzle-cube-model_files\\3d-puzzle-cube.3mf","time":"2022\/3\/24 20:33:10"},{"path":"D:\\work\\Models\\Art\\Carved Stone Vase - remeshed+drainage\\Carved Stone Vase.3mf","time":"2022\/3\/24 17:11:51"},{"path":"D:\\work\\Models\\Art\\Kity & Cat\\Cat.3mf","time":"2022\/3\/24 17:07:55"},{"path":"D:\\work\\Models\\Toy\\鐩村墤.3mf","time":"2022\/3\/24 17:06:02"},{"path":"D:\\work\\Models\\Toy\\minimalistic-dual-tone-whistle-model_files\\minimalistic-dual-tone-whistle.3mf","time":"2022\/3\/22 21:12:22"},{"path":"D:\\work\\Models\\Toy\\spiral-city-model_files\\spiral-city.3mf","time":"2022\/3\/22 18:58:37"},{"path":"D:\\work\\Models\\Toy\\impossible-dovetail-puzzle-box-model_files\\impossible-dovetail-puzzle-box.3mf","time":"2022\/3\/22 20:08:40"}]};

var m_HotModelList=null;
var m_ForUModelList=null;

var m_MakerlabList=null;
var m_PrintHistoryList=null;

function OnHomeInit()
{
	//-----Official-----
    TranslatePage();

	SendMsg_GetRecentFile();
	SendMsg_GetStaffPick();
    SendMsg_GetMakerlabList();
	SendMsg_GetPrintHistory();	    
	
    document.getElementById('HotModel_Search_Input').onkeydown = function (event) {
		if (event.key === 'Enter') {
			OnSearchOnline();
		}
		
		event.defaultPrevented();
    };
	
	let ModelSearchTip=GetCurrentTextByKey('t122');
	$('#HotModel_Search_Input').prop('placeholder', ModelSearchTip);
	
	//Test
	//ShowMakerlabList(Test_MakerlabList['list']);
	
    //$('#PrintHistoryArea').show();
    //ShowPrintHistory(Test_PrintTaskList['hits']);
	
	//Show4UPick(Test_4UModelList['hits']);
}

//Recent详情页面的状态
var Recent_Normal=1;
var Recent_BatchDelete=2;

var RecentPage_Mode=Recent_Normal;

function OnRecentInit()
{
	TranslatePage();

	SendMsg_GetRecentFile();
    Set_RecentFile_Delete_Checkbox_Event();
}

var m_LineMenuName='';
function OnLineInit()
{
	TranslatePage();
	m_LineMenuName=GetQueryString("menu");
}

function OnLineRetry()
{
	if(m_LineMenuName!="" && m_LineMenuName!=null)
		SwitchContent(m_LineMenuName);
}

function ShowLineWarn( bShow )
{
	if(bShow)
		$('#WarnMainArea').show();
	else
		$('#WarnMainArea').hide();
}

//------最佳打开文件的右键菜单功能----------
var RightBtnFilePath='';

var MousePosX=0;
var MousePosY=0;
var sImages = {};
 
function Set_RecentFile_MouseRightBtn_Event()
{
	$(".FileItem").mousedown(
		function(e)
		{			
			//FilePath
			RightBtnFilePath=$(this).attr('fpath');
			
			if(e.which == 3){
				//鼠标点击了右键+$(this).attr('ff') );
				ShowRecnetFileContextMenu();
			}else if(e.which == 2){
				//鼠标点击了中键
			}else if(e.which == 1){
				//鼠标点击了左键
				OnOpenRecentFile( encodeURI(RightBtnFilePath) );
			}
		});

	$(document).bind("contextmenu",function(e){
		//在这里书写代码，构建个性右键化菜单
		return false;
	});	
	
    $(document).mousemove( function(e){
		MousePosX=e.pageX;
		MousePosY=e.pageY;
		
		let ContextMenuWidth=$('#recnet_context_menu').width();
		let ContextMenuHeight=$('#recnet_context_menu').height();
	
		let DocumentWidth=$(document).width();
		let DocumentHeight=$(document).height();
		
		//$("#DebugText").text( ContextMenuWidth+' - '+ContextMenuHeight+'<br/>'+
		//					 DocumentWidth+' - '+DocumentHeight+'<br/>'+
		//					 MousePosX+' - '+MousePosY +'<br/>' );
	} );
	

	$(document).click( function(){		
		var e = e || window.event;
        var elem = e.target || e.srcElement;
        while (elem) {
			if (elem.id && elem.id == 'recnet_context_menu') {
                    return;
			}
			elem = elem.parentNode;
		}		
		
		$("#recnet_context_menu").hide();
	} );

	
}


function HandleStudio( pVal )
{
	let strCmd = pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='get_recent_projects')
	{
		ShowRecentFileList(pVal['response']);
	}
	else if( strCmd=="studio_set_mallurl" )
	{
		SetMallUrl( pVal['data']['url'] );
	}
	else if( strCmd=="studio_clickmenu" )
	{
		let strName=pVal['data']['menu'];
		
		GotoMenu(strName);
	}
	else if( strCmd=="network_plugin_installtip" )
	{
		let nShow=pVal["show"]*1;
		
	    if(nShow==1)
		{
			$("#NoPluginTip").show();
			$("#NoPluginTip").css("display","flex");
		}
		else
		{
			$("#NoPluginTip").hide();
		}
	}
	else if( strCmd=="modelmall_model_advise_get")
	{
		//alert('hot');
		if( m_HotModelList!=null && pVal['hits'].length>0 )
		{
			let SS1=JSON.stringify(pVal['hits']);
			let SS2=JSON.stringify(m_HotModelList);
			
			if( SS1==SS2 )
				return;
		}
		
	    m_HotModelList=pVal['hits'];		
		ShowStaffPick( m_HotModelList );
	}
	else if( strCmd=="modelmall_model_customized_get")
	{
		//alert('For U');	
		if( m_ForUModelList!=null && pVal['hits'].length>0 )
		{
			let SS1=JSON.stringify(pVal['hits']);
			let SS2=JSON.stringify(m_ForUModelList);
			
			if( SS1==SS2 )
				return;
		}
		
	    m_ForUModelList=pVal['hits'];		
		Show4UPick( m_ForUModelList );
	}
	else if(strCmd=='homepage_makerlab_get')
	{			
		if( m_MakerlabList!=null && pVal['list'].length>0 )
		{
			let SS1=JSON.stringify(pVal['list']);
			let SS2=JSON.stringify(m_MakerlabList);
			
			if( SS1==SS2 )
				return;
		}
		
	    m_MakerlabList=pVal['list'];	
				
		ShowMakerlabList(m_MakerlabList);
	}
	else if(strCmd=='homepage_leftmenu_clicked')
	{
		let strName=pVal['menu'];
		OnBoardChange(strName);
	}
	else if(strCmd=='homepage_rightarea_reset')
	{
		$('#HotModelList').html('');
   	    $('#HotModelArea').hide();
		m_HotModelList=null;
		m_ForUModelList=null;
		
		$('#LabList').html('');
	    $('#MakerlabArea').hide();
		m_MakerlabList=null;
		
		OnHomeInit();
	}
	else if(strCmd=='printhistory_task_show')
	{
		if( m_PrintHistoryList!=null && pVal['hits'].length>0 )
		{
			let SS1=JSON.stringify(pVal['list']);
			let SS2=JSON.stringify(m_PrintHistoryList);
			
			if( SS1==SS2 )
			{
				alert("PrintHistory is Same. Ignore");
				return;
			}
		}
		
	    m_PrintHistoryList=pVal['hits'];
				
		ShowPrintHistory(m_PrintHistoryList);
	}
	else if(strCmd=='homepage_leftmenu_show')
	{
		let MenuName=pVal['menu'];
		let nShow=pVal['show']*1;
		
		if(MenuName=='printhistory')
		{		
			if(nShow==1)
			{
				$('#PrintHistoryArea').show();
			}
			else
			{
				$('#PrintHistoryArea').hide();
				m_PrintHistoryList=null;
				$('#PrintHistoryList').html('');
			}
		}		
	}
}

function OnBoardChange( strMenu )
{
	if( strMenu=='home' )
	{
		$('#MenuArea').css('display','flex');	
		$('#HomeFullArea').css('display','inline');	
		$('#RecentFileArea').css('display','none');		
		$('#WikiGuideBoard').css('display','none');
		
		if( (m_HotModelList==null || m_HotModelList.length==0) && (m_ForUModelList==null || m_ForUModelList.length==0))
			SendMsg_GetStaffPick();
		
		if( m_MakerlabList==null || m_MakerlabList.length==0 )
			SendMsg_GetMakerlabList();
		
		if( m_PrintHistoryList==null )
			SendMsg_GetPrintHistory();
	}
	else if(strMenu=='recent')
	{
		$('#MenuArea').css('display','flex');			
		$('#HomeFullArea').css('display','none');		
		$('#RecentFileArea').css('display','flex');		
		$('#WikiGuideBoard').css('display','none');		
	}
	else if(strMenu=='manual')
	{
		$('#MenuArea').css('display','none');
		$('#HomeFullArea').css('display','none');		
		$('#RecentFileArea').css('display','none');		
		$('#WikiGuideBoard').css('display','flex');			
	}		
}

function SwtichLeftMenu( strMenu )
{
	//SendWX
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_leftmenu_switch";
	tSend['menu']=strMenu;
	
	SendWXMessage( JSON.stringify(tSend) );
}

function SetMallUrl( strUrl )
{
	$("#MallWeb").prop("src",strUrl);
}


function ShowRecentFileList( pList )
{
	let nTotal=pList.length;
	
	let strHtml='';
	for(let n=0;n<nTotal;n++)
	{
		let OneFile=pList[n];
		
		let sPath=OneFile['path'];
		let sImg=OneFile["image"] || sImages[sPath];
		let sTime=OneFile['time'];
		let sName=OneFile['project_name'];
		sImages[sPath] = sImg;
		
		//let index=sPath.lastIndexOf('\\')>0?sPath.lastIndexOf('\\'):sPath.lastIndexOf('\/');
		//let sShortName=sPath.substring(index+1,sPath.length);
		
		let TmpHtml='<div class="FileItem GuideBlock"  fpath="'+sPath+'"  >'+
				'<a class="FileTip" title="'+sPath+'"></a>'+
				'<div class="FileImg" ><img src="'+sImg+'" onerror="this.onerror=null;this.src=\'img/d.png\';"  alt="No Image"  /></div>'+
				'<div class="FileName TextS1">'+sName+'</div>'+
				'<div class="FileDate TextS2">'+sTime+'</div>'+
				'<div class="FileMask"></div>'+
				'<div class="FileCheckBox"></div>'+
			    '</div>';
		
		strHtml+=TmpHtml;
	}
	
	$("#FileList").html(strHtml);
	$("#MiniFileList").html(strHtml);
	
    Set_RecentFile_MouseRightBtn_Event();
	UpdateRecentClearBtnDisplay();
	Set_RecentFile_Delete_Checkbox_Event();
}

function ShowRecnetFileContextMenu()
{
	if( RecentPage_Mode!=Recent_Normal )
		return;
	
	$("#recnet_context_menu").offset({top: 10000, left:-10000});
	$('#recnet_context_menu').show();
	
	let ContextMenuWidth=$('#recnet_context_menu').width();
	let ContextMenuHeight=$('#recnet_context_menu').height();
	
    let DocumentWidth=$(document).width();
	let DocumentHeight=$(document).height();

	let RealX=MousePosX;
	let RealY=MousePosY;
	
	if( MousePosX + ContextMenuWidth + 24 >DocumentWidth )
		RealX=DocumentWidth-ContextMenuWidth-24;
	if( MousePosY+ContextMenuHeight+24>DocumentHeight )
		RealY=DocumentHeight-ContextMenuHeight-24;
	
	$("#recnet_context_menu").offset({top: RealY, left:RealX});
}

/*-------RecentFile MX Message------*/
function SendMsg_GetLoginInfo()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="get_login_info";
	
	SendWXMessage( JSON.stringify(tSend) );	
}


function SendMsg_GetRecentFile()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="get_recent_projects";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function OnClickModelDepot()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_modeldepot";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function OnClickNewProject()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_newproject";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function OnClickOpenProject()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_openproject";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

let RecentClick=true;
function OnOpenRecentFile( strPath )
{
	if( RecentPage_Mode!=Recent_Normal )
		return;

	if(RecentClick)
	{
        RecentClick = false;
        setTimeout(() => {
            RecentClick = true;
        }, 1000);		
		
		var tSend={};
		tSend['sequence_id']=Math.round(new Date() / 1000);
		tSend['command']="homepage_open_recentfile";
		tSend['data']={};
		tSend['data']['path']=decodeURI(strPath);
	
		SendWXMessage( JSON.stringify(tSend) );	
	}
}

function OnDeleteRecentFile( )
{
	//Clear in UI
	$("#recnet_context_menu").hide();
	
	let AllFile=$(".FileItem");
	let nFile=AllFile.length;
	for(let p=0;p<nFile;p++)
	{
		let pp=AllFile[p].getAttribute("fpath");
		if(pp==RightBtnFilePath)
			$(AllFile[p]).remove();
	}	
	
	UpdateRecentClearBtnDisplay();
	
	//Send Msg to C++
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_delete_recentfile";
	tSend['data']={};
	tSend['data']['path']=RightBtnFilePath;
	
	SendWXMessage( JSON.stringify(tSend) );
}

function OnDeleteAllRecentFiles()
{
	$('#FileList').html('');
	UpdateRecentClearBtnDisplay();
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_delete_all_recentfile";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function UpdateRecentClearBtnDisplay()
{
    let AllFile=$("#RecentFileArea .FileItem");
	let nFile=AllFile.length;	
	if( nFile>0 )
	{
		$("#Menu_Clear").show();
		$('#Menu_Batch').show();
	}
	else
	{
		$("#Menu_Clear").hide();
		$('#Menu_Batch').hide();
	}
}

function OnExploreRecentFile( )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_explore_recentfile";
	tSend['data']={};
	tSend['data']['path']=decodeURI(RightBtnFilePath);
	
	SendWXMessage( JSON.stringify(tSend) );	
	
	$("#recnet_context_menu").hide();
}

function BeginDownloadNetworkPlugin()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="begin_network_plugin_download";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function SendMsg_GetMakerlabList()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_makerlab_get";
	
	SendWXMessage( JSON.stringify(tSend) );
	
	setTimeout("SendMsg_GetMakerlabList()",3600*1000*6);
}

function SwitchContent(strMenu)
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);		
	tSend['command']="homepage_leftmenu_clicked";
	tSend['menu']=strMenu;
	
	SendWXMessage( JSON.stringify(tSend) );	
}

function OnBatchDelete()
{
	//切换页面工作模式
	RecentPage_Mode=Recent_BatchDelete;
	
	$('#Menu_Batch').hide();
	$('#Menu_Clear').hide();
	
	$('#Menu_Delete').css('display','flex');
	$('#Menu_Cancel').css('display','flex');

	$('.FileCheckBox.FileCheckBox_checked').removeClass('FileCheckBox_checked');
	$('.FileCheckBox').show();	
}

function OnCancelDelete()
{
	//切换页面工作模式
	RecentPage_Mode=Recent_Normal;
	
	$('#Menu_Batch').css('display','flex');
	$('#Menu_Clear').css('display','flex');
	
	$('#Menu_Delete').hide();
	$('#Menu_Cancel').hide();
	
	
	$('.FileCheckBox.FileCheckBox_checked').removeClass('FileCheckBox_checked');
	$('.FileCheckBox').hide();
	$('.FileMask').hide();
}

function OnMultiDelete()
{
	let ChooseFiles=$('.FileCheckBox.FileCheckBox_checked');
	let nChoose=ChooseFiles.length;

	var tBatchDel={};
	tBatchDel['sequence_id']=Math.round(new Date() / 1000);
	tBatchDel['command']="homepage_delete_recentfile";
	tBatchDel['data']={};	
	
	for(let n=0;n<nChoose;n++)
	{
		let OneItem=ChooseFiles[n];
		let ParentItem=$(OneItem).parent();
		
		let fPath=$(ParentItem).attr("fpath");

		//删除文件对象
		$(ParentItem).remove();
		
		//发送WX消息
	    tBatchDel['data']['path']=fPath;	
   	    SendWXMessage( JSON.stringify(tBatchDel) );		
	}
	
	//更新按钮状态
	OnCancelDelete();	
	UpdateRecentClearBtnDisplay();	
}

function Set_RecentFile_Delete_Checkbox_Event()
{
	$(".FileCheckBox").mousedown(
		function(e)
		{			
			//FilePath		
			if(e.which == 3){
				//鼠标点击了右键+$(this).attr('ff') );
			}else if(e.which == 2){
				//鼠标点击了中键
			}else if(e.which == 1){
				//鼠标点击了左键
				if( $(this).hasClass('FileCheckBox_checked') )
				{
					$(this).removeClass('FileCheckBox_checked');
					$(this).prev('.FileMask').hide();
				}
				else
				{
					$(this).addClass('FileCheckBox_checked');
					$(this).prev('.FileMask').show();					
				}
			}
		});	
}

//-------------User Manual------------

function OpenWikiUrl( strUrl )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="userguide_wiki_open";
	tSend['data']={};
	tSend['data']['url']=strUrl;
	
	SendWXMessage( JSON.stringify(tSend) );	
}

//--------------Staff Pick-------
var StaffPickSwiper=null;
function InitStaffPick()
{

}

function SendMsg_GetStaffPick()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="modelmall_model_advise_get";
	
	SendWXMessage( JSON.stringify(tSend) );
	
    setTimeout("SendMsg_GetStaffPick()",3600*1000*6);
}

function ExNumber( number )
{
	let nNew=number;
	if( number>=1000*1000*1000 )
    {
		nNew=Math.round(number/(1000*1000*1000)*10)/10;
		nNew=nNew+'b';
	}
	else if( number>=1000*1000 )
    {
		nNew=Math.round(number/(1000*1000)*10)/10;
		nNew=nNew+'m';
	}
	if( number>=1000 )
    {
		nNew=Math.round(number/(1000)*10)/10;
		nNew=nNew+'k';
	}	
	
	return nNew;
}

function ShowStaffPick( ModelList )
{
	let PickTotal=ModelList.length;
	if(PickTotal==0)
	{
		$('#HotModelList').html('');
		$('#HotModelArea').hide();
		
		return;
	}
	
	$("#Online_Models_Bar").css('display','flex');
	$("#ForU_Models_Bar").css('display','none');	
	
	let strPickHtml='';
	for(let a=0;a<PickTotal;a++)
	{
		let OnePickModel=ModelList[a];
		
		let ModelID=OnePickModel['design']['id'];
		let ModelName=OnePickModel['design']['title'];
		let ModelCover=OnePickModel['design']['cover']+'?image_process=resize,w_360/format,webp';
		
		let DesignerName=OnePickModel['design']['designCreator']['name'];
		let DesignerAvatar=OnePickModel['design']['designCreator']['avatar']+'?image_process=resize,w_32/format,webp';
		
		let NumZan=OnePickModel['design']['likeCount'];
		let NumDownload=OnePickModel['design']['downloadCount'];
		NumZan=ExNumber(NumZan);
		NumDownload=ExNumber(NumDownload);
			
		strPickHtml+='			<div class="HotModelPiece GuideBlock" onClick="OpenOneStaffPickModel('+ModelID+')">'+
				'<div class="HotModel_PrevBlock">'+
				'	<img class="HotModel_PrevImg" src="'+ModelCover+'" />'+
				'</div>'+
				'<div class="HotModel_Designer_Info">'+
				'  <div class="HotModel_Author_HeadIcon">'+
				'    <img src="'+DesignerAvatar+'" />'+
				'  </div>'+
				'  <div class="HotModel_Right_1">'+
				'    <div class="HotModel_Name TextS1">'+ModelName+'</div>'+
				'    <div class="HotModel_Right_1_2">'+
				'      <div class="HotModel_Author_Name TextS2">'+DesignerName+'</div>'+
				'      <div class="HotModel_click_info TextS2">'+
				'        <div class="Model_Click_Number"><img src="img/zan.svg"><span>'+NumZan+'</span></div>'+
				'        <div class="Model_Click_Number"><img src="img/xia.svg"><span>'+NumDownload+'</span></div>'+
			    '		  </div>'+
				'    </div>'+
				'  </div>'+
				'</div>'+
			    '</div>';					
	}

	$('#HotModelList').html(strPickHtml);
	InitStaffPick();
	$('#HotModelArea').show();
	$('#HotModel_Search_Bar').css('display','flex');
}

function Show4UPick( ModelList )
{
	let PickTotal=ModelList.length;
	if(PickTotal==0)
	{
		$('#HotModelList').html('');
		$('#HotModelArea').hide();
		
		return;
	}
	
	$("#Online_Models_Bar").css('display','none');
	$("#ForU_Models_Bar").css('display','flex');	
	
	let strPickHtml='';
	for(let a=0;a<PickTotal;a++)
	{
		let OnePickModel=ModelList[a];
		
		let ModelID=OnePickModel['id'];
		let ModelName=OnePickModel['title'];
		let ModelCover=OnePickModel['cover']+'?image_process=resize,w_360/format,webp';
		
		let DesignerName=OnePickModel['designCreator']['name'];
		let DesignerAvatar=OnePickModel['designCreator']['avatar']+'?image_process=resize,w_32/format,webp';
		
		let NumZan=OnePickModel['likeCount'];
		let NumDownload=OnePickModel['downloadCount'];
		NumZan=ExNumber(NumZan);
		NumDownload=ExNumber(NumDownload);
		
		strPickHtml+='			<div class="HotModelPiece GuideBlock" onClick="OpenOneStaffPickModel('+ModelID+')">'+
				'<div class="HotModel_PrevBlock">'+
				'	<img class="HotModel_PrevImg" src="'+ModelCover+'" />'+
				'</div>'+
				'<div class="HotModel_Designer_Info">'+
				'  <div class="HotModel_Author_HeadIcon">'+
				'    <img src="'+DesignerAvatar+'" />'+
				'  </div>'+
				'  <div class="HotModel_Right_1">'+
				'    <div class="HotModel_Name TextS1">'+ModelName+'</div>'+
				'    <div class="HotModel_Right_1_2">'+
				'      <div class="HotModel_Author_Name TextS2">'+DesignerName+'</div>'+
				'      <div class="HotModel_click_info TextS2">'+
				'        <div class="Model_Click_Number"><img src="img/zan.svg"><span>'+NumZan+'</span></div>'+
				'        <div class="Model_Click_Number"><img src="img/xia.svg"><span>'+NumDownload+'</span></div>'+
			    '		  </div>'+
				'    </div>'+
				'  </div>'+
				'</div>'+
			    '</div>';			
	}

	$('#HotModelList').html(strPickHtml);
	InitStaffPick();
	$('#HotModelArea').show();
	$('#HotModel_Search_Bar').css('display','flex');
}

function OpenOneStaffPickModel( ModelID )
{
	//alert(ModelID);
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="modelmall_model_open";
	tSend['data']={};
	tSend['data']['id']=ModelID;
	
	SendWXMessage( JSON.stringify(tSend) );		
}


function OnSearchOnline(event)
{		
	let strKW=$('#HotModel_Search_Input').val().trim();
	if(strKW=='' )
		return;
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_online_search";
	tSend['keyword']=strKW;
	
	SendWXMessage( JSON.stringify(tSend) );	
}

//----------MakerLab------------
function IsChinese()
{
	let strLang=GetQueryString("lang");
	if(strLang!=null)
	{
	}
	else
	{
		strLang=localStorage.getItem(LANG_COOKIE_NAME);
	}
	
	if(strLang!=null)
		return strLang.includes('zh')
	else
		return false;
}

function ShowMakerlabList( LabList )
{
	let LabTotal=LabList.length;
	if(LabTotal==0)
	{
		$('#LabList').html('');
		$('#MakerlabArea').hide();
		
		return;
	}
	
	let bCN=IsChinese();
	
	let strLabHtml='';
	for(let a=0;a<LabTotal;a++)
	{
		let OneLabItem=LabList[a];
		let InfoItem=OneLabItem['info'];
		
		let LabImg=OneLabItem['thumbnail']+'?image_process=resize,w_360/format,webp';
		let LabUrl=OneLabItem['jumpTo'];

		let LabName='';
		let LabDesc='';
		let LabAuthor='';
		if(bCN && InfoItem.hasOwnProperty('zh_CN'))
		{
			LabName=InfoItem['zh_CN']['name'];
			LabDesc=InfoItem['zh_CN']['description'];
			LabAuthor=InfoItem['zh_CN']['author'];
		}
		else if( InfoItem.hasOwnProperty('en') )
		{
			LabName=InfoItem['en']['name'];
			LabDesc=InfoItem['en']['description'];
			LabAuthor=InfoItem['en']['author'];			
		}
		else
			continue;
		
		
		strLabHtml+='<div class="MakerlabItem GuideBlock" onClick="OnOpenOneMakerlab(\''+LabUrl+'\')" >'+
				'<div class="MakerlabImg"><img src="'+LabImg+'"/></div>'+
				'<div class="MakerlabTextBlock">'+
				'<div class="MakerlabName">'+LabName+'</div>'+
				'<div class="MakerlabDesc">'+LabDesc+'</div>'+
				'<div class="MakerlabAuthor">'+LabAuthor+'</div>'+
				'</div></div>';		
	}

	$('#LabList').html(strLabHtml);
	$('#MakerlabArea').show();			
}

function OnOpenOneMakerlab( ChildUrl )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_makerlab_open";
	tSend['url']=ChildUrl;
	
	SendWXMessage( JSON.stringify(tSend) );		
}

//-----------Print History------------
function SendMsg_GetPrintHistory()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_printhistory_get";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function convertTimeFormat(timeStr) 
{
	const date = new Date(timeStr);  
	const year = date.getFullYear(); // 取后两位年份
    const day = date.getDate();
    const month = date.getMonth() + 1;
    const hours = date.getHours();
    const minutes = date.getMinutes();
    return `${year}/${month.toString().padStart(2, '0')}/${day.toString().padStart(2, '0')} ${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
}


//只更新HTML内容，不控制显示/隐藏
function ShowPrintHistory( TaskList )
{
	let TaskTotal=TaskList.length;
	
	let strTaskHtml='';
	for(let a=0;a<TaskTotal;a++)
	{
		let OneTask=TaskList[a];
		
		let TaskID=OneTask['id'];
		let Status=OneTask['status'];
				
		let TaskName=OneTask['designTitle']!=''?OneTask['designTitle']:OneTask['title'];
		let CoverImg=OneTask['cover'];
		
		let DeviceName=OneTask['deviceName'];
		let CostTime=OneTask['costTime'];
		if( Status==2 || Status==3 )
		{
			if( OneTask['startTime']!=null && OneTask['endTime']!=null )
			{
				CostTime=DateToUnixstamp(OneTask['endTime'])-DateToUnixstamp(OneTask['startTime']);
			}
		}
		let strCostTime='';
		if(CostTime>=3600)
		{
			strCostTime=Math.round( (CostTime/3600)*10 )/10+'h';
		}
		else if(CostTime>=60)
		{
			strCostTime=Math.floor( CostTime/60 )+'min';
		}
		else 
			strCostTime=CostTime+'s';
		
		let PlateName='<span tid="t123" class="trans"></span>&nbsp;'+OneTask['plateIndex']+'<span tid="t124" class="trans"></span>';
		if( OneTask['plateName'].trim()!='' )
			PlateName+=' - '+OneTask['plateName'].trim();
		
		let StartTime=convertTimeFormat( OneTask['startTime'] );
		
		let isPublicProfile =OneTask['isPublicProfile'];
		let sMode=OneTask['mode'];
		
		strTaskHtml+=
			'<div class="PrintHistoryItem GuideBlock" onClick="OnOpenPrintHistory('+TaskID+')" >'+
			'	<div class="PrintHistoryImg"><img src="'+CoverImg+'" onerror="this.onerror=null;this.src=\'img/d.png\';" /></div>'+
			'	<div class="PrintHistoryTextBlock">'+
			'		<div class="PrintHistoryName TextS1">'+TaskName+'</div>'+
			'		<div class="PrintHistory_Line2">'+
			'			<img src="img/time.svg" /><span class="PH_PrintTime TextS2">'+strCostTime+'</span>'+
			'			<img src="img/device.svg" /><span class="PH_DeviceName TextS2">'+DeviceName+'</span>'+
			'		</div>'+
			'		<div class="PrintHistoryInfo">'+
			'			<div class="PrintHistoryPlate TextS2">'+
			'				<div class="PH_PlateName">'+PlateName+'</div>'+
			'				<div class="PH_PrintDate">('+StartTime+')</div>'+
			'			</div>';
		    switch(Status)
		{
			case 2:
				strTaskHtml+='		<div class="PrintHistoryStatus PH_Status_Success trans" tid="t119">Success</div>';
				break;
			case 3:
				strTaskHtml+='		<div class="PrintHistoryStatus PH_Status_Fail trans" tid="t120">Canceled</div>';
				break;
			default:
				strTaskHtml+='		<div class="PrintHistoryStatus PH_Status_Printing trans" tid="t118">Printing</div>';
				break;
		}
		strTaskHtml+=
			'		</div>'+
			'	</div>';
		if( isPublicProfile==false && sMode!='cloud_slice'  )
			strTaskHtml+='	<div class="PH_Gcode_Icon">Gcode</div>';
				
		strTaskHtml+='</div>';
	}

	$('#PrintHistoryList').html(strTaskHtml);
	TranslatePage();
}

function OnOpenPrintHistory( TaskID )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_printhistory_click";
	tSend['taskid']=TaskID*1;
	
	SendWXMessage( JSON.stringify(tSend) );
}

//---------------Global-----------------
window.postMessage = HandleStudio;
