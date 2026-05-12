#pragma once
#include <span>
#include <string>

#include "core/bopomofo.hpp"

namespace tsf {

class IEngine {
public:
    virtual ~IEngine() {};
    virtual void predict(const std::u16string &context, std::span<BopomofoPos> padding /* in out */) = 0;
};

struct IEngineCtx {
    virtual ~IEngineCtx() {};
};

}  // namespace tsf
