//var TestData={"sequence_id":"0","command":"get_recent_projects","response":[{"path":"D:\\work\\Models\\Toy\\3d-puzzle-cube-model_files\\3d-puzzle-cube.3mf","time":"2022\/3\/24 20:33:10"},{"path":"D:\\work\\Models\\Art\\Carved Stone Vase - remeshed+drainage\\Carved Stone Vase.3mf","time":"2022\/3\/24 17:11:51"},{"path":"D:\\work\\Models\\Art\\Kity & Cat\\Cat.3mf","time":"2022\/3\/24 17:07:55"},{"path":"D:\\work\\Models\\Toy\\鐩村墤.3mf","time":"2022\/3\/24 17:06:02"},{"path":"D:\\work\\Models\\Toy\\minimalistic-dual-tone-whistle-model_files\\minimalistic-dual-tone-whistle.3mf","time":"2022\/3\/22 21:12:22"},{"path":"D:\\work\\Models\\Toy\\spiral-city-model_files\\spiral-city.3mf","time":"2022\/3\/22 18:58:37"},{"path":"D:\\work\\Models\\Toy\\impossible-dovetail-puzzle-box-model_files\\impossible-dovetail-puzzle-box.3mf","time":"2022\/3\/22 20:08:40"}]};

var m_HotModelList=null;
var m_HasNetworkPlugin=true;

function OnInit()
{
	//-----Official-----
    TranslatePage();

	SendMsg_GetLoginInfo();
	GotoMenu( 'home' );
}

function HandleStudio( pVal )
{
	let strCmd = pVal['command'];
	

	if(strCmd=='studio_userlogin')
	{
		SetLoginInfo(pVal['data']['avatar'],pVal['data']['name']);
	}
	else if(strCmd=='studio_useroffline')
	{
		SetUserOffline();
	}
	else if( strCmd=="network_plugin_installtip" )
	{
		let nShow=pVal["show"]*1;
		
	    if(nShow==1)
		{
			$("#NoPluginTip").show();
			$("#NoPluginTip").css("display","flex");
			
			m_HasNetworkPlugin=false;
		}
		else
		{
			$("#NoPluginTip").hide();
			m_HasNetworkPlugin=true;
		}
	}	
	else if(strCmd=='homepage_leftmenu_clicked')
	{						
		let NewMenu=pVal['menu'];
		//alert('LeftMenu Clicked:'+strMenu );
		
		GotoMenu(NewMenu);
	}
	else if(strCmd=='homepage_leftmenu_newtag')
	{		
		let NewMenu=pVal['menu'];
		let nShow=pVal['show'];
		
		ShowMenuNewTag(NewMenu,nShow);
	}	
	else if(strCmd=='homepage_leftmenu_show')
	{		
		let NewMenu=pVal['menu'];
		let nShow=pVal['show'];
		
		ShowMenuBtn(NewMenu,nShow);
	}	
}

var NowMenu='';
function GotoMenu( strMenu )
{
	ShowMenuNewTag(strMenu,0);
	
	if(NowMenu==strMenu && strMenu!='makersupply')
		return;
	
	NowMenu=strMenu;
	
	let MenuList=$(".BtnItem");
	let nAll=MenuList.length;
	
	for(let n=0;n<nAll;n++)
	{
		let OneBtn=MenuList[n];
		
		if( $(OneBtn).attr("menu")==strMenu )
		{
			if(strMenu!=='makersupply')
			{
				$(".BtnItem").removeClass("BtnItemSelected");						
				$(OneBtn).addClass("BtnItemSelected");
			}
						
			//SendWX
			var tSend={};
			tSend['sequence_id']=Math.round(new Date() / 1000);
			tSend['command']="homepage_leftmenu_clicked";
			tSend['menu']=strMenu;
			tSend['refresh']=0;
			
			SendWXMessage( JSON.stringify(tSend) );	
		}
	}
}

