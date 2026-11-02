#include "AccountStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <wx/secretstore.h>
#endif

namespace Slic3r {
namespace {

using json = nlohmann::json;
using Bytes = std::vector<unsigned char>;

constexpr const char* account_filename = "saved_accounts.dat";

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

#ifdef _WIN32

struct LocalMemoryDeleter
{
    void operator()(unsigned char* value) const { LocalFree(value); }
};

Bytes protect(const std::string& plaintext, const std::string&)
{
    require(plaintext.size() <= (std::numeric_limits<DWORD>::max)(), "账号数据超过支持的长度。");
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    reinterpret_cast<unsigned char*>(const_cast<char*>(plaintext.data()))};
    DATA_BLOB output{};
    require(CryptProtectData(&input, L"Bambu Studio saved accounts", nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE,
            "无法加密已保存账号。");
    std::unique_ptr<unsigned char, LocalMemoryDeleter> allocation(output.pbData);
    return Bytes(output.pbData, output.pbData + output.cbData);
}

std::string unprotect(const Bytes& ciphertext, const std::string&)
{
    require(ciphertext.size() <= (std::numeric_limits<DWORD>::max)(), "账号数据超过支持的长度。");
    DATA_BLOB input{static_cast<DWORD>(ciphertext.size()), const_cast<unsigned char*>(ciphertext.data())};
    DATA_BLOB output{};
    require(CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE,
            "无法解密已保存账号。");
    std::unique_ptr<unsigned char, LocalMemoryDeleter> allocation(output.pbData);
    return std::string(reinterpret_cast<char*>(output.pbData), output.cbData);
}

#else

Bytes account_key(const std::string& directory, bool create)
{
    auto store = wxSecretStore::GetDefault();
    require(store.IsOk(), "系统凭证存储不可用。");
    wxString user;
    wxSecretValue secret;
    const auto service = wxString::FromUTF8("BambuStudio.saved_accounts:" + directory);
    if (store.Load(service, user, secret)) {
        require(secret.GetSize() == 32, "账号加密密钥无效。");
        const auto* data = static_cast<const unsigned char*>(secret.GetData());
        return Bytes(data, data + secret.GetSize());
    }
    require(create, "账号加密密钥不可用。");
    Bytes key(32);
    require(RAND_bytes(key.data(), static_cast<int>(key.size())) == 1,
            "无法创建账号加密密钥。");
    require(store.Save(service, "accounts", wxSecretValue(key.size(), key.data())),
            "无法保存账号加密密钥。");
    return key;
}

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

Bytes protect(const std::string& plaintext, const std::string& directory)
{
    require(plaintext.size() <= std::numeric_limits<int>::max(), "账号数据超过支持的长度。");
    const auto key = account_key(directory, true);
    Bytes iv(12);
    Bytes tag(16);
    Bytes ciphertext(plaintext.size() + 16);
    require(RAND_bytes(iv.data(), static_cast<int>(iv.size())) == 1,
            "无法生成账号加密所需的随机数据。");
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(context != nullptr, "无法创建账号加密对象。");
    require(EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) == 1,
            "无法初始化账号加密。");
    int written = 0;
    require(EVP_EncryptUpdate(context.get(), ciphertext.data(), &written,
                              reinterpret_cast<const unsigned char*>(plaintext.data()),
                              static_cast<int>(plaintext.size())) == 1,
            "无法加密已保存账号。");
    int final_size = 0;
    require(EVP_EncryptFinal_ex(context.get(), ciphertext.data() + written, &final_size) == 1,
            "无法完成账号加密。");
    ciphertext.resize(written + final_size);
    require(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) == 1,
            "无法生成账号数据的完整性校验信息。");
    return json::to_cbor(json{{"version", 1}, {"iv", iv}, {"tag", tag}, {"ciphertext", ciphertext}});
}

std::string unprotect(const Bytes& data, const std::string& directory)
{
    const auto envelope = json::from_cbor(data);
    require(envelope.at("version") == 1, "不支持此账号加密格式版本。");
    const auto iv = envelope.at("iv").get<Bytes>();
    const auto tag = envelope.at("tag").get<Bytes>();
    const auto ciphertext = envelope.at("ciphertext").get<Bytes>();
    require(iv.size() == 12 && tag.size() == 16 &&
            ciphertext.size() <= std::numeric_limits<int>::max(), "已加密的账号数据无效。");
    const auto key = account_key(directory, false);
    Bytes plaintext(ciphertext.size() + 16);
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(context != nullptr, "无法创建账号加密对象。");
    require(EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) == 1,
            "无法初始化账号解密。");
    int written = 0;
    require(EVP_DecryptUpdate(context.get(), plaintext.data(), &written, ciphertext.data(),
                              static_cast<int>(ciphertext.size())) == 1,
            "无法解密已保存账号。");
    require(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()),
                                const_cast<unsigned char*>(tag.data())) == 1,
            "无法验证已保存账号。");
    int final_size = 0;
    require(EVP_DecryptFinal_ex(context.get(), plaintext.data() + written, &final_size) == 1,
            "已保存账号的完整性校验失败。");
    return std::string(reinterpret_cast<char*>(plaintext.data()), written + final_size);
}

