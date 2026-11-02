var AccountSwitcherLabels = {
	en: {
		title: "Saved accounts",
		add: "Add account",
		current: "Current",
		switchAccount: "Switch account",
		remove: "Remove saved account",
		empty: "Accounts are saved after you sign in.",
		busy: "Updating account...",
		login: "Sign in again",
		failed: "Unable to switch account.",
		missing_account: "This saved account is no longer available.",
		clear_failed: "Unable to clear the web login session.",
		remove_current: "Log out before removing the current account.",
		login_failed: "Sign-in failed. Please try again.",
		expired: "The saved login has expired. Please sign in again.",
		privacy_declined: "Sign-in was cancelled because the privacy policy was declined.",
		unavailable: "Install the network plugin to manage accounts."
	},
	zh_CN: {
		title: "已保存账号",
		add: "添加账号",
		current: "当前账号",
		switchAccount: "切换账号",
		remove: "移除已保存账号",
		empty: "登录成功后自动保存账号。",
		busy: "正在处理账号……",
		login: "重新登录",
		failed: "账号切换失败。",
		missing_account: "所选账号已经移除。",
		clear_failed: "无法清理网页登录状态。",
		remove_current: "请退出登录后移除当前账号。",
		login_failed: "登录失败，请重新登录。",
		expired: "已保存的登录状态已失效，请重新登录。",
		privacy_declined: "尚未同意隐私协议，登录未完成。",
		unavailable: "请安装网络插件后管理账号。"
	},
	zh_TW: {
		title: "已儲存帳號",
		add: "新增帳號",
		current: "目前帳號",
		switchAccount: "切換帳號",
		remove: "移除已儲存帳號",
		empty: "登入成功後自動儲存帳號。",
		busy: "正在處理帳號……",
		login: "重新登入",
		failed: "帳號切換失敗。",
		missing_account: "所選帳號已經移除。",
		clear_failed: "無法清理網頁登入狀態。",
		remove_current: "請登出後移除目前帳號。",
		login_failed: "登入失敗，請重新登入。",
		expired: "已儲存的登入狀態已失效，請重新登入。",
		privacy_declined: "尚未同意隱私權政策，登入未完成。",
		unavailable: "請安裝網路外掛後管理帳號。"
	}
};

var AccountSwitcherBusy = false;

function GetAccountSwitcherLabels()
{
	var language = GetQueryString("lang") || localStorage.getItem(LANG_COOKIE_NAME);
	return AccountSwitcherLabels[language] || AccountSwitcherLabels.en;
}

function SendAccountCommand(command, accountId)
{
	var message = {
		sequence_id: Date.now(),
		command: command
	};
	if (accountId !== undefined)
		message.account_id = accountId;
	SendWXMessage(JSON.stringify(message));
}

function SetAccountSwitcherBusy(busy)
{
	AccountSwitcherBusy = busy;
	document.getElementById("AddAccountButton").disabled = busy || !m_HasNetworkPlugin;
	document.querySelectorAll("#SavedAccountList button").forEach(function(button) {
		button.disabled = busy || button.dataset.current === "true" || !m_HasNetworkPlugin;
	});
	var status = document.getElementById("AccountSwitcherStatus");
	var labels = GetAccountSwitcherLabels();
	if (!m_HasNetworkPlugin) {
		status.textContent = labels.unavailable;
		status.dataset.pluginUnavailable = "true";
	}
	else if (status.dataset.pluginUnavailable === "true") {
		status.textContent = busy ? labels.busy :
			(document.getElementById("SavedAccountList").childElementCount === 0 ? labels.empty : "");
		status.dataset.pluginUnavailable = "false";
	}
}

function RenderSavedAccounts(accounts)
{
	var labels = GetAccountSwitcherLabels();
	var list = document.getElementById("SavedAccountList");
	list.replaceChildren();
	accounts.forEach(function(account) {
		var row = document.createElement("div");
		row.className = "SavedAccountRow";
		row.classList.toggle("SavedAccountActive", account.active);

		var switchButton = document.createElement("button");
		switchButton.type = "button";
		switchButton.className = "SavedAccountButton";
		switchButton.dataset.current = String(account.active && !account.needs_login);
		switchButton.title = account.name + ": " + (account.active ? labels.current : labels.switchAccount);
		switchButton.setAttribute("aria-pressed", String(account.active));

		var name = document.createElement("span");
		name.className = "SavedAccountName";
		name.textContent = account.name;
		switchButton.appendChild(name);

		var state = document.createElement("span");
		state.className = "SavedAccountState";
		state.textContent = account.needs_login ? labels.login :
			(account.active ? labels.current : account.region);
		switchButton.appendChild(state);
		switchButton.addEventListener("click", function() {
			if (AccountSwitcherBusy)
				return;
			SetAccountSwitcherBusy(true);
			document.getElementById("AccountSwitcherStatus").textContent = labels.busy;
			SendAccountCommand("homepage_switch_account", account.id);
		});
		row.appendChild(switchButton);

		if (!account.active) {
			var removeButton = document.createElement("button");
			removeButton.type = "button";
			removeButton.className = "RemoveAccountButton";
			removeButton.textContent = "\u00d7";
			removeButton.title = labels.remove;
			removeButton.setAttribute("aria-label", labels.remove + ": " + account.name);
			removeButton.addEventListener("click", function() {
				if (AccountSwitcherBusy)
					return;
				SetAccountSwitcherBusy(true);
				SendAccountCommand("homepage_remove_account", account.id);
			});
			row.appendChild(removeButton);
		}
		list.appendChild(row);
	});
	document.getElementById("AccountSwitcherStatus").textContent =
		AccountSwitcherBusy ? labels.busy : (accounts.length === 0 ? labels.empty : "");
	SetAccountSwitcherBusy(AccountSwitcherBusy);
}

function HandleAccountSwitcherMessage(message)
{
	if (message.command === "homepage_accounts") {
		SetAccountSwitcherBusy(message.busy);
		RenderSavedAccounts(message.accounts);
		return true;
	}
	if (message.command === "homepage_account_result") {
		SetAccountSwitcherBusy(false);
		var labels = GetAccountSwitcherLabels();
		var status = document.getElementById("AccountSwitcherStatus");
		status.textContent =
			message.success ? "" : (labels[message.error] || labels.failed);
		status.dataset.pluginUnavailable = String(!message.success && message.error === "unavailable");
		return true;
	}
	return false;
}

function InitAccountSwitcher()
{
	var labels = GetAccountSwitcherLabels();
	document.getElementById("AccountSwitcherTitle").textContent = labels.title;
	document.getElementById("AddAccountButton").textContent = labels.add;
	document.getElementById("AddAccountButton").addEventListener("click", function() {
		if (AccountSwitcherBusy)
			return;
		SetAccountSwitcherBusy(true);
		SendAccountCommand("homepage_add_account");
	});
	document.getElementById("SavedAccountList").setAttribute("aria-label", labels.switchAccount);
	SendAccountCommand("homepage_get_accounts");
}
