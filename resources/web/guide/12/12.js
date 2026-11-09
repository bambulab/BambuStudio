// 使用条款与隐私政策页：地区页(11) -> 本页(12) -> 用户体验改进计划页(3)
var m_RegionRaw = '';

function OnInit()
{
	TranslatePage();

	let strRegion = GetQueryString('region');
	if (strRegion != null)
		m_RegionRaw = strRegion;
}

function IsChinaRegion()
{
	return m_RegionRaw.toLowerCase() === 'china';
}

function SendOpenUrl( strUrl )
{
	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "common_openurl";
	tSend['url'] = strUrl;

	SendWXMessage( JSON.stringify(tSend) );
}

function OpenTermsOfUse()
{
	if (IsChinaRegion())
		SendOpenUrl("https://bambulab.cn/zh-cn/policies/terms");
	else
		SendOpenUrl("https://bambulab.com/en/policies/terms");
	return false;
}

function OpenPrivacyPolicy()
{
	if (IsChinaRegion())
		SendOpenUrl("https://bambulab.cn/zh-cn/policies/privacy");
	else
		SendOpenUrl("https://bambulab.com/en/policies/privacy");
	return false;
}

function SendClauseChoice( strAction )
{
	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "user_clause";
	tSend['data'] = {};
	tSend['data']['action'] = strAction;

	SendWXMessage( JSON.stringify(tSend) );
}

function GotoBack()
{
	window.location.href = "../11/index.html";
}

function Decline()
{
	SendClauseChoice('refuse');
}

function Agree()
{
	SendClauseChoice('agree');

	window.location.href = "../3/index.html?region=" + encodeURIComponent(m_RegionRaw);
}