#endif

}

AccountStore::AccountStore(std::string directory) : m_directory(std::move(directory))
{
    const auto path = std::filesystem::u8path(m_directory) / account_filename;
    if (!std::filesystem::exists(path))
        return;
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "无法打开已保存账号文件。");
    const Bytes ciphertext{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!input.bad(), "无法读取已保存账号。");
    const auto document = json::parse(unprotect(ciphertext, m_directory));
    require(document.at("version") == 1, "不支持此账号存储格式版本。");
    const auto& accounts = document.at("accounts");
    require(accounts.is_array(), "已保存账号列表必须是 JSON 数组。");
    for (const auto& item : accounts) {
        SavedAccount account;
        account.id = item.at("id").get<std::string>();
        account.user_id = item.at("user_id").get<std::string>();
        account.name = item.at("name").get<std::string>();
        account.region = item.at("region").get<std::string>();
        account.country_code = item.at("country_code").get<std::string>();
        account.session_data = item.at("session_data").get<Bytes>();
        account.last_machine_id = item.at("last_machine_id").get<std::string>();
        account.needs_login = item.at("needs_login").get<bool>();
        require(!account.id.empty() && !account.user_id.empty() && !account.session_data.empty(),
                "已保存的账号数据不完整。");
        require(!find(account.id).has_value(), "已保存账号的标识重复。");
        m_accounts.emplace_back(std::move(account));
    }
}

std::optional<SavedAccount> AccountStore::find(const std::string& id) const
{
    const auto item = std::find_if(m_accounts.begin(), m_accounts.end(),
                                   [&id](const SavedAccount& account) { return account.id == id; });
    if (item == m_accounts.end())
        return std::nullopt;
    return *item;
}

void AccountStore::remember(SavedAccount account)
{
    require(!account.id.empty() && !account.user_id.empty() && !account.session_data.empty(),
            "账号数据不完整。");
    const auto item = std::find_if(m_accounts.begin(), m_accounts.end(),
                                   [&account](const SavedAccount& saved) { return saved.id == account.id; });
    if (item == m_accounts.end())
        m_accounts.emplace_back(std::move(account));
    else {
        if (item->user_id == account.user_id && item->name == account.name &&
            item->region == account.region && item->country_code == account.country_code &&
            item->session_data == account.session_data && item->last_machine_id == account.last_machine_id &&
            item->needs_login == account.needs_login)
            return;
        *item = std::move(account);
    }
    save();
}

void AccountStore::remove(const std::string& id)
{
    const auto item = std::find_if(m_accounts.begin(), m_accounts.end(),
                                   [&id](const SavedAccount& account) { return account.id == id; });
    require(item != m_accounts.end(), "已保存账号不存在。");
    m_accounts.erase(item);
    save();
}

void AccountStore::set_needs_login(const std::string& id, bool needs_login)
{
    const auto item = std::find_if(m_accounts.begin(), m_accounts.end(),
                                   [&id](const SavedAccount& account) { return account.id == id; });
    require(item != m_accounts.end(), "已保存账号不存在。");
    if (item->needs_login == needs_login)
        return;
    item->needs_login = needs_login;
    save();
}

void AccountStore::save() const
{
    json document{{"version", 1}, {"accounts", json::array()}};
    for (const auto& account : m_accounts) {
        document["accounts"].push_back({
            {"id", account.id}, {"user_id", account.user_id}, {"name", account.name},
            {"region", account.region}, {"country_code", account.country_code},
            {"session_data", account.session_data}, {"last_machine_id", account.last_machine_id},
            {"needs_login", account.needs_login}
        });
    }
    const auto ciphertext = protect(document.dump(), m_directory);
    const auto path = std::filesystem::u8path(m_directory) / account_filename;
    auto pending = path;
    pending += ".pending";
    {
        std::ofstream output(pending, std::ios::binary | std::ios::trunc);
        require(output.is_open(), "无法创建账号存储文件。");
        output.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext.size());
        output.flush();
        require(output.good(), "无法写入已保存账号。");
        output.close();
        require(output.good(), "无法关闭账号存储文件。");
    }
#ifdef _WIN32
    require(MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE,
            "无法替换账号存储文件。");
#else
    std::filesystem::permissions(pending, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
    std::filesystem::rename(pending, path);
#endif
}

}
