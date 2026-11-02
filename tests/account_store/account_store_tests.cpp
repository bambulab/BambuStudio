#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <nlohmann/json.hpp>

#include "AccountStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;
using json = nlohmann::json;
using Slic3r::AccountStore;
using Slic3r::SavedAccount;

constexpr const char* account_filename = "saved_accounts.dat";

struct LocalMemoryDeleter
{
    void operator()(unsigned char* value) const
    {
        if (value != nullptr)
            LocalFree(value);
    }
};

Bytes random_bytes(std::size_t size)
{
    Bytes result(size);
    if (!result.empty()) {
        const auto status = BCryptGenRandom(nullptr, result.data(), static_cast<ULONG>(result.size()),
                                           BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status != 0)
            throw std::runtime_error("无法生成随机数据。");
    }
    return result;
}

std::string random_hex(std::size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    const auto bytes = random_bytes(size);
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto value : bytes) {
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

Bytes protect_with_dpapi(const Bytes& plaintext)
{
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    const_cast<unsigned char*>(plaintext.data())};
    DATA_BLOB output{};
    REQUIRE(CryptProtectData(&input, L"AccountStore test", nullptr, nullptr, nullptr,
                             CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE);
    std::unique_ptr<unsigned char, LocalMemoryDeleter> allocation(output.pbData);
    return Bytes(output.pbData, output.pbData + output.cbData);
}

std::string unprotect_with_dpapi(const Bytes& ciphertext)
{
    DATA_BLOB input{static_cast<DWORD>(ciphertext.size()),
                    const_cast<unsigned char*>(ciphertext.data())};
    DATA_BLOB output{};
    REQUIRE(CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE);
    std::unique_ptr<unsigned char, LocalMemoryDeleter> allocation(output.pbData);
    return std::string(reinterpret_cast<const char*>(output.pbData), output.cbData);
}

Bytes read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
        throw std::runtime_error("无法打开测试输入文件。");
    return Bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const Bytes& contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        throw std::runtime_error("无法打开测试输出文件。");
    output.write(reinterpret_cast<const char*>(contents.data()), contents.size());
    if (!output.good())
        throw std::runtime_error("无法写入测试输出文件。");
}

std::filesystem::path test_root()
{
    const auto current = std::filesystem::weakly_canonical(std::filesystem::current_path());
    const auto allowed = std::any_of(current.begin(), current.end(), [](const auto& component) {
        const auto name = component.string();
        return name == ".cache" || name.rfind("build-account-store", 0) == 0;
    });
    if (!allowed)
        throw std::runtime_error("测试必须从工作目录内的 .cache 或 build-account-store 目录运行。");
    return current / "account_store_test_data";
}

class TemporaryDirectory
{
public:
    explicit TemporaryDirectory(const std::string& label)
    {
        m_root = std::filesystem::weakly_canonical(test_root());
        std::filesystem::create_directories(m_root);
        m_root = std::filesystem::weakly_canonical(m_root);
        m_path = m_root / (label + "-" + random_hex(8));
        std::filesystem::create_directories(m_path);
        m_path = std::filesystem::weakly_canonical(m_path);
    }

    const std::filesystem::path& path() const { return m_path; }

    void cleanup() const
    {
        const auto root = std::filesystem::weakly_canonical(m_root);
        const auto target = std::filesystem::weakly_canonical(m_path);
        const auto relative = target.lexically_relative(root);
        const auto component_count = std::distance(relative.begin(), relative.end());
        if (target == root || relative.empty() || component_count != 1 ||
            *relative.begin() == "..")
            throw std::runtime_error("拒绝清理测试数据目录之外的路径。");
        std::filesystem::remove_all(target);
    }

private:
    std::filesystem::path m_root;
    std::filesystem::path m_path;
};

SavedAccount make_account()
{
    const auto suffix = random_hex(48);
    return SavedAccount{
        "account-" + suffix,
        "user-" + suffix,
        "printer-" + suffix,
        "region-" + random_hex(4),
        "country-" + random_hex(3),
        random_bytes(128),
        "machine-" + random_hex(16),
        false
    };
}

