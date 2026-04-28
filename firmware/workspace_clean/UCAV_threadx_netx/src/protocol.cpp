/*
 * protocol.cpp -- OMC wire-protocol parser implementations.
 *
 * Each parser tokenizes the input line in place (commas become NUL
 * separators) so there is no allocation. The first token is checked
 * against the message kind; if it does not match, the parser returns
 * NotMatchingMessage without mutating the buffer further, so the
 * caller can try the next parser on the same buffer.
 */

#include "protocol.hpp"

#include <cstring>
#include <cstdlib>

namespace omc {
namespace {

/* Maximum tokens we ever expect on a single line. FRM with 8 detections
 * has 3 + 8*5 = 43 tokens; round up to 64 with comfortable margin. */
constexpr int MAX_TOKENS = 64;

/*
 * Split 'line' in-place into comma-separated tokens. The buffer is
 * mutated: every ',' becomes '\0' and 'tokens[i]' points into the
 * original storage. Empty input yields zero tokens.
 *
 * No newlib dependency, deterministic, no allocation.
 */
int split_csv(char* line, char* tokens[], int max_tokens)
{
    if (line == nullptr || max_tokens <= 0 || *line == '\0') {
        return 0;
    }

    int   count = 0;
    char* p     = line;

    tokens[count++] = p;

    while (*p != '\0') {
        if (*p == ',') {
            *p = '\0';
            if (count >= max_tokens) {
                /* Buffer full: stop tokenizing, last token may be truncated. */
                return count;
            }
            tokens[count++] = p + 1;
        }
        ++p;
    }

    return count;
}

/* Map numeric class id to enum, returning Unknown for out-of-range. */
VehicleClass int_to_class(int v)
{
    switch (v) {
        case 0: return VehicleClass::Tank;
        case 1: return VehicleClass::Zpt;
        case 2: return VehicleClass::MilitaryTruck;
        case 3: return VehicleClass::Civilian;
        default: return VehicleClass::Unknown;
    }
}

} /* anonymous namespace */

ParseResult parse_frame(char* line, ParsedFrame& out)
{
    /* Quick prefix check before mutating the buffer.
     * Length 4 covers "FRM," exactly; shorter strings cannot be FRM. */
    if (line == nullptr ||
        line[0] != 'F' || line[1] != 'R' || line[2] != 'M' || line[3] != ',') {
        return ParseResult::NotMatchingMessage;
    }

    char* tokens[MAX_TOKENS];
    const int n = split_csv(line, tokens, MAX_TOKENS);

    if (n < 3) {
        return ParseResult::BadFormat;
    }

    /* tokens[0] is "FRM" (already validated). */
    out.frame_id        = std::atoi(tokens[1]);
    out.detection_count = std::atoi(tokens[2]);

    if (out.detection_count < 0 ||
        out.detection_count > MAX_DETECTIONS_PER_FRAME) {
        return ParseResult::OutOfRange;
    }

    /* Each detection contributes 5 fields after the header (cls,cx,cy,w,h). */
    if (n != 3 + out.detection_count * 5) {
        return ParseResult::BadFormat;
    }

    for (int i = 0; i < out.detection_count; ++i) {
        const int  base = 3 + i * 5;
        Detection& d    = out.detections[i];

        d.cls = int_to_class(std::atoi(tokens[base + 0]));
        d.cx  = std::atoi(tokens[base + 1]);
        d.cy  = std::atoi(tokens[base + 2]);
        d.w   = std::atoi(tokens[base + 3]);
        d.h   = std::atoi(tokens[base + 4]);

        if (d.cls == VehicleClass::Unknown) {
            return ParseResult::UnknownClass;
        }
    }

    return ParseResult::Ok;
}

ParseResult parse_lsrres(char* line, ParsedLsrResult& out)
{
    /* Quick prefix check before mutating: "LSRRES," = 7 chars. */
    if (line == nullptr ||
        line[0] != 'L' || line[1] != 'S' || line[2] != 'R' ||
        line[3] != 'R' || line[4] != 'E' || line[5] != 'S' ||
        line[6] != ',') {
        return ParseResult::NotMatchingMessage;
    }

    char* tokens[MAX_TOKENS];
    const int n = split_csv(line, tokens, MAX_TOKENS);

    if (n < 3) {
        return ParseResult::BadFormat;
    }

    /* tokens[0] is "LSRRES" (already validated). */
    out.frame_id   = std::atoi(tokens[1]);
    out.item_count = std::atoi(tokens[2]);

    if (out.item_count < 0 ||
        out.item_count > MAX_DETECTIONS_PER_FRAME) {
        return ParseResult::OutOfRange;
    }

    /* Each item contributes 2 fields after the header (track_id, hit). */
    if (n != 3 + out.item_count * 2) {
        return ParseResult::BadFormat;
    }

    for (int i = 0; i < out.item_count; ++i) {
        const int base = 3 + i * 2;
        out.items[i].track_id = std::atoi(tokens[base + 0]);
        out.items[i].hit      = std::atoi(tokens[base + 1]);
    }

    return ParseResult::Ok;
}

ParseResult parse_clsrres(char* line, ParsedClsrResult& out)
{
    /* Quick prefix check before mutating: "CLSRRES," = 8 chars. */
    if (line == nullptr ||
        line[0] != 'C' || line[1] != 'L' || line[2] != 'S' ||
        line[3] != 'R' || line[4] != 'R' || line[5] != 'E' ||
        line[6] != 'S' || line[7] != ',') {
        return ParseResult::NotMatchingMessage;
    }

    char* tokens[MAX_TOKENS];
    const int n = split_csv(line, tokens, MAX_TOKENS);

    if (n < 3) {
        return ParseResult::BadFormat;
    }

    out.frame_id   = std::atoi(tokens[1]);
    out.item_count = std::atoi(tokens[2]);

    constexpr int CLSRRES_MAX_ITEMS = 16;  /* matches MAX_PENDING_CLUSTERS */
    if (out.item_count < 0 || out.item_count > CLSRRES_MAX_ITEMS) {
        return ParseResult::OutOfRange;
    }

    /* Each item contributes 2 fields after the header (cluster_id, hit). */
    if (n != 3 + out.item_count * 2) {
        return ParseResult::BadFormat;
    }

    for (int i = 0; i < out.item_count; ++i) {
        const int base = 3 + i * 2;
        out.items[i].cluster_id = std::atoi(tokens[base + 0]);
        out.items[i].hit        = std::atoi(tokens[base + 1]);
    }

    return ParseResult::Ok;
}

} /* namespace omc */
