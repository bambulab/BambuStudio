#pragma once

#include <string>

namespace Slic3r {

struct DevFilamentWeightValidation
{
    bool valid = false;
    std::string message;
};

class DevFilamentWeight
{
public:
    static constexpr int MIN_TOTAL_GRAMS = 1;
    static constexpr int MAX_TOTAL_GRAMS = 10000;

    static DevFilamentWeightValidation Validate(int total, int remain)
    {
        if (total < MIN_TOTAL_GRAMS || total > MAX_TOTAL_GRAMS) {
            return { false, "Please enter a valid total weight (1-10000 g)." };
        }
        if (remain < MIN_TOTAL_GRAMS || remain > total) {
            return { false, "Please enter a valid remaining weight (1-total weight g)." };
        }
        return { true, "" };
    }
};

} // namespace Slic3r