bool contains_bytes(const Bytes& contents, const Bytes& text)
{
    return std::search(contents.begin(), contents.end(), text.begin(), text.end()) != contents.end();
}

void check_account(const SavedAccount& expected, const SavedAccount& actual)
{
    CHECK(actual.id == expected.id);
    CHECK(actual.user_id == expected.user_id);
    CHECK(actual.name == expected.name);
    CHECK(actual.region == expected.region);
    CHECK(actual.country_code == expected.country_code);
    CHECK(actual.session_data == expected.session_data);
    CHECK(actual.last_machine_id == expected.last_machine_id);
    CHECK(actual.needs_login == expected.needs_login);
}

void check_serialized_account(const json& item, const SavedAccount& expected)
{
    CHECK(item.at("id").get<std::string>() == expected.id);
    CHECK(item.at("user_id").get<std::string>() == expected.user_id);
    CHECK(item.at("name").get<std::string>() == expected.name);
    CHECK(item.at("region").get<std::string>() == expected.region);
    CHECK(item.at("country_code").get<std::string>() == expected.country_code);
    CHECK(item.at("session_data").get<Bytes>() == expected.session_data);
    CHECK(item.at("last_machine_id").get<std::string>() == expected.last_machine_id);
    CHECK(item.at("needs_login").get<bool>() == expected.needs_login);
}

void write_protected_document(const std::filesystem::path& directory, const json& document)
{
    const auto plaintext = document.dump();
    const Bytes bytes(plaintext.begin(), plaintext.end());
    write_file(directory / account_filename, protect_with_dpapi(bytes));
}

}

TEST_CASE("Windows AccountStore 使用 DPAPI 保存随机数据",
          "[account_store][windows][dpapi]")
{
    TemporaryDirectory temporary("roundtrip");
    const auto first = make_account();
    const auto second = make_account();

    AccountStore store(temporary.path().string());
    store.remember(first);
    store.remember(second);
    REQUIRE(store.accounts().size() == 2);

    const auto saved_path = temporary.path() / account_filename;
    REQUIRE(std::filesystem::exists(saved_path));
    const auto encrypted = read_file(saved_path);
    REQUIRE_FALSE(encrypted.empty());
    CHECK_FALSE(contains_bytes(encrypted, first.session_data));
    CHECK_FALSE(contains_bytes(encrypted, second.session_data));
    store.remember(first);
    REQUIRE(store.accounts().size() == 2);
    CHECK(read_file(saved_path) == encrypted);

    const auto decrypted = unprotect_with_dpapi(encrypted);
    const auto document = json::parse(decrypted);
    REQUIRE(document.at("version") == 1);
    REQUIRE(document.at("accounts").size() == 2);
    check_serialized_account(document.at("accounts").at(0), first);
    check_serialized_account(document.at("accounts").at(1), second);

    AccountStore loaded(temporary.path().string());
    REQUIRE(loaded.accounts().size() == 2);
    const auto loaded_first = loaded.find(first.id);
    REQUIRE(loaded_first.has_value());
    check_account(first, *loaded_first);

    auto updated = first;
    updated.name = "renamed-" + random_hex(12);
    updated.session_data = random_bytes(160);
    loaded.remember(updated);
    REQUIRE(loaded.accounts().size() == 2);
    loaded.set_needs_login(updated.id, true);

    AccountStore reloaded(temporary.path().string());
    const auto reloaded_first = reloaded.find(updated.id);
    REQUIRE(reloaded_first.has_value());
    CHECK(reloaded_first->name == updated.name);
    CHECK(reloaded_first->session_data == updated.session_data);
    CHECK(reloaded_first->needs_login);
    CHECK_FALSE(std::filesystem::exists(saved_path.string() + ".pending"));

    temporary.cleanup();
}

