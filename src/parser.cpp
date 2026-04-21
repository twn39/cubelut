#include "cubelut/parser.hpp"
#include <fstream>
#include <charconv>  // std::from_chars for integer parsing
#include "fast_float/fast_float.h"

namespace cubelut {

// ---------------------------------------------------------------------------
// FEAT-1: human-readable error descriptions
// ---------------------------------------------------------------------------
const char* parseErrorToString(ParseError e) noexcept {
    switch (e) {
        case ParseError::None:                    return "No error";
        case ParseError::FileNotFound:            return "File not found or cannot be opened";
        case ParseError::FileReadError:           return "File could not be fully read";
        case ParseError::MissingLutSizeDirective: return "No LUT_1D_SIZE or LUT_3D_SIZE directive found";
        case ParseError::InvalidLutSize:          return "LUT size value is out of range or malformed";
        case ParseError::InvalidDomain:           return "DOMAIN_MIN, DOMAIN_MAX, or INPUT_RANGE contains unparseable values";
        case ParseError::InsufficientData:        return "Fewer data triplets than the declared LUT size requires";
    }
    return "Unknown error";
}

// ---------------------------------------------------------------------------
// Core parse implementation – returns a full ParseResult.
// FEAT-3: now also handles LUT_1D_INPUT_RANGE and LUT_3D_INPUT_RANGE.
// ---------------------------------------------------------------------------
ParseResult Parser::parseString(std::string_view content) {
    ParseResult result;
    Lut lut;
    const char* p   = content.data();
    const char* end = p + content.size();

    Domain current_domain;

    int expected_1d_items = 0;
    int expected_3d_items = 0;
    int read_1d_items     = 0;
    int read_3d_items     = 0;
    bool saw_any_size     = false;

    // ------- helpers (lambdas) -----------------------------------------------

    auto skip_whitespace = [&]() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
    };

    auto skip_line = [&]() {
        while (p < end && *p != '\n') p++;
        if (p < end) p++; // consume '\n'
    };

    auto match_prefix = [&](const char* prefix, size_t len) -> bool {
        if (static_cast<size_t>(end - p) >= len &&
            std::string_view(p, len) == std::string_view(prefix, len)) {
            p += len;
            return true;
        }
        return false;
    };

    // Hard limits to prevent malicious OOM attacks.
    constexpr int MAX_1D_SIZE = 65536;
    constexpr int MAX_3D_SIZE = 256;

    // Parse a scalar pair "lo hi" into all-channel domain values.
    // Used for LUT_*_INPUT_RANGE directives.
    auto parse_scalar_range = [&](Domain& dom) -> bool {
        float lo, hi;
        auto res_lo = fast_float::from_chars(p, end, lo);
        if (res_lo.ec != std::errc()) return false;
        p = res_lo.ptr; skip_whitespace();
        auto res_hi = fast_float::from_chars(p, end, hi);
        if (res_hi.ec != std::errc()) return false;
        p = res_hi.ptr;
        dom.min = {lo, lo, lo};
        dom.max = {hi, hi, hi};
        return true;
    };

    // -------------------------------------------------------------------------

