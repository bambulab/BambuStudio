// 用户体验改进计划条款页脚本：翻译页面、关闭对话框、跳转隐私政策链接。
function OnInit()
{
	TranslatePage();
}

function CloseUxProgramTerms()
{
	// Handled by the host dialog (see UxProgramTermsDialog) via navigation intercept.
	window.location.href = "bambu://close";
}

function OpenPrivacyNotice()
{
	let url = "https://www.bambulab.com/policies/privacy";
	let region = GetQueryString('region');
	let lang = GetQueryString('lang');

	// Priority: explicit region hint from host dialog.
	if (region != null && region.toLowerCase() == 'china') {
		url = "https://www.bambulab.cn/policies/privacy";
	}
	// Fallback: if UI language is Chinese, open the CN policy site.
	else if (lang != null && lang.toLowerCase().indexOf('zh') === 0) {
		url = "https://www.bambulab.cn/policies/privacy";
	}

	// Trigger a navigation in the embedded WebView; the host dialog intercepts
	// http(s) and opens it in the system browser.
	window.location.href = url;
	return false;
}
