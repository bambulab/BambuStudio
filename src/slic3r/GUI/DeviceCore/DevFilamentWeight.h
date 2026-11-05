#pragma once

#include <string>

#include "slic3r/GUI/I18N.hpp"

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
    static constexpr int MAX_RFID_REMAIN_GRAMS = 1000;

    static DevFilamentWeightValidation Validate(int total, int remain)
    {
        if (remain > total) {
            return { false, _u8L("The total weight must bigger than remain weight.") };
        }

        if (remain < MIN_TOTAL_GRAMS || remain > MAX_TOTAL_GRAMS) {
            return { false, _u8L("Please enter a valid remaining weight from 1g to 10000g.") };
        }

        if (total < MIN_TOTAL_GRAMS || total > MAX_TOTAL_GRAMS) {
            return { false, _u8L("Please enter a valid total weight from 1g to 10000g.") };
        }

        return { true, "" };
    }
};

} // namespace Slic3r