    while (p < end) {
        skip_whitespace();
        if (p == end) break;

        if (*p == '\n') { p++; continue; }
        if (*p == '#')  { skip_line(); continue; }

        // ---- Data line (starts with digit / sign / dot) ---------------------
        if (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9')) {
            float r, g, b;

            auto resR = fast_float::from_chars(p, end, r);
            if (resR.ec != std::errc()) { skip_line(); continue; }
            p = resR.ptr; skip_whitespace();

            auto resG = fast_float::from_chars(p, end, g);
            if (resG.ec != std::errc()) { skip_line(); continue; }
            p = resG.ptr; skip_whitespace();

            auto resB = fast_float::from_chars(p, end, b);
            if (resB.ec != std::errc()) { skip_line(); continue; }
            p = resB.ptr;

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

            skip_line();
            continue;
        }

        // ---- TITLE ----------------------------------------------------------
        if (match_prefix("TITLE ", 6)) {
            skip_whitespace();
            const char* start = p;
            while (p < end && *p != '\n' && *p != '\r') p++;
            lut.title = std::string(start, p - start);
            if (lut.title.size() >= 2 &&
                lut.title.front() == '"' && lut.title.back() == '"') {
                lut.title = lut.title.substr(1, lut.title.size() - 2);
            }
            skip_line();
            continue;
        }

        // ---- LUT_1D_SIZE ----------------------------------------------------
        if (match_prefix("LUT_1D_SIZE ", 12)) {
            skip_whitespace();
            int size;
            auto res = std::from_chars(p, end, size);
            if (res.ec == std::errc() && size > 0 && size <= MAX_1D_SIZE) {
                LutData1D d1;
                d1.size   = size;
                d1.domain = current_domain;
                expected_1d_items = size * 3;
                d1.data.reserve(expected_1d_items);
                lut.shaper1D = std::move(d1);
                current_domain = Domain{};
                p = res.ptr;
                saw_any_size = true;
            } else {
                result.error        = ParseError::InvalidLutSize;
                result.errorMessage = "LUT_1D_SIZE value is out of range [1, 65536]";
                return result;
            }
            skip_line();
            continue;
        }

        // ---- LUT_3D_SIZE ----------------------------------------------------
        if (match_prefix("LUT_3D_SIZE ", 12)) {
            skip_whitespace();
            int size;
            auto res = std::from_chars(p, end, size);
            if (res.ec == std::errc() && size > 0 && size <= MAX_3D_SIZE) {
                LutData3D d3;
                d3.size   = size;
                d3.domain = current_domain;
                expected_3d_items = size * size * size * 3;
                d3.data.reserve(expected_3d_items);
                lut.grid3D = std::move(d3);
                current_domain = Domain{};
                p = res.ptr;
                saw_any_size = true;
            } else {
                result.error        = ParseError::InvalidLutSize;
                result.errorMessage = "LUT_3D_SIZE value is out of range [1, 256]";
                return result;
            }
            skip_line();
            continue;
        }

        // ---- DOMAIN_MIN / DOMAIN_MAX ----------------------------------------
        if (match_prefix("DOMAIN_MIN ", 11)) {
            skip_whitespace();
            bool ok = true;
            for (int i = 0; i < 3; ++i) {
                auto res = fast_float::from_chars(p, end, current_domain.min[i]);
                if (res.ec != std::errc()) { ok = false; break; }
                p = res.ptr; skip_whitespace();
            }
            if (!ok) {
                result.error        = ParseError::InvalidDomain;
                result.errorMessage = "DOMAIN_MIN contains an unparseable float value";
                return result;
            }
            skip_line();
            continue;
        }

        if (match_prefix("DOMAIN_MAX ", 11)) {
            skip_whitespace();
            bool ok = true;
            for (int i = 0; i < 3; ++i) {
                auto res = fast_float::from_chars(p, end, current_domain.max[i]);
                if (res.ec != std::errc()) { ok = false; break; }
                p = res.ptr; skip_whitespace();
            }
            if (!ok) {
                result.error        = ParseError::InvalidDomain;
                result.errorMessage = "DOMAIN_MAX contains an unparseable float value";
                return result;
            }
            skip_line();
            continue;
        }

        // ---- FEAT-3: LUT_1D_INPUT_RANGE / LUT_3D_INPUT_RANGE ---------------
        // Syntax: LUT_*_INPUT_RANGE <lo> <hi>
        // Sets a uniform input range [lo, hi] for all three channels, equivalent
        // to DOMAIN_MIN lo lo lo followed by DOMAIN_MAX hi hi hi.
        if (match_prefix("LUT_1D_INPUT_RANGE ", 19) ||
            match_prefix("LUT_3D_INPUT_RANGE ", 19)) {
            skip_whitespace();
            if (!parse_scalar_range(current_domain)) {
                result.error        = ParseError::InvalidDomain;
                result.errorMessage = "LUT_*_INPUT_RANGE contains an unparseable float value";
                return result;
            }
            skip_line();
            continue;
        }

        // Unknown keyword – skip safely.
        skip_line();
    }

    // ---- Final validation ---------------------------------------------------
    if (!lut.isValid()) {
        if (!saw_any_size) {
            result.error        = ParseError::MissingLutSizeDirective;
            result.errorMessage = "No LUT_1D_SIZE or LUT_3D_SIZE directive was found in the content";
        } else {
            // Size directive was seen but not enough data rows followed it.
            int got_1d = read_1d_items / 3;
            int exp_1d = expected_1d_items / 3;
            int got_3d = read_3d_items / 3;
            int exp_3d = expected_3d_items / 3;
            result.error = ParseError::InsufficientData;
            if (expected_1d_items > 0 && got_1d < exp_1d) {
                result.errorMessage =
                    "LUT_1D: expected " + std::to_string(exp_1d) +
                    " data rows but found " + std::to_string(got_1d);
            } else {
                result.errorMessage =
                    "LUT_3D: expected " + std::to_string(exp_3d) +
                    " data rows but found " + std::to_string(got_3d);
            }
        }
        return result;
    }

    result.lut = std::move(lut);
    return result;
}

ParseResult Parser::parseFile(const std::string& filePath) {
    ParseResult result;

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.error        = ParseError::FileNotFound;
        result.errorMessage = "Cannot open file: " + filePath;
        return result;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content(static_cast<size_t>(size), '\0');
    if (!file.read(&content[0], size)) {
        result.error        = ParseError::FileReadError;
        result.errorMessage = "Failed to read file: " + filePath;
        return result;
    }

    return parseString(content);
}

// ---------------------------------------------------------------------------
// Legacy wrappers – delegate to the error-aware API, discard diagnostic.
// ---------------------------------------------------------------------------
std::optional<Lut> Parser::fromFile(const std::string& filePath) {
    return parseFile(filePath).lut;
}

std::optional<Lut> Parser::fromString(std::string_view content) {
    return parseString(content).lut;
}

} // namespace cubelut