function ShowMenuNewTag(MenuName,nStatus)
{	
	//alert(MenuName+" - "+nStatus);
	if(MenuName=='online')
	{
		if(nStatus==1)
			$('#OnlineNewTag').show();
		else
			$('#OnlineNewTag').hide();
	}
	else if(MenuName=='makerlab')
	{
		if(nStatus==1)
			$('#MakerlabNewTag').show();
		else
			$('#MakerlabNewTag').hide();
	}	
}

function ShowMenuBtn( MenuName,nShow)
{
	let sKey='div[menu="'+MenuName+'"]';
	
	if(nShow==1)
		$(sKey).css('display','flex');
	else
		$(sKey).css('display','none');
}


function SetLoginInfo( strAvatar, strName ) 
{
	$("#Login1").hide();
	
	$("#UserName").text(strName);
	
    let OriginAvatar=$("#UserAvatarIcon").prop("src");
	if(strAvatar!=OriginAvatar)
		$("#UserAvatarIcon").prop("src",strAvatar);
	else
	{
		//alert('Avatar is Same');
	}
	
	$("#Login2").show();
	$("#Login2").css("display","flex");
}

function SetUserOffline()
{
	$("#UserAvatarIcon").prop("src","img/c.jpg");
	$("#UserName").text('');
	$("#Login2").hide();	
	
	$("#Login1").show();
	$("#Login1").css("display","flex");
}

function SetMallUrl( strUrl )
{
	$("#MallWeb").prop("src",strUrl);
}

/*-------RecentFile MX Message------*/
function SendMsg_GetLoginInfo()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="get_login_info";
	
	SendWXMessage( JSON.stringify(tSend) );	
}

function OnLoginOrRegister()
{
	var tSend={};
	
	if( m_HasNetworkPlugin )
	{
		tSend['sequence_id']=Math.round(new Date() / 1000);
		tSend['command']="homepage_login_or_register";		
	}
	else
	{
		tSend['sequence_id']=Math.round(new Date() / 1000);
		tSend['command']="homepage_need_networkplugin";
	}
		
	SendWXMessage( JSON.stringify(tSend) );	
}

function OnLogOut()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_logout";
	
	SendWXMessage( JSON.stringify(tSend) );	
}

function SendMsg_CheckNewTag()
{	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_leftmenu_newtag";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function BeginDownloadNetworkPlugin()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="begin_network_plugin_download";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

var WidthBoundary=168;
function ChangeLeftWidth()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_leftmenu_change_width";
	
	let NowWidth=window.innerWidth;
	if(NowWidth<=WidthBoundary)
	{
		tSend['width']=224;
		
		ShowLeftMenuTip( false );
	}
	else
	{
		tSend['width']=64;
		
		ShowLeftMenuTip( true );
	}

	SendWXMessage( JSON.stringify(tSend) );		
}

function ShowLeftMenuTip( bShow )
{
	if(bShow==true)
	{
		$("div[menu='home'] div.BtnIcon").prop('title',GetCurrentTextByKey('t114'));
		$("div[menu='recent'] div.BtnIcon").prop('title',GetCurrentTextByKey('t35'));
		$("div[menu='online'] div.BtnIcon").prop('title',GetCurrentTextByKey('t107'));
		$("div[menu='makerlab'] div.BtnIcon").prop('title','MakerLab');
		$("div[menu='makersupply'] div.BtnIcon").prop('title',GetCurrentTextByKey('t125'));
		$("div[menu='printhistory'] div.BtnIcon").prop('title',GetCurrentTextByKey('t117'));
		$("div[menu='manual'] div.BtnIcon").prop('title',GetCurrentTextByKey('t87'));
	}
	else
	{
		$("div[menu='home'] div.BtnIcon").removeAttr('title');
		$("div[menu='recent'] div.BtnIcon").removeAttr('title');
		$("div[menu='online'] div.BtnIcon").removeAttr('title');
		$("div[menu='makerlab'] div.BtnIcon").removeAttr('title');
		$("div[menu='makersupply'] div.BtnIcon").removeAttr('title');
		$("div[menu='printhistory'] div.BtnIcon").removeAttr('title');
		$("div[menu='manual'] div.BtnIcon").removeAttr('title');
	}
}

//---------------Global-----------------
window.postMessage = HandleStudio;
