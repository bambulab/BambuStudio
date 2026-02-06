var m_ModelID=null;

$(function () {
  // 向宿主请求数据（握手）
  TranslatePage();
  RequestProjectInfo();
});

function HandleStudio(pVal)
{
	let strCmd=pVal['command'];
	
	if(strCmd=='show_3mf_info')
	{
    const detail = pVal.model && pVal.model.model;
    const name = detail ? decodeURIComponent(detail.name || '').trim() : '';
    const description = detail ? decodeURIComponent(detail.description || '').trim() : '';

    if (!name || !description) {
      window.location.href = 'black.html';
      return;
    }
		ShowProjectInfo( pVal['model'] );
	}
	else if(strCmd=='clear_3mf_info')
	{
		ShowProjectInfo( null );
	}
	else if(strCmd=='3mf_detail_set_modelid')		
	{
		let ModelID=pVal['model_id'];
		
		// UpdateModelID( ModelID );
	}
}

//Push Command to C++		
function RequestProjectInfo()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_3mf_info";
		
	SendWXMessage( JSON.stringify(tSend) );		
}

function safeDecode(value) {
  if (!value) return '';
  try {
    return decodeURIComponent(value);
  } catch (err) {
    return value;
  }
}

function normalizeCoverName(value) {
  const decoded = safeDecode(value);
  if (!decoded) return '';
  const segments = decoded.split(/[/\\]/);
  return segments[segments.length - 1];
}

// function resetNavigation() {
//   const $ul = $('#sideNav ul');
//   $ul.empty();
//   $ul.append('<li class="nav-item active"><a href="#projectName">Project information</a></li>');
// }

// function addNavItem(anchor, label) {
//   const $ul = $('#sideNav ul');
//   if ($ul.find(`a[href="${anchor}"]`).length === 0) {
//     $ul.append(`<li class="nav-item"><a href="${anchor}">${label}</a></li>`);
//   }
// }

// function removeNavItem(anchor) {
//   $('#sideNav ul').find(`a[href="${anchor}"]`).parent().remove();
// }

function ShowProjectInfo( p3MF )
{
  //Check Data
  // resetNavigation();
  if (!p3MF) {
    $("#projectName").text('');
    $("#projectAuthor").text('');
    $("#projectDescription").html('');
    $('#projectGallery').hide();
    $("#profileName").text('');
    $("#profileAuthor").text('');
    $("#profileDescript").html('');
    // removeNavItem('#Accessories');
    // removeNavItem('#profileName');
    return;
  }
	let pModel=p3MF['model'];
	let pFile=p3MF['file'];
  let pProfile=p3MF['profile'];

  ShowModelInfo( pModel );
  ShowFileInfo( pFile );
  ShowProfileInfo( pProfile );
}

function ShowModelInfo(pModel)
{

  //==========Model Info==========
  // 先完整解码，再按白名单净化，保留安全样式与 https 图片
  let rawName = decodeURIComponent(pModel.name);
  let rawAuthor = decodeURIComponent(pModel.author);
  let rawDesc = decodeURIComponent(pModel.description);
  rawDesc = HtmlDecodeFrom3MF(rawDesc);

  const sanitizeCfg = {
    ADD_TAGS: ['span','img','font','u','s','a'],
    ADD_ATTR: ['style','color','href','target','rel','src','alt'],
    ALLOWED_URI_REGEXP: /^(?:(?:https?|data|blob):|[^a-z]|[a-z+.\-]+(?:[^a-z+.\-:]|$))/i
  };

  if( pModel.hasOwnProperty('model_id') )
	{
		let m_id=pModel['model_id']+'';
		m_ModelID = m_id.trim();
    if (m_ModelID != "") {
      if( !$('#projectName').hasClass('link') );
        $("#projectName").addClass("link");
    }else {
      $("#projectName").removeClass("link");
    }
    
	}
	let sModelName=DOMPurify.sanitize(rawName);
	let sModelAuthor=DOMPurify.sanitize(rawAuthor);
	let UploadType=pModel.upload_type.toLowerCase();
	let sLicence=pModel.license.toUpperCase();
  let sModelDesc=DOMPurify.sanitize(rawDesc, sanitizeCfg);
  let ModelPreviewList=pModel.preview_img;
  let modelImages=[];
  const projectCover = normalizeCoverName(pModel.cover_img);

  $("#projectName").text(sModelName);
  $("#projectAuthor").text(sModelAuthor);
  $("#projectDescription").html(sModelDesc);

  if(ModelPreviewList && ModelPreviewList.length > 0) {
    const previews = ModelPreviewList.slice();
    if (projectCover) {
      const coverIndex = previews.findIndex(function(item){
        if (!item || !item.filename) return false;
        const filename = normalizeCoverName(item.filename);
        return filename === projectCover;
      });
      if (coverIndex > 0) {
        const coverItem = previews.splice(coverIndex, 1)[0];
        previews.unshift(coverItem);
      }
    }
    let TotalPreview = previews.length;
    for(let pn=0;pn<TotalPreview;pn++) {
      let FTmpPath=previews[pn]['filepath'];
      modelImages.push( FTmpPath );
    }
    $('#projectGallery').bsGallery({ images: modelImages, mainHeight: 420 });
    $('#projectGallery').css("display", 'flex');
  }else {
    $('#projectGallery').css("display", 'none');
  }
  
}


function HtmlDecodeFrom3MF(strInput)
{
  const el = document.createElement('textarea');
  el.innerHTML = strInput;
  el.innerHTML = el.value;
  return el.value;
}

