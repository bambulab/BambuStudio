

function IsInSlicer()
{
	let bMatch=navigator.userAgent.match(  RegExp('BBL-Slicer','i') );
	
	return bMatch;
}



function SendWXMessage( strMsg )
{
	let bCheck=IsInSlicer();
	
	if(bCheck!=null)
	{
		window.wx.postMessage(strMsg);
	}
}


function OnInit()
{

}


window.addEventListener('message', event => {
	//console.log(event);
	
    //alert(event.data);
})
