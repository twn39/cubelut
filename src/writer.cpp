#include "cubelut/writer.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace cubelut {

// ---------------------------------------------------------------------------
// writeErrorToString
// ---------------------------------------------------------------------------

const char* writeErrorToString(WriteError e) noexcept {
    switch (e) {
        case WriteError::None:           return "No error";
        case WriteError::InvalidLut:     return "LUT is invalid or empty";
        case WriteError::FileOpenError:  return "Cannot open or create file";
        case WriteError::FileWriteError: return "Write operation failed";
    }
    return "Unknown error";
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static bool isDefaultDomain(const Domain& d) {
    return d.min[0] == 0.0f && d.min[1] == 0.0f && d.min[2] == 0.0f
        && d.max[0] == 1.0f && d.max[1] == 1.0f && d.max[2] == 1.0f;
}

// Write DOMAIN_MIN / DOMAIN_MAX before a LUT_*D_SIZE directive.
// cubelut's parser reads domain BEFORE the size keyword, so this order
// is required for roundtrip fidelity.
void Writer::writeDomain(std::ostream& os,
                         const Domain& domain,
                         const WriteOptions& opts) {
    if (!opts.writeDomainIfNonDefault) return;
    if (isDefaultDomain(domain)) return;

    os << "DOMAIN_MIN "
       << domain.min[0] << ' ' << domain.min[1] << ' ' << domain.min[2] << '\n';
    os << "DOMAIN_MAX "
       << domain.max[0] << ' ' << domain.max[1] << ' ' << domain.max[2] << '\n';
}

void Writer::write1DData(std::ostream& os, const LutData1D& lut) {
    for (int i = 0; i < lut.size; ++i) {
        os << lut.data[i*3+0] << ' '
           << lut.data[i*3+1] << ' '
           << lut.data[i*3+2] << '\n';
    }
}

void Writer::write3DData(std::ostream& os, const LutData3D& lut) {
    const size_t total = static_cast<size_t>(lut.size) * lut.size * lut.size;
    for (size_t i = 0; i < total; ++i) {
        os << lut.data[i*3+0] << ' '
           << lut.data[i*3+1] << ' '
           << lut.data[i*3+2] << '\n';
    }
}

// ---------------------------------------------------------------------------
// toStream – core serialization logic (all other methods call this)
// ---------------------------------------------------------------------------

WriteError Writer::toStream(const Lut& lut,
                            std::ostream& os,
                            const WriteOptions& opts) {
    if (!lut.isValid()) return WriteError::InvalidLut;

    // Set fixed-point format: "0.123456" not "1.23456e-01"
    // Matches OCIO (FileFormatResolveCube.cpp:646) and Resolve standard.
    os.setf(std::ios::fixed, std::ios::floatfield);
    os.precision(opts.precision);

    // ── Comment block (must precede all header directives per Resolve spec) ──
    if (opts.writeGeneratorComment) {
        os << "# Created by cubelut\n";
    }
    for (const auto& c : opts.comments) {
        os << "# " << c << '\n';
    }
    if (opts.writeGeneratorComment || !opts.comments.empty()) {
        os << '\n';
    }

    // ── TITLE ────────────────────────────────────────────────────────────────
    if (!lut.title.empty()) {
        os << "TITLE \"" << lut.title << "\"\n";
    }

    // ── Header: SIZE directives + per-LUT domain ──────────────────────────
    // Order: [DOMAIN] LUT_1D_SIZE  [DOMAIN] LUT_3D_SIZE
    // DOMAIN must precede its SIZE directive so cubelut's parser can read it.
    if (lut.shaper1D.has_value()) {
        writeDomain(os, lut.shaper1D->domain, opts);
        os << "LUT_1D_SIZE " << lut.shaper1D->size << '\n';
    }
    if (lut.grid3D.has_value()) {
        writeDomain(os, lut.grid3D->domain, opts);
        os << "LUT_3D_SIZE " << lut.grid3D->size << '\n';
    }

    // ── Data: 1D before 3D (Resolve spec requirement) ─────────────────────
    if (lut.shaper1D.has_value()) {
        write1DData(os, *lut.shaper1D);
    }
    if (lut.grid3D.has_value()) {
        write3DData(os, *lut.grid3D);
    }

    if (os.fail()) return WriteError::FileWriteError;
    return WriteError::None;
}

// ---------------------------------------------------------------------------
// toString / toFile – thin wrappers over toStream
// ---------------------------------------------------------------------------

std::string Writer::toString(const Lut& lut, const WriteOptions& opts) {
    std::ostringstream oss;
    if (toStream(lut, oss, opts) != WriteError::None) return {};
    return oss.str();
}

WriteResult Writer::toFile(const Lut& lut,
                           const std::string& filePath,
                           const WriteOptions& opts) {
    WriteResult result;

    if (!lut.isValid()) {
        result.error        = WriteError::InvalidLut;
        result.errorMessage = "LUT is invalid: isValid() returned false";
        return result;
    }

    // Use binary mode to prevent Windows CRLF translation on LF-only output.
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs.is_open()) {
        result.error        = WriteError::FileOpenError;
        result.errorMessage = "Cannot open file for writing: " + filePath;
        return result;
    }

    const WriteError err = toStream(lut, ofs, opts);
    if (err != WriteError::None) {
        result.error        = err;
        result.errorMessage = std::string(writeErrorToString(err))
                              + ": " + filePath;
    }
    return result;
}

} // namespace cubelut
