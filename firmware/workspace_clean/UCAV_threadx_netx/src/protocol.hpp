/*
 * protocol.hpp -- wire-protocol parsers for the OMC subsystem.
 *
 * Each parser is a pure function: no global state, no I/O, no side
 * effects beyond mutating the input buffer for in-place CSV tokenization.
 * Bare-keyword commands (ARM, DISARM, RESET, MODE_NORMAL, MODE_CLUSTER)
 * are handled directly in udp_router with strcmp; only structured
 * messages live here.
 *
 * Wire format for FRM:
 *   FRM,<frame_id>,<count>,<cls>,<cx>,<cy>,<w>,<h>, ... repeats per detection
 *
 * Example:
 *   FRM,42,2,0,200,180,40,30,3,310,90,28,22
 *   -> frame_id = 42
 *   -> 2 detections:
 *        (cls=0=Tank,     cx=200, cy=180, w=40, h=30)
 *        (cls=3=Civilian, cx=310, cy=90,  w=28, h=22)
 */

#ifndef OMC_PROTOCOL_HPP_
#define OMC_PROTOCOL_HPP_

#include "types.hpp"

namespace omc {

/* Result code from any parser. */
enum class ParseResult : int {
    Ok                   = 0,
    NotMatchingMessage   = 1,   /* line does not match this message kind */
    BadFormat            = 2,   /* malformed: too few/many fields */
    OutOfRange           = 3,   /* count outside accepted bounds */
    UnknownClass         = 4    /* class id not in {0,1,2,3} */
};

/*
 * Parsed FRM message. Only the first 'detection_count' entries of
 * 'detections' are valid.
 */
struct ParsedFrame {
    int       frame_id;
    int       detection_count;
    Detection detections[MAX_DETECTIONS_PER_FRAME];
};

/*
 * Parse one already-trimmed, NUL-terminated line as a FRM message.
 *
 * IMPORTANT: this function MUTATES 'line' in place (commas become NUL
 * separators). Callers must save a copy first if the original text is
 * still needed for logging on error.
 *
 * Returns:
 *   Ok                  -> 'out' is fully populated
 *   NotMatchingMessage  -> line does not start with "FRM,"; 'out' untouched
 *   BadFormat           -> field count mismatch
 *   OutOfRange          -> count outside [0, MAX_DETECTIONS_PER_FRAME]
 *   UnknownClass        -> class id outside {0,1,2,3}
 */
ParseResult parse_frame(char* line, ParsedFrame& out);

/*
 * Parsed LSRRES message. Wire format:
 *   LSRRES,<frame_id>,<count>,<track_id>,<hit>,<track_id>,<hit>,...
 * 'hit' is 1 for a laser hit, 0 for a miss. Only the first 'item_count'
 * entries of 'items' are valid.
 */
struct ParsedLsrResultItem {
    int track_id;
    int hit;
};
struct ParsedLsrResult {
    int frame_id;
    int item_count;
    ParsedLsrResultItem items[MAX_DETECTIONS_PER_FRAME];
};

/*
 * Parse one already-trimmed, NUL-terminated line as an LSRRES message.
 *
 * IMPORTANT: like parse_frame, this MUTATES 'line' in place.
 *
 * Returns:
 *   Ok                  -> 'out' is fully populated
 *   NotMatchingMessage  -> line does not start with "LSRRES,"; 'out' untouched
 *   BadFormat           -> field count mismatch
 *   OutOfRange          -> count outside [0, MAX_DETECTIONS_PER_FRAME]
 */
ParseResult parse_lsrres(char* line, ParsedLsrResult& out);

/*
 * Parsed CLSRRES message. Wire format (matches legacy):
 *   CLSRRES,<frame_id>,<cluster_count>,<cluster_id>,<hit>,<cluster_id>,<hit>,...
 * One (cluster_id, hit) pair per verified cluster. Only the first
 * 'item_count' entries of 'items' are valid.
 */
struct ParsedClsrResultItem {
    int cluster_id;
    int hit;
};
struct ParsedClsrResult {
    int                  frame_id;
    int                  item_count;
    ParsedClsrResultItem items[16];   /* matches MAX_PENDING_CLUSTERS */
};

/*
 * Parse one already-trimmed, NUL-terminated line as a CLSRRES message.
 * In-place tokenization like the other parsers.
 *
 * Returns:
 *   Ok                  -> 'out' is fully populated
 *   NotMatchingMessage  -> line does not start with "CLSRRES,"; 'out' untouched
 *   BadFormat           -> field count mismatch
 *   OutOfRange          -> count outside [0, 16]
 */
ParseResult parse_clsrres(char* line, ParsedClsrResult& out);

} /* namespace omc */

#endif /* OMC_PROTOCOL_HPP_ */
