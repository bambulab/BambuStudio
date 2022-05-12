var LangText={
	"en":{
		"t1":"Welcome to Bambu Studio",
		"t2":"Bambu Studio will be setup in several steps. Let's start!",
		"t3":"User Agreement",
		"t4":"Disagree",
		"t5":"Agree",
		"t6":"Help us to improve Bambu Studio",
		"t7":"Allow sending anonymous data",
		"t8":"Back",
		"t9":"Next",
        "t10":"Printer Selection",
		"t11":"All",
		"t12":"None",
		"t13":"mm nozzel",
		"t14":"Filament Selection",
		"t15":"printer",
		"t16":"Filament type",
		"t17":"Vendor",
		"t18":"error",
		"t19":"At least one filament must be selected.",
		"t20":"Do you want to use default filament ?",
		"t21":"yes",
		"t22":"no",
		"t23":"Release note",
		"t24":"Get Started",
		"t25":"Finish",
		"t26":"Login",
		"t27":"Register",
		"t28":"Recent",
		"t29":"Mall",
		"t30":"Manual",
		"t31":"New project",
		"t32":"Create new project",
		"t33":"Open project",
		"t34":"hotspot",
		"t35":"Recently opened",
		"t36":"ok",
		"t37":"At least one printer must be selected.",
		"t38":"Cancel",
		"t39":"Confirm",
		"t40":"Network disconnect, please check and try again later.",
		"t47":"Please select your login region",
		"t48":"Asia-Pacific",
		"t49":"China",
		"t50":"Log out",
		"t52":"Skip",
		"t53":"Join",
		"t54":"In the 3D Printing community, we learn from each other's successes and failures to adjust our own slicing parameters and settings. Bambu Studio follows the same principle and uses machine learning to improve its performance from the successes and failures of the vast number of prints by our users. We are training Bambu Studio to be more smarter by feeding them the real-world data. If you are willing, this service will access information from your error logs and usage logs, which may include information described in",
		"t55":"Privacy Policy",
		"t56":".We will not collect any private data such as names, addresses, payment information, or phone numbers. By enabling this service, you agree to these terms and the statement about Privacy Policy",
		"t57":"",
		"t58":"",
		"t59":".",
		"t60":"Europe",
		"t61":"North America",
		"t62":"Others",
		"t63":"After changing the region, your account will be logged out. Please log in again later."
	},
	"zh_CN":{
		"t1":"欢迎使用Bambu Studio",
		"t2":"Bambu Studio需要几步安装步骤，让我们开始吧！",
		"t3":"用户使用协议",
		"t4":"拒绝",
		"t5":"同意",
		"t6":"帮助提升Bambu Studio性能",
		"t7":"允许发送匿名数据",
		"t8":"上一步",
		"t9":"下一步",
        "t10":"选择打印机",
		"t11":"全部",
		"t12":"无",
		"t13":"mm 喷嘴",
		"t14":"选择材料",
		"t15":"打印机",
		"t16":"材料类型",
		"t17":"供应商",
		"t18":"错误",
		"t19":"至少要选择一款材料。",
		"t20":"你希望使用默认的材料列表吗?",
		"t21":"是",
		"t22":"否",
		"t23":"发布说明",
		"t24":"开始",
		"t25":"结束",
		"t26":"登录",
		"t27":"注册",
		"t28":"近期",
		"t29":"商城",
		"t30":"使用手册",
		"t31":"新建项目",
		"t32":"创建一个新项目",
		"t33":"打开项目",
		"t34":"热点",
		"t35":"近期打开文件",
		"t36":"确定",
		"t37":"至少需要选择一款打印机。",
		"t38":"取消",
		"t39":"确定",
		"t40":"网络不通，请检查并稍后重试。",
		"t47":"请选择登录区域",
		"t48":"亚太",
		"t49":"中国",
		"t50":"退出登录",
		"t52":"忽略",
		"t53":"同意",
		"t54":"在社区中，我们从每一次成功或失败的经历中吸取经验，不断优化切片参数和打印设置。Bambu Studio遵循同样的原则，通过使用机器学习算法，从大量打印的成功与失败中不断总结学习，从而提高打印机的性能。换句话说，我们可以通过向Bambu Studio提供真实世界的数据来训练它并让它变得更聪明，就像特斯拉用每个人的驾驶数据训练他们的自动驾驶仪一样。此服务需要访问有关错误日志和使用日志的信息，详细信息描述可以查看",
		"t55":"隐私策略",
		"t56":"。我们不会搜集你的个人隐私数据，包括姓名、地址、银行账号、电话号码。启用此服务即表示您同意这些条款和声明：",
		"t57":"用户体验改善计划",
		"t58":"和",
		"t59":"。",
		"t60":"欧洲",
		"t61":"北美",
		"t62":"其他",
		"t63":"切换区域后，你的账号会被登出。稍后请重新登录。"
	}
};


var LANG_COOKIE_NAME="BambuWebLang";
var LANG_COOKIE_EXPIRESECOND= 365*86400;

function TranslatePage()
{
	let strLang=GetQueryString("lang");
	if(strLang!=null)
	{
		//setCookie(LANG_COOKIE_NAME,strLang,LANG_COOKIE_EXPIRESECOND,'/');
		localStorage.setItem(LANG_COOKIE_NAME,strLang);
	}
	else
	{
		//strLang=getCookie(LANG_COOKIE_NAME);
		strLang=localStorage.getItem(LANG_COOKIE_NAME);
	}
	
	//alert(strLang);
	
	if( !LangText.hasOwnProperty(strLang) )
		strLang="en";
	
    let AllNode=$(".trans");
	let nTotal=AllNode.length;
	for(let n=0;n<nTotal;n++)
	{
		let OneNode=AllNode[n];
		
		let tid=$(OneNode).attr("tid");
		if( LangText[strLang].hasOwnProperty(tid) )
		{
			if($(OneNode).is('input'))
			{
				$(OneNode).html(LangText[strLang][tid]);
			}
			else
				$(OneNode).text( LangText[strLang][tid] );
		}
	}
}
