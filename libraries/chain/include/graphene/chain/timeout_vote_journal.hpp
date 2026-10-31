#pragma once

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace graphene {
    namespace chain {

        /**
         * Local cache of accepted timeout votes, so a restart does not throw away evidence
         * the node already collected.
         *
         * Timeout votes are P2P messages, not transactions: what a node accumulated lives in
         * memory only, and a restart (watchdog, deploy, OOM-kill) drops it. On 2026-10-31 the
         * watchdog restarted the hub node every 10 minutes during a chain-wide stall, and every
         * restart reset the round to zero.
         *
         * A disk copy cannot become a source of trust: the caller replays every line through the
         * SAME push_timeout_vote that handles live votes, so the round window, the schedule
         * membership, the non-empty signing key and the signature are all checked again. A
         * tampered file is therefore just a file whose lines get rejected.
         *
         * Format: one vote per line, tab separated -- round, high_qc_round, validator, 65-byte
         * signature as 130 hex digits. TSV rather than JSON so this class stays free of the fc
         * libraries and can be tested on its own.
         */
        class timeout_vote_journal {
            public:
                static constexpr size_t SIGNATURE_BYTES = 65;
                static constexpr size_t HEX_SIGNATURE_BYTES = SIGNATURE_BYTES * 2;
                /** A compaction failure shows up as a file bigger than this, not as data. */
                static constexpr size_t MAX_FILE_BYTES = 64u * 1024u * 1024u;
                /** Votes are buffered in memory and written in blocks; consensus must not wait on I/O. */
                static constexpr size_t FLUSH_THRESHOLD = 64u * 1024u;
                static constexpr size_t MAX_VALIDATOR_LENGTH = 32;
                static constexpr size_t MAX_LINE_BYTES = 512;

                using signature_type = std::array<unsigned char, SIGNATURE_BYTES>;
                /** Returns true when the vote was accepted (it is then kept on disk). */
                using replay_fn = std::function<bool(uint32_t round, uint32_t high_qc_round,
                                                     const std::string &validator,
                                                     const signature_type &signature)>;

                struct replay_result {
                    size_t lines = 0;       ///< lines that parsed as votes
                    size_t accepted = 0;    ///< votes the caller accepted (kept on disk)
                    size_t dropped = 0;     ///< malformed lines + votes the caller rejected/aged out
                };

                explicit timeout_vote_journal(std::string path) : _path(std::move(path)) {}
                timeout_vote_journal(const timeout_vote_journal &) = delete;
                timeout_vote_journal &operator=(const timeout_vote_journal &) = delete;
                ~timeout_vote_journal() { close(); }

                const std::string &path() const { return _path; }
                bool is_open() const { return _file != nullptr; }
                bool is_failed() const { return _failed; }

                /**
                 * Opens (creating if needed) the journal for appending. A failure is reported in
                 * `error` and leaves the journal closed: a node that cannot cache votes still
                 * starts, it simply starts with an empty cache.
                 */
                bool open(std::string &error) {
                    close();
                    _failed = false;
                    _file = std::fopen(_path.c_str(), "a+");
                    if (_file == nullptr) {
                        error = std::strerror(errno);
                        return false;
                    }
                    return true;
                }

                /** Discards everything on disk (the chain state was wiped, the cache means nothing). */
                bool truncate(std::string &error) {
                    flush();
                    if (_file != nullptr) {
                        std::fclose(_file);
                        _file = nullptr;
                    }
                    _file = std::fopen(_path.c_str(), "w");
                    if (_file == nullptr) {
                        error = std::strerror(errno);
                        return false;
                    }
                    std::fclose(_file);
                    _file = std::fopen(_path.c_str(), "a+");
                    if (_file == nullptr) {
                        error = std::strerror(errno);
                        return false;
                    }
                    return true;
                }

                /**
                 * Reads the journal, hands every parsable vote to `fn` and rewrites the file with
                 * exactly the votes `fn` accepted. That rewrite is the compaction: aged-out and
                 * malformed lines never come back.
                 *
                 * `min_round` drops older lines before `fn` is called -- the caller knows the round
                 * window and does not need to pay a signature check for votes that cannot be used.
                 */
                replay_result replay(const replay_fn &fn, uint32_t min_round, std::string &error) {
                    replay_result result;
                    error.clear();
                    if (_file == nullptr) {
                        error = "not open";
                        return result;
                    }
                    if (std::fseek(_file, 0, SEEK_END) != 0) {
                        error = std::strerror(errno);
                        return result;
                    }
                    const long size = std::ftell(_file);
                    if (size < 0) {
                        error = std::strerror(errno);
                        return result;
                    }
                    if (static_cast<size_t>(size) > MAX_FILE_BYTES) {
                        // A file this large means compaction is not running. Reading it would
                        // trade a bounded startup for unbounded memory, so start over instead.
                        error = "journal is larger than " + std::to_string(MAX_FILE_BYTES) +
                                " bytes; truncating it instead of reading";
                        std::string truncate_error;
                        truncate(truncate_error);
                        return result;
                    }

                    std::vector<std::string> keep;
                    char line[MAX_LINE_BYTES];
                    std::rewind(_file);
                    while (std::fgets(line, sizeof(line), _file) != nullptr) {
                        std::string raw(line);
                        const bool terminated = !raw.empty() && raw.back() == '\n';
                        if (terminated) raw.pop_back();
                        if (raw.empty()) continue;

                        ++result.lines;
                        uint32_t round = 0, high_qc_round = 0;
                        std::string validator;
                        signature_type signature{};
                        if (!parse_line(raw, round, high_qc_round, validator, signature)) {
                            ++result.dropped;
                            continue;
                        }
                        if (round < min_round) {
                            ++result.dropped;
                            continue;
                        }
                        bool accepted = false;
                        try {
                            accepted = fn(round, high_qc_round, validator, signature);
                        } catch (...) {
                            // A single unreadable vote must never keep the node from starting.
                            accepted = false;
                        }
                        if (!accepted) {
                            ++result.dropped;
                            continue;
                        }
                        ++result.accepted;
                        keep.push_back(raw);
                    }

                    std::string rewrite_error;
                    if (!rewrite(keep, rewrite_error) && error.empty()) error = rewrite_error;
                    return result;
                }

                void append(uint32_t round, uint32_t high_qc_round, const std::string &validator,
                            const signature_type &signature) {
                    if (_file == nullptr || _failed) return;
                    if (!valid_validator(validator)) return;

                    _pending += std::to_string(round);
                    _pending += '\t';
                    _pending += std::to_string(high_qc_round);
                    _pending += '\t';
                    _pending += validator;
                    _pending += '\t';
                    append_hex(_pending, signature);
                    _pending += '\n';

                    if (_pending.size() >= FLUSH_THRESHOLD) flush();
                }

                /**
                 * Writes buffered votes. Called on a threshold, before a certificate is formed
                 * (so a crash right after producing does not lose the vote that made it) and on close.
                 */
                void flush() {
                    if (_file == nullptr) return;
                    if (!_pending.empty()) {
                        const size_t written = std::fwrite(_pending.data(), 1, _pending.size(), _file);
                        if (written != _pending.size()) {
                            // Stop accepting votes rather than let the buffer grow without bound.
                            _failed = true;
                        }
                        _pending.clear();
                    }
                    std::fflush(_file);
                }

                void close() {
                    if (_file == nullptr) {
                        _pending.clear();
                        return;
                    }
                    flush();
                    std::fclose(_file);
                    _file = nullptr;
                }

            private:
                static bool valid_validator(const std::string &validator) {
                    if (validator.empty() || validator.size() > MAX_VALIDATOR_LENGTH) return false;
                    for (const char c : validator) {
                        if (c == '\t' || c == '\n' || c == '\r') return false;
                        if (static_cast<unsigned char>(c) < 0x20) return false;
                    }
                    return true;
                }

                static bool parse_uint(const std::string &text, uint32_t &out) {
                    if (text.empty() || text.size() > 10) return false;
                    uint64_t value = 0;
                    for (const char c : text) {
                        if (c < '0' || c > '9') return false;
                        value = value * 10 + uint64_t(c - '0');
                    }
                    if (value > 0xFFFFFFFFull) return false;
                    out = uint32_t(value);
                    return true;
                }

                static int hex_value(const char c) {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                }

                static bool parse_line(const std::string &raw, uint32_t &round, uint32_t &high_qc_round,
                                       std::string &validator, signature_type &signature) {
                    size_t field_begin = 0;
                    std::string fields[4];
                    for (size_t i = 0; i < 4; ++i) {
                        const size_t tab = raw.find('\t', field_begin);
                        if (i < 3) {
                            if (tab == std::string::npos) return false;
                            fields[i] = raw.substr(field_begin, tab - field_begin);
                            field_begin = tab + 1;
                        } else {
                            if (tab != std::string::npos) return false;
                            fields[i] = raw.substr(field_begin);
                        }
                    }
                    if (fields[3].size() != HEX_SIGNATURE_BYTES) return false;
                    for (size_t i = 0; i < SIGNATURE_BYTES; ++i) {
                        const int hi = hex_value(fields[3][i * 2]);
                        const int lo = hex_value(fields[3][i * 2 + 1]);
                        if (hi < 0 || lo < 0) return false;
                        signature[i] = static_cast<unsigned char>(hi * 16 + lo);
                    }
                    if (!parse_uint(fields[0], round)) return false;
                    if (!parse_uint(fields[1], high_qc_round)) return false;
                    validator = fields[2];
                    return valid_validator(validator);
                }

                static void append_hex(std::string &out, const signature_type &signature) {
                    static const char digits[] = "0123456789abcdef";
                    for (const unsigned char byte : signature) {
                        out += digits[byte >> 4];
                        out += digits[byte & 0x0F];
                    }
                }

                /**
                 * Compaction writes a temporary file and renames it over the journal, so a crash
                 * mid-rewrite leaves the previous cache intact instead of a half-written one.
                 */
                bool rewrite(const std::vector<std::string> &lines, std::string &error) {
                    if (std::fclose(_file) != 0) {
                        _file = nullptr;
                        error = std::strerror(errno);
                        return false;
                    }
                    _file = nullptr;

                    const std::string tmp_path = _path + ".tmp";
                    std::FILE *tmp = std::fopen(tmp_path.c_str(), "w");
                    if (tmp == nullptr) {
                        error = std::strerror(errno);
                        return false;
                    }
                    for (const std::string &line : lines) {
                        const std::string record = line + "\n";
                        if (std::fwrite(record.data(), 1, record.size(), tmp) != record.size()) {
                            error = "short write while compacting";
                            std::fclose(tmp);
                            std::remove(tmp_path.c_str());
                            return false;
                        }
                    }
                    if (std::fclose(tmp) != 0) {
                        error = std::strerror(errno);
                        std::remove(tmp_path.c_str());
                        return false;
                    }
                    if (std::rename(tmp_path.c_str(), _path.c_str()) != 0) {
                        // Windows refuses to rename over an existing file.
                        std::remove(_path.c_str());
                        if (std::rename(tmp_path.c_str(), _path.c_str()) != 0) {
                            error = std::strerror(errno);
                            std::remove(tmp_path.c_str());
                            return false;
                        }
                    }
                    _file = std::fopen(_path.c_str(), "a+");
                    if (_file == nullptr) {
                        error = std::strerror(errno);
                        return false;
                    }
                    return true;
                }

                std::string _path;
                std::FILE *_file = nullptr;
                std::string _pending;
                bool _failed = false;
        };

    } // namespace chain
} // namespace graphene
