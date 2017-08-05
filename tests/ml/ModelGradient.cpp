#include <ModelGradient.h>
#include <iostream>

LRGradient::LRGradient(int d) {
    weights.resize(d);
}

LRGradient::LRGradient(const std::vector<double>& data) :
    weights(data) {
}

void LRGradient::loadSerialized(const void* mem) {
    const double* data = reinterpret_cast<const double*>(mem);
    std::copy(data, data + weights.size(), weights.end());
}

void LRGradient::serialize(void* mem) const {
    double* data = reinterpret_cast<double*>(mem);
    std::copy(weights.begin(), weights.end(), data);
}

uint64_t LRGradient::getSerializedSize() const {
    return weights.size() * sizeof(double);
}

void LRGradient::print() const {
    std::cout << "LRGradient: ";
    for (const auto &v : weights) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
}

SoftmaxGradient::SoftmaxGradient(uint64_t nclasses, uint64_t d) {
    weights.resize(d);
    for (auto& v : weights) {
        v.resize(nclasses);
    }
}

SoftmaxGradient::SoftmaxGradient(const std::vector<std::vector<double>>& w) {
    weights = w;
}

void SoftmaxGradient::serialize(void* mem) const {
    double* data = reinterpret_cast<double*>(mem);

    for (const auto& v : weights) {
        std::copy(v.begin(), v.end(), data);
        data += v.size();
    }
}

uint64_t SoftmaxGradient::getSerializedSize() const {
    return weights.size() * weights[0].size() * sizeof(double);
}

void SoftmaxGradient::loadSerialized(const void* mem) {
    const double* data = reinterpret_cast<const double*>(mem);

    for (auto& v : weights) {
        std::copy(data, data + v.size(), v.begin());
        data += v.size();
    }
}

void SoftmaxGradient::print() const {
    std::cout
        << "SoftmaxGradient (" << weights.size() << "x"
        << weights[0].size() << "): " << std::endl;
    for (const auto &v : weights) {
        for (const auto &vv : v) {
            std::cout << vv << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}
