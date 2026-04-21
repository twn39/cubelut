#pragma once

#include <string>
#include <string_view>
#include <optional>
#include "lut.hpp"

namespace cubelut {

// ---------------------------------------------------------------------------
// FEAT-1: Structured error reporting for parse failures.
// ---------------------------------------------------------------------------

/// Diagnostic codes returned by the error-aware parse API.
enum class ParseError {
    None,                    ///< No error; parse succeeded.
    FileNotFound,            ///< Path does not exist or cannot be opened.
    FileReadError,           ///< File opened but could not be fully read into memory.
    MissingLutSizeDirective, ///< Neither LUT_1D_SIZE nor LUT_3D_SIZE was found.
    InvalidLutSize,          ///< Size value is out of range or syntactically invalid.
    InvalidDomain,           ///< A DOMAIN_MIN/MAX or INPUT_RANGE directive contained unparseable values.
    InsufficientData,        ///< Fewer data triplets than the declared LUT size requires were found.
};

/// Returns a short, human-readable English description of a ParseError code.
const char* parseErrorToString(ParseError e) noexcept;

/// Result of a parse operation. Carries either the parsed LUT or a diagnostic.
struct ParseResult {
    std::optional<Lut> lut;
    ParseError         error = ParseError::None;
    std::string        errorMessage; ///< Non-empty on failure; human-readable detail.

    bool ok() const noexcept { return lut.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
};

// ---------------------------------------------------------------------------
// Parser class
// ---------------------------------------------------------------------------

class Parser {
public:
    // ------------------------------------------------------------------
    // Legacy API  –  backward-compatible; returns nullopt on any failure.
    // ------------------------------------------------------------------
    static std::optional<Lut> fromFile(const std::string& filePath);
    static std::optional<Lut> fromString(std::string_view content);

    // ------------------------------------------------------------------
    // Error-aware API  –  returns structured diagnostic on failure.
    // ------------------------------------------------------------------
    static ParseResult parseFile(const std::string& filePath);
    static ParseResult parseString(std::string_view content);
};

} // namespace cubelut
