var LangText={
	"en":{
		"t1":"Welcome to BambuStudio",
		"t2":"We will guide you to quickly learn to use this software. Let's start!",
		"t3":"User Agreement",
		"t4":"Reject clause",
		"t5":"Agree",
		"t6":"Help us to improve BambuStudio",
		"t7":"Allow sending anonymous data",
		"t8":"Back",
		"t9":"Next",
        "t10":"Select the type of printer you have",
		"t11":"all",
		"t12":"none",
		"t13":"mm nozzel",
		"t14":"Filament profiles select",
		"t15":"printer",
		"t16":"filament type",
		"t17":"vendor",
		"t18":"error",
		"t19":"At least one filament must be selected.",
		"t20":"Do you want to use default filament ?",
		"t21":"yes",
		"t22":"no",
		"t23":"Release note",
		"t24":"Get Started",
		"t25":"finish",
		"t26":"login",
		"t27":"register",
		"t28":"recent",
		"t29":"mall",
		"t30":"manual",
		"t31":"new project",
		"t32":"create new project",
		"t33":"open project",
		"t34":"hotspot",
		"t35":"recent open",
		"t36":"ok",
		"t37":"At least one printer must be selected.",
		"t38":"Cancel",
		"t39":"Confirm",
		"t40":"Network disconnect, please check and try again later.",
		"t41":"In the community, we learn from each other's success and failure to adjust our own slicing parameters and printing settings. BambuStudio follows the same principle and improves its performance by learning success and failure from the vast number of printings, using its sensor and  machine learning capabilities.  In other words, we could train the printer to be smarter by feeding them the real-world data, just like Tesla to train their Auto Pilot with everyone's driving data.  This service needs to access information about your error logs and usage logs, which may include information described in XXXX-Link. We will not collect your private data such as name, address, bank account, phone number. By enabling this service, you agree to these terms and the Statement about User Experience Improvement Program and privacy.",
		"t47":"Please select your login region",
		"t48":"United States",
		"t49":"China",
		"t50":"log out"
	},
	"zh_CN":{
		"t1":"欢迎使用BambuStudio",
		"t2":"我们将快速引导你配置并使用这个软件，让我们开始吧！",
		"t3":"用户使用协议",
		"t4":"拒绝",
		"t5":"同意",
		"t6":"帮助提升BambuStudio性能",
		"t7":"允许匿名发送使用数据",
		"t8":"返回",
		"t9":"下一步",
        "t10":"选择你拥有的打印机",
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
		"t41":"在社区中，通过分析每一次打印的成功和失败，从而来调整打印机的切片参数和打印设置。Bambu Studio遵循同样的原理，利用传感器和机器学习功能，从大量打印中学习成功和失败经验，从而提高打印的性能。换句话说，我们可以通过向切片算法提供真实世界的数据来训练它并让它变得更智能，就像特斯拉用每个人的驾驶数据训练自动驾驶仪一样。此服务需要访问有关错误日志和使用日志的信息，其中可能包括XXXX链接中描述的信息。我们不会收集您的姓名、地址、银行账户、电话号码等私人数据。通过启用此服务，您同意这些条款以及关于用户体验改善计划和隐私的声明。",
		"t47":"请选择你希望登录和使用的国家区域",
		"t48":"美国",
		"t49":"中国",
		"t50":"退出登录"
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