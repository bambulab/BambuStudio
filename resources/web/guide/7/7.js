function OnInit()
{
	TranslatePage();

	if (LangStringForAcademy() == 'zh') {
		document.getElementById('AcademyBanner').src = 'academy_banner_zh.png';
	}
}

function FinishGuide()
{
	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "user_guide_finish";
	tSend['data'] = {};
	tSend['data']['action'] = "finish";

	SendWXMessage(JSON.stringify(tSend));
}

// Mirrors homepage3/js/wiki.js's openAcademyUrl(): same cn/com domain split
// and per-language path, so the Setup Wizard's academy link lands on the same
// pages as the homepage "Learning" entry.
function LangStringForAcademy()
{
	let strLang = GetQueryString("lang");
	if (strLang == null)
		strLang = localStorage.getItem(LANG_COOKIE_NAME);
	if (!strLang)
		return 'en';

	if (strLang.includes('zh')) return 'zh';
	else if (strLang.includes('en')) return 'en';
	else if (strLang.includes('fr')) return 'fr-fr';
	else if (strLang.includes('de')) return 'de-de';
	else if (strLang.includes('es')) return 'es-mx';
	else if (strLang.includes('it')) return 'it-it';
	else if (strLang.includes('ja')) return 'ja-jp';
	else if (strLang.includes('ko')) return 'ko-kr';
	else if (strLang.includes('pt')) return 'pt-br';
	else if (strLang.includes('nl')) return 'nl-nl';
	else return 'en';
}

function OpenAcademyUrl()
{
	let strLang = LangStringForAcademy();
	let openUrl = (strLang == 'zh')
		? "https://bambulab.cn/zh-cn/support/academy/"
		: "https://bambulab.com/" + strLang + "/support/academy/";

	// Distinct from common_openurl so the native side can track this specific
	// click (Setup Wizard completion page -> Bambu Studio Academy).
	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "user_guide_academy_click";
	tSend['url'] = openUrl;

	SendWXMessage(JSON.stringify(tSend));
}