TEST_CASE("Windows AccountStore 从受保护文件移除账号",
          "[account_store][windows][dpapi]")
{
    TemporaryDirectory temporary("remove");
    const auto first = make_account();
    const auto second = make_account();
    const auto saved_path = temporary.path() / account_filename;

    AccountStore store(temporary.path().string());
    store.remember(first);
    store.remember(second);
    store.remove(first.id);

    REQUIRE_FALSE(store.find(first.id).has_value());
    REQUIRE(store.find(second.id).has_value());
    REQUIRE_THROWS_WITH(store.remove(first.id), "已保存账号不存在。");

    AccountStore after_first_remove(temporary.path().string());
    REQUIRE(after_first_remove.accounts().size() == 1);
    CHECK(after_first_remove.accounts().front().id == second.id);

    after_first_remove.remove(second.id);
    REQUIRE(after_first_remove.accounts().empty());
    REQUIRE(std::filesystem::exists(saved_path));
    const auto document = json::parse(unprotect_with_dpapi(read_file(saved_path)));
    REQUIRE(document.at("version") == 1);
    REQUIRE(document.at("accounts").empty());

    AccountStore empty(temporary.path().string());
    CHECK(empty.accounts().empty());

    temporary.cleanup();
}

TEST_CASE("Windows AccountStore 拒绝随机损坏和截断文件",
          "[account_store][windows][dpapi][corruption]")
{
    TemporaryDirectory random_directory("random-corruption");
    write_file(random_directory.path() / account_filename, random_bytes(256));
    CHECK_THROWS_WITH(AccountStore(random_directory.path().string()),
                      "无法解密已保存账号。");
    random_directory.cleanup();

    TemporaryDirectory truncated_directory("truncated-corruption");
    AccountStore source(truncated_directory.path().string());
    source.remember(make_account());
    const auto valid_path = truncated_directory.path() / account_filename;
    auto truncated = read_file(valid_path);
    truncated.resize(truncated.size() / 2);
    write_file(valid_path, truncated);
    CHECK_THROWS_WITH(AccountStore(truncated_directory.path().string()),
                      "无法解密已保存账号。");
    truncated_directory.cleanup();
}

TEST_CASE("Windows AccountStore 报告受保护文档内容错误",
          "[account_store][windows][dpapi][corruption]")
{
    TemporaryDirectory version_directory("invalid-version");
    write_protected_document(version_directory.path(),
                             json{{"version", 2}, {"accounts", json::array()}});
    CHECK_THROWS_WITH(AccountStore(version_directory.path().string()),
                      "不支持此账号存储格式版本。");
    version_directory.cleanup();

    TemporaryDirectory duplicate_directory("duplicate-id");
    const auto account = make_account();
    const json serialized_account{
        {"id", account.id},
        {"user_id", account.user_id},
        {"name", account.name},
        {"region", account.region},
        {"country_code", account.country_code},
        {"session_data", account.session_data},
        {"last_machine_id", account.last_machine_id},
        {"needs_login", account.needs_login}
    };
    write_protected_document(duplicate_directory.path(),
                             json{{"version", 1},
                                  {"accounts", json::array({serialized_account, serialized_account})}});
    CHECK_THROWS_WITH(AccountStore(duplicate_directory.path().string()),
                      "已保存账号的标识重复。");
    duplicate_directory.cleanup();

    TemporaryDirectory invalid_container_directory("invalid-accounts-container");
    write_protected_document(invalid_container_directory.path(),
                             json{{"version", 1}, {"accounts", nullptr}});
    CHECK_THROWS_WITH(AccountStore(invalid_container_directory.path().string()),
                      "已保存账号列表必须是 JSON 数组。");
    invalid_container_directory.cleanup();

    TemporaryDirectory incomplete_directory("incomplete-account");
    write_protected_document(incomplete_directory.path(),
                             json{{"version", 1},
                                  {"accounts", json::array({{
                                      {"id", random_hex(8)},
                                      {"user_id", random_hex(8)},
                                      {"session_data", random_bytes(32)}
                                  }})}});
    CHECK_THROWS(AccountStore(incomplete_directory.path().string()));
    incomplete_directory.cleanup();
}
