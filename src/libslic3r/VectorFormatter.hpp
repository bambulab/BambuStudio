#pragma once

#include <vector>
#include <iostream>

// custom vector wrapper for outputting to log
template<typename Container> struct VectorFormatter
{
    const Container &vec;
    explicit VectorFormatter(const Container& v) : vec(v) {}

    friend std::ostream &operator<<(std::ostream &os, const VectorFormatter<Container> &vf)
    {
        os << "[";
        for (auto it = vf.vec.begin(); it != vf.vec.end();it++) {
            os << *it;
            if (std::next(it) != vf.vec.end()) { os << ", "; }
        }
        os << "]";
        return os;
    }
};

// custom vector wrapper for outputting to log
template<typename T1, typename T2> struct MapFormatter
{
    const std::map<T1, T2> &vec;
    explicit MapFormatter(const std::map<T1, T2> &v) : vec(v) {}

    friend std::ostream &operator<<(std::ostream &os, const MapFormatter<T1,T2> &vf)
    {
        os << "[";
        for (auto it = vf.vec.begin(); it != vf.vec.end(); it++) {
            os << it->first << " : " << it->second;
            if (std::next(it) != vf.vec.end()) { os << ", "; }
        }
        os << "]";
        return os;
    }
};
