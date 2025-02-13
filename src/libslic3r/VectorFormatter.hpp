#pragma once

#include <Eigen/Core>
#include <vector>
#include <iostream>

// custom vector wrapper for outputting to log
template<typename T> struct VectorFormatter
{
    const std::vector<T>* vec = nullptr;
    const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>* mat = nullptr;
    explicit VectorFormatter(const std::vector<T>& v) : vec(&v) {}
    explicit VectorFormatter(const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& m) : mat(&m) {}

    friend std::ostream& operator<<(std::ostream& os, const VectorFormatter<T>& vf)
    {
        os << "[";
        if (vf.vec) {
            for (size_t i = 0; i < vf.vec->size(); ++i) {
                os << (*vf.vec)[i];
                if (i != vf.vec->size() - 1) { os << ", "; }
            }
        }
        else {
            for (int i = 0; i < vf.mat->size(); ++i) {
                os << (*vf.mat)(i);
                if (i != vf.mat->size() - 1) { os << ", "; }
            }
        }
        os << "]";
        return os;
    }
};
