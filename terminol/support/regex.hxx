// vi:noai:sw=4
// Copyright © 2013 David Bryant

#ifndef SUPPORT__REGEX__HXX
#define SUPPORT__REGEX__HXX

#include "terminol/support/pattern.hxx"
#include "terminol/support/debug.hxx"
#include "terminol/support/conv.hxx"

#include <vector>
#include <string>

#include <pcre.h>

// Regex r("foo");
// r.match("foo bar");

class Regex : protected Uncopyable {
    pcre    * _pcre;
    const int _maxMatches;
public:
    struct Substr {
        Substr(int first_, int last_) : first(first_), last(last_) {}

        int first;
        int last;
    };

    struct Error {
        explicit Error(const std::string & message_) : message(message_) {}
        std::string message;
    };

    explicit Regex(const std::string & pattern, int maxMatches = 10) throw (Error) :
        _pcre(nullptr),
        _maxMatches(maxMatches)
    {
        const char * err       = nullptr;
        int          errOffset = 0;

        _pcre = pcre_compile(pattern.c_str(),
                             PCRE_UTF8,
                             &err,
                             &errOffset,
                             nullptr);     // tableptr

        if (!_pcre) {
            throw Error("PCRE compilation of \"" + pattern + "\" "
                        "failed at offset " + stringify(errOffset) +
                        ", error: " + stringify(err));
        }
    }

    ~Regex() {
        pcre_free(_pcre);
    }

    // First element is "whole match", subsequent are "captures" (things in parentheses).
    std::vector<std::string> matchString(const std::string & text) const {
        return matchString(text.data(), text.size());
    }

    // First element is "whole match", subsequent are "captures" (things in parentheses).
    std::vector<std::string> matchString(const char * text, size_t size) const {
        auto substrs = common(text, size);

        std::vector<std::string> matches;

        for (auto & substr : substrs) {
            auto first = text + substr.first;
            auto last  = text + substr.last;
            matches.push_back(std::string(first, last));
        }

        return matches;
    }

    // First element is "whole match", subsequent are "captures" (things in parentheses).
    std::vector<Substr> matchOffsets(const char * text, size_t size) const {
        return common(text, size);
    }

    // First element is "whole match", subsequent are "captures" (things in parentheses).
    std::vector<Substr> matchOffsets(const std::string & text) const {
        return common(text.data(), text.size());
    }

    // First element is "whole match", subsequent are "captures" (things in parentheses).
    bool matchTest(const std::string & text) const {
        return matchTest(text.data(), text.size());
    }

    // First element is "whole match", subsequent are "captures" (things in parentheses).
    bool matchTest(const char * text, size_t size) const {
        return !common(text, size).empty();
    }

    std::vector<std::vector<Substr>> matchAllOffsets(const char * text, size_t size) const {
        std::vector<std::vector<Substr>> allOffsets;

        size_t offset = 0;

        for (;;) {
            auto offsets = common(text, size, static_cast<size_t>(offset));

            if (offsets.empty()) {
                break;
            }
            else {
                offset = offsets.front().last;
                allOffsets.push_back(std::move(offsets));

                if (offset == size) {
                    break;
                }
            }
        }

        return allOffsets;
    }

protected:
    std::vector<Substr> common(const char * text, size_t size, size_t offset = 0) const {
        // FIXME This will need reworking. We don't want to allocate offsets everytime
        // this function is called. Also, if there is insufficient match room then we want
        // to try again with more match room.
        // XXX Or, perhaps we need a different code path for when we are just
        // testing for the existence a match and not interested in the results.

        // XXX may want PCRE_NO_UTF8_CHECK check in options.

        std::vector<int>    offsets(_maxMatches * 3, 0);
        std::vector<Substr> substrs;

        auto rval = pcre_exec(_pcre,
                              nullptr,          // study
                              text,             // subject
                              size,             // length of subject
                              offset,           // offset into subject
                              0,                // options
                              &offsets.front(),
                              offsets.size());

        if (rval < 0) {
            switch (rval) {
                case PCRE_ERROR_NOMATCH:
                    break;
                default:
                    ERROR("Unknown error matching.");
                    break;
            }
            return substrs;
        }
        else if (rval == 0) {
            ERROR("Insufficient match room.");
            offsets.resize(0);
            return substrs;
        }
        else {
            for (int i = 0; i != rval; ++i) {
                substrs.emplace_back(offsets[2 * i], offsets[2 * i + 1]);
            }
            return substrs;
        }
    }
};

#endif // SUPPORT__REGEX__HXX