function ShowFileInfo( pFile ){
  let pBOM=pFile['BOM'];
	let pAssembly=pFile['Assembly'];
	let pOther=pFile['Other'];
  let BTotal=pBOM.length;
	let ATotal=pAssembly.length;
	let OTotal=pOther.length;
  let fTotal=BTotal+ATotal+OTotal;

  // if(fTotal==0){
  //   removeNavItem('#Accessories');
  //   return;
  // } else {
  //   addNavItem('#Accessories', 'Accessories');
  // }

  if (BTotal>0){
    $("#bom-accessories").text("Bill of Materials (" + BTotal + ")");
    ConstructFileHtml( "bom-list", pBOM );
  }else {
    $("#bom-accessories").text("");
    $("#bom-list").html("");
  }

  if (ATotal>0){
    $("#assembly-accessories").text("Assembly Guide (" + ATotal + ")");
    ConstructFileHtml( "assembly-list", pAssembly );
  }else {
    $("#assembly-accessories").text("");
    $("#assembly-list").html("");
  }

  if (OTotal>0){
    $("#other-accessories").text("Other (" + OTotal + ")");
    ConstructFileHtml( "other-list", pOther );
  }else {
    $("#other-accessories").text("");
    $("#other-list").html("");
  }
}

function ShowProfileInfo( pProfile )
{
  const sanitizeCfg = {
    ADD_TAGS: ['span','img','font','u','s','a'],
    ADD_ATTR: ['style','color','href','target','rel','src','alt'],
    ALLOWED_URI_REGEXP: /^(?:(?:https?|data|blob):|[^a-z]|[a-z+.\-]+(?:[^a-z+.\-:]|$))/i
  };
  let sProfileName=DOMPurify.sanitize(decodeURIComponent(pProfile.name));
	let sProfileAuthor=DOMPurify.sanitize(decodeURIComponent(pProfile.author));
	let sProfileDesc=HtmlDecodeFrom3MF(decodeURIComponent(pProfile.description));
	sProfileDesc=DOMPurify.sanitize(sProfileDesc, sanitizeCfg);
  let ModelPreviewList=pProfile.preview_img;
  let modelImages=[];
  const profileCover = normalizeCoverName(pProfile.cover_img);

  $("#profileName").text(sProfileName);
  $("#profileAuthor").text(sProfileAuthor);
  $("#profileDescript").html(sProfileDesc);
  // removeNavItem('#profileName');
  // if(sProfileName) {
  //   addNavItem('#profileName', 'Profile information');
  // }

  if(ModelPreviewList && ModelPreviewList.length > 0) {
    const previews = ModelPreviewList.slice();
    if (profileCover) {
      const coverIndex = previews.findIndex(function(item){
        if (!item || !item.filename) return false;
        const filename = normalizeCoverName(item.filename);
        return filename === profileCover;
      });
      if (coverIndex > 0) {
        const coverItem = previews.splice(coverIndex, 1)[0];
        previews.unshift(coverItem);
      }
    }
    let TotalPreview = previews.length;
    for(let pn=0;pn<TotalPreview;pn++) {
      let FTmpPath=previews[pn]['filepath'];
      modelImages.push( FTmpPath );
    }
    $('#profileGallery').bsGallery({ images: modelImages, mainHeight: 420 });
    $('#profileGallery').css("display", 'flex');
  }else {
    $('#profileGallery').css("display", 'none');
  }
}

var ExcelTail=['xlsx','xlsm','xlsb','csv','xls','xltx','xltm','xlt','xlam','xla'];
var TxtTail=['txt'];
var PdfTail=['pdf','fdf','xfdf','xdp','ppdf','ofd'];
var JpgTail=['jpg','jpeg'];
var PngTail=['png'];

function ConstructFileHtml( ID, pItem ){
  let fTotal=pItem.length;
	
	let strHtml='';
  for( let f=0;f<fTotal;f++ ){
    let pOne=pItem[f];
		
		let tPath=pOne['filepath'];
		let tName=decodeURIComponent(pOne['filename']);
		
		let sTail=getFileTail(tName).toLowerCase();

    let ImgPath='img/icon_txt.svg';
    if( $.inArray( sTail, JpgTail )>=0 ){
      ImgPath='img/icon_jpg.svg';
    }
    else if( $.inArray( sTail, PngTail )>=0 ){
      ImgPath='img/icon_png.svg';
    }
    else if( $.inArray( sTail, ExcelTail )>=0 ){
      ImgPath='img/icon_xcl.svg';
    }
    else if( $.inArray( sTail, PdfTail )>=0 ){
      ImgPath='img/icon_pdf.svg';
    }

    // base64 图片不发送外部打开指令
    let onclick = '';
    if (tPath) {
      onclick = ' onClick="OnClickOpenFile(\''+tPath+'\')"';
    }
    strHtml+='<div class="attachment"'+onclick+'><img src="'+ImgPath+'">'+tName+'</div>';
  }
  $("#"+ID).html( strHtml );
  if( fTotal>0 ) {$("#"+ID).show();}
}

function OnClickOpenFile( strFullPath )
{
  if (!strFullPath) return; 
  if(isBase64String(strFullPath)) {
    showBase64ImageLayer(strFullPath);
    return;
  }
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="open_3mf_accessory";
	tSend['accessory_path']=strFullPath;
	
	SendWXMessage( JSON.stringify(tSend) );
	SendWXDebugInfo('----open accessory:  '+strFullPath);
}

function editorBtn(){
  window.location.href = 'editor.html';
}

function JumpToWeb()
{
	if(m_ModelID=='')
		return;
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="modelmall_model_open";
	tSend['data']={};
	tSend['data']['id']=m_ModelID+'';
	
	SendWXMessage( JSON.stringify(tSend) );			
}
