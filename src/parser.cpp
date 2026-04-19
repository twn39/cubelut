#include "cubelut/parser.hpp"
#include <fstream>
#include <vector>
#include <iostream>
#include "fast_float/fast_float.h"

namespace cubelut {

std::optional<Lut> Parser::fromFile(const std::string& filePath) {
    // Read the entire file into memory once.
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::string content(size, '\0');
    if (file.read(&content[0], size)) {
        return fromString(content);
    }
    
    return std::nullopt;
}

std::optional<Lut> Parser::fromString(std::string_view content) {
    Lut lut;
    const char* p = content.data();
    const char* end = p + content.size();
    
    Domain current_domain;
    
    int expected_1d_items = 0;
    int expected_3d_items = 0;
    int read_1d_items = 0;
    int read_3d_items = 0;

    auto skip_whitespace = [&]() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
            p++;
        }
    };

    auto skip_line = [&]() {
        while (p < end && *p != '\n') {
            p++;
        }
        if (p < end) p++; // skip '\n'
    };

    auto match_prefix = [&](const char* prefix, size_t len) -> bool {
        if (static_cast<size_t>(end - p) >= len && std::string_view(p, len) == prefix) {
            p += len;
            return true;
        }
        return false;
    };

    while (p < end) {
        skip_whitespace();
        if (p == end) break;

        if (*p == '\n') {
            p++;
            continue;
        }
        if (*p == '#') {
            skip_line();
            continue;
        }

        // Data section usually starts with a number, minus, or dot
        if (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9')) {
            float r, g, b;
            auto resR = fast_float::from_chars(p, end, r);
            p = resR.ptr; skip_whitespace();
            
            auto resG = fast_float::from_chars(p, end, g);
            p = resG.ptr; skip_whitespace();
            
            auto resB = fast_float::from_chars(p, end, b);
            p = resB.ptr;

            if (resR.ec == std::errc() && resG.ec == std::errc() && resB.ec == std::errc()) {
                if (lut.shaper1D.has_value() && read_1d_items < expected_1d_items) {
                    lut.shaper1D->data.push_back(r);
                    lut.shaper1D->data.push_back(g);
                    lut.shaper1D->data.push_back(b);
                    read_1d_items += 3;
                } else if (lut.grid3D.has_value() && read_3d_items < expected_3d_items) {
                    lut.grid3D->data.push_back(r);
                    lut.grid3D->data.push_back(g);
                    lut.grid3D->data.push_back(b);
                    read_3d_items += 3;
                }
            }
            skip_line();
            continue;
        }

        if (match_prefix("TITLE ", 6)) {
            skip_whitespace();
            const char* start = p;
            while (p < end && *p != '\n' && *p != '\r') p++;
            lut.title = std::string(start, p - start);
            if (lut.title.size() >= 2 && lut.title.front() == '"' && lut.title.back() == '"') {
                lut.title = lut.title.substr(1, lut.title.size() - 2);
            }
            skip_line();
            continue;
        }

        if (match_prefix("LUT_1D_SIZE ", 12)) {
            skip_whitespace();
            int size;
            auto res = fast_float::from_chars(p, end, size);
            if (res.ec == std::errc()) {
                LutData1D d1;
                d1.size = size;
                d1.domain = current_domain;
                expected_1d_items = size * 3;
                d1.data.reserve(expected_1d_items);
                lut.shaper1D = std::move(d1);
                
                current_domain = Domain{}; // reset domain
                p = res.ptr;
            }
            skip_line();
            continue;
        }

        if (match_prefix("LUT_3D_SIZE ", 12)) {
            skip_whitespace();
            int size;
            auto res = fast_float::from_chars(p, end, size);
            if (res.ec == std::errc()) {
                LutData3D d3;
                d3.size = size;
                d3.domain = current_domain;
                expected_3d_items = size * size * size * 3;
                d3.data.reserve(expected_3d_items);
                lut.grid3D = std::move(d3);
                
                current_domain = Domain{}; // reset domain
                p = res.ptr;
            }
            skip_line();
            continue;
        }

        if (match_prefix("DOMAIN_MIN ", 11)) {
            skip_whitespace();
            for (int i = 0; i < 3; ++i) {
                auto res = fast_float::from_chars(p, end, current_domain.min[i]);
                p = res.ptr;
                skip_whitespace();
            }
            skip_line();
            continue;
        }

        if (match_prefix("DOMAIN_MAX ", 11)) {
            skip_whitespace();
            for (int i = 0; i < 3; ++i) {
                auto res = fast_float::from_chars(p, end, current_domain.max[i]);
                p = res.ptr;
                skip_whitespace();
            }
            skip_line();
            continue;
        }

        // Unknown line or unsupported keyword, safely skip it
        skip_line();
    }

    if (!lut.isValid()) {
        return std::nullopt;
    }
    
    return lut;
}

} // namespace cubelut
