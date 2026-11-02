#ifndef slic3r_AccountStore_hpp_
#define slic3r_AccountStore_hpp_

#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

struct SavedAccount
{
    std::string id;
    std::string user_id;
    std::string name;
    std::string region;
    std::string country_code;
    std::vector<unsigned char> session_data;
    std::string last_machine_id;
    bool needs_login = false;
};

class AccountStore
{
public:
    explicit AccountStore(std::string directory);

    const std::vector<SavedAccount>& accounts() const { return m_accounts; }
    std::optional<SavedAccount> find(const std::string& id) const;
    void remember(SavedAccount account);
    void remove(const std::string& id);
    void set_needs_login(const std::string& id, bool needs_login);

private:
    void save() const;

    std::string m_directory;
    std::vector<SavedAccount> m_accounts;
};

}

#endif
