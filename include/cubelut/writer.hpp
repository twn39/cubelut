#pragma once

#include <string>
#include <ostream>
#include "lut.hpp"

namespace cubelut {

// ---------------------------------------------------------------------------
// WriteOptions – controls serialization format.
// ---------------------------------------------------------------------------

struct WriteOptions {
    /// Floating-point decimal places. 6 = OCIO/Resolve standard. 8 = smol-cube.
    int precision = 6;

    /// Write DOMAIN_MIN/DOMAIN_MAX only when the domain differs from [0,1].
    /// Set to false to suppress domain directives unconditionally.
    bool writeDomainIfNonDefault = true;

    /// Prepend a "# Created by cubelut" comment line (tool watermark).
    /// This is the only write-time comment; it identifies the generating tool,
    /// not the LUT content itself.
    bool writeGeneratorComment = true;

    /// When true (default), emit all lines from lut.comments verbatim.
    /// Set to false for "clean" output: only the tool watermark is written.
    bool preserveComments = true;
};

// ---------------------------------------------------------------------------
// WriteError / WriteResult – symmetric with ParseError / ParseResult.
// ---------------------------------------------------------------------------

enum class WriteError {
    None,           ///< Success.
    InvalidLut,     ///< lut.isValid() == false; nothing to write.
    FileOpenError,  ///< Could not open / create the destination file.
    FileWriteError, ///< File opened but write operation failed.
};

/// Returns a short human-readable description of a WriteError code.
const char* writeErrorToString(WriteError e) noexcept;

/// Result of a serialization operation.
struct WriteResult {
    WriteError  error        = WriteError::None;
    std::string errorMessage; ///< Non-empty only on failure.

    bool ok()                const noexcept { return error == WriteError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

// ---------------------------------------------------------------------------
// Writer – stateless, all-static serialization interface.
//
// Symmetric with Parser:  Parser reads .cube → Lut;  Writer writes Lut → .cube.
//
// All serialization logic goes through toStream(), which writes to any
// std::ostream. toFile() and toString() are thin wrappers on top of it.
//
// Usage:
//   // Simple write (preserves all comments from parsing)
//   auto r = cubelut::Writer::toFile(lut, "output.cube");
//   if (!r) std::cerr << r.errorMessage << "\n";
//
//   // Clean output (strip inherited comments, keep tool watermark only)
//   cubelut::WriteOptions opts;
//   opts.preserveComments = false;
//   auto r = cubelut::Writer::toFile(lut, "clean.cube", opts);
//
//   // Add programmatic comments before writing
//   lut.comments.push_back("Author: colorist@studio.com");
//   std::string text = cubelut::Writer::toString(lut);
//
//   // Custom stream
//   cubelut::Writer::toStream(lut, std::cout);
// ---------------------------------------------------------------------------

class Writer {
public:
    /// Serialize `lut` to a .cube file at `filePath`.
    static WriteResult toFile(const Lut& lut,
                              const std::string& filePath,
                              const WriteOptions& opts = {});

    /// Serialize `lut` into the stream `os`.
    /// Returns WriteError::None on success; the stream is untouched on failure.
    static WriteError toStream(const Lut& lut,
                               std::ostream& os,
                               const WriteOptions& opts = {});

    /// Serialize `lut` to a std::string.
    /// Returns an empty string if the Lut is invalid.
    static std::string toString(const Lut& lut,
                                const WriteOptions& opts = {});

private:
    /// Write DOMAIN_MIN / DOMAIN_MAX lines if the domain is non-default.
    static void writeDomain(std::ostream& os,
                            const Domain& domain,
                            const WriteOptions& opts);

    static void write1DData(std::ostream& os, const LutData1D& lut);
    static void write3DData(std::ostream& os, const LutData3D& lut);
};

} // namespace cubelut
