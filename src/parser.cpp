#include "cubelut/parser.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iostream>

namespace cubelut {

static std::string_view trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::optional<Lut> Parser::fromFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return fromString(content);
}

std::optional<Lut> Parser::fromString(std::string_view content) {
    Lut lut;
    std::stringstream ss{std::string(content)};
    std::string line;
    
    bool sizeDefined = false;
    bool dataStarted = false;
    
    std::vector<float> values;

    while (std::getline(ss, line)) {
        std::string_view sv = trim(line);
        if (sv.empty() || sv[0] == '#') continue;

        if (!dataStarted) {
            if (sv.substr(0, 6) == "TITLE ") {
                lut.title = std::string(trim(sv.substr(6)));
                // Strip quotes if present
                if (lut.title.size() >= 2 && lut.title.front() == '"' && lut.title.back() == '"') {
                    lut.title = lut.title.substr(1, lut.title.size() - 2);
                }
                continue;
            }

            if (sv.substr(0, 12) == "LUT_1D_SIZE ") {
                lut.type = LutType::Lut1D;
                lut.size = std::stoi(std::string(sv.substr(12)));
                sizeDefined = true;
                continue;
            }

            if (sv.substr(0, 12) == "LUT_3D_SIZE ") {
                lut.type = LutType::Lut3D;
                lut.size = std::stoi(std::string(sv.substr(12)));
                sizeDefined = true;
                continue;
            }

            if (sv.substr(0, 11) == "DOMAIN_MIN ") {
                std::stringstream line_ss{std::string(sv.substr(11))};
                line_ss >> lut.domainMin[0] >> lut.domainMin[1] >> lut.domainMin[2];
                continue;
            }

            if (sv.substr(0, 11) == "DOMAIN_MAX ") {
                std::stringstream line_ss{std::string(sv.substr(11))};
                line_ss >> lut.domainMax[0] >> lut.domainMax[1] >> lut.domainMax[2];
                continue;
            }

            // If we encounter a number, it's the start of the data
            if (std::isdigit(sv[0]) || sv[0] == '-' || sv[0] == '.') {
                dataStarted = true;
            }
        }

        if (dataStarted) {
            std::stringstream line_ss{std::string(sv)};
            float r, g, b;
            if (line_ss >> r >> g >> b) {
                values.push_back(r);
                values.push_back(g);
                values.push_back(b);
            }
        }
    }

    if (!sizeDefined) return std::nullopt;
    
    lut.data = std::move(values);
    
    if (lut.isValid()) {
        return lut;
    } else {
        return std::nullopt;
    }
}

} // namespace cubelut
