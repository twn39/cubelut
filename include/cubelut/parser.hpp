#pragma once

#include <string>
#include <string_view>
#include <optional>
#include "lut.hpp"

namespace cubelut {

class Parser {
public:
    static std::optional<Lut> fromFile(const std::string& filePath);
    static std::optional<Lut> fromString(std::string_view content);
};

} // namespace cubelut
