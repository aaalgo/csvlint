#include <map>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#define LOG(x) BOOST_LOG_TRIVIAL(x)
#include "csvlint.h"

namespace csvlint {
    using namespace std;
    using namespace boost;

    vector<char> COMMON_FS{',', '\t', ' ', '|'};
    vector<char> COMMON_QUOTE{'\'', '"'};
    vector<string> NA{"NA", "N/A"};

    static const unsigned LOG_CUT_BREAKER_SAMPLE = 9;
    static const unsigned MAX_CUT_BREAKER_SAMPLE = 100;

    /// crange doesn't own the underlying data
    typedef vector<crange> Lines;

    // keep is to keep the separator in the previous line
    void split (crange ref, char fs, Lines *o, bool keep = false) {
        o->clear();
        char const *begin = ref.begin();
        while (begin < ref.end()) {
            char const *end = begin;
            while (end < ref.end() && end[0] != fs) {
                ++end;
            }
            if (keep) {
                if (end < ref.end()) {
                    ++end;
                }
                o->emplace_back(begin, end);
            }
            else {
                o->emplace_back(begin, end);
                ++end;
            }
            begin = end;
        }
    }

    // this handles the case when fs == 0 for splitting without fs
    void split_with_quote (crange ref, char fs, char quote, vector<crange> *o, vector<crange> *cut_breakers = nullptr) {
        if (quote == 0) {
            split(ref, fs, o);
            return;
        }
        o->clear();
        char const *begin = ref.begin();
        while (begin < ref.end()) {
            char const *end = begin;
            bool breaks_cut = false;
            if (begin[0] == quote) {
                ++end;
                while (end < ref.end() && end[0] != quote) {
                    if (end[0] == fs) {
                        breaks_cut = true;
                    }
                    ++end;
                }
                BOOST_VERIFY(end < ref.end());
                ++end;
            }
            else {
                while (end < ref.end() && end[0] != fs) {
                    ++end;
                }
            }
            o->emplace_back(begin, end);
            if (breaks_cut && cut_breakers) {
                if (cut_breakers->size() < MAX_CUT_BREAKER_SAMPLE) {
                    cut_breakers->push_back(o->back());
                }
            }
            begin = end+1;
        }
    }

    class Text: public Lines {
        string buffer;
    public:
        Text (string const &path, size_t max) {
            ifstream is(path.c_str());
            BOOST_VERIFY(is);
            is.seekg(0, ios::end);
            size_t sz = is.tellg();
            bool trimmed = 0;
            if ((max > 0) && (sz > max)) {
                sz = max;
                trimmed = true;
            }
            buffer.resize(sz);
            is.seekg(0);
            is.read(&buffer[0], buffer.size());
            BOOST_VERIFY(is);
            char const *begin = &buffer[0];
            char const *end = begin + buffer.size();
            BOOST_VERIFY(end > begin);
            if (!trimmed && (*(end-1) != '\n')) {
                LOG(warning) << "file doesn't end with '\\n'.";
                trimmed = true;
            }
            if (trimmed) {
                // search for the last '\n'
                char const *last = end - 1;
                while ((last >= begin) && last[0] != '\n') {
                    --last;
                }
                BOOST_VERIFY(last >= begin);
                end = last + 1;
            }
            split(crange(begin, end), '\n', this, true);
            LOG(info) << size() << " lines loaded from " << path << ".";
        }
        char const *origin () const {
            return &buffer[0];
        }
    };

    string unquote (crange in, char quote) {
        string s;
        if (quote == 0) return string(in.begin(), in.end());
        BOOST_VERIFY(in.size()>=2);
        BOOST_VERIFY(in.front() == quote && in.back() == quote);
        
        unsigned i = 1;
        while (i + 1< in.size()) {
            if (i == '\\') {
                if ((i + 1 < in.size()) && in[i+1] == quote) {
                    s.push_back(quote);
                    i += 2;
                    continue;
                }
            }
            s.push_back(in[i]);
            ++i;
        }
        return s;
    }

    bool is_quoted (crange in, char quote) {
        if (in.size() < 2) return false;
        if (in.front() != quote) return false;
        if (in.back() != quote) return false;
        return true;
    }

#if 0
    string quote_helper (string const &in) {
        string s;
        s.push_back('"');
        for (char c: in) {
            if (c == '"') {
                s.push_back('\\');
            }
            s.push_back(c);
        }
        s.push_back('"');
        return s;
    }

    string quote (string const &in) {
        for (auto c:in) {
            if ((c == '"') || isspace(c)) {
                return quote_helper(in);
            }
        }
        return in;
    }
#endif

    string char2hex (char c) {
        static char table[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
        string v;
        v.push_back(table[(c >> 4)&0xF]);
        v.push_back(table[c&0xF]);
        return v;
    }

    int detect_and_trim_eol (Text *lines) {
        int unix = 0;
        int dos = 0;
        BOOST_VERIFY(lines->size() > 1);
        for (unsigned i = 0; i < lines->size(); ++i) {
            crange &line = lines->at(i);
            BOOST_VERIFY(line.end() > line.begin());
            char const *back = line.end() - 1;
            BOOST_VERIFY(back[0] == '\n');
            char const *back2 = back - 1;
            if (back2 >= line.begin() && back2[0] == '\r') {
                line = crange(line.begin(), back2);
                ++dos;
            }
            else {
                line = crange(line.begin(), back);
                ++unix;
            }
        }
        if (unix && dos) {
            throw runtime_error("Mixed DOS & UNIX format.");
        }
        LOG(info) << "detected EOL: " << (unix ? "UNIX" : "DOS") << '.';
        return unix ? EOL_UNIX : EOL_DOS;
    }

    char detect_fs (Text const &lines, unsigned *columns) {
        map<pair<char, unsigned>, unsigned> candidates;
        for (char fs: COMMON_FS) {
            for (auto const &line: lines) {
                unsigned n = count(line.begin(), line.end(), fs) + 1;
                if (n > 1) {
                    candidates[make_pair(fs, n)] += 1;
                }
            }
        }
        if (candidates.empty()) {
            throw runtime_error("Cannot determine field delimitor.");
        }
        auto best = candidates.begin();
        for (auto it = candidates.begin(); it != candidates.end(); ++it) {
            if (it->second > best->second) {
                best = it;
            }
        }
        if (columns) {
            *columns = best->first.second;
        }
        char fs = best->first.first;
        LOG(info) << "detected FS: '" << fs << "'" << '.';
        LOG(info) << "detected columns: " << best->first.second << '.';
        return fs;
    }

    unsigned detect_prelude (Text const &lines, unsigned columns, char fs) {
        unsigned c = 0;
        for (unsigned i = 0; i < lines.size(); ++i) {
            unsigned n = count(lines[i].begin(), lines[i].end(), fs) + 1;
            if (n != columns) {
                ++c;
            }
            else break;
        }
        if (c) {
            LOG(warning) << "prelude detected, " << c << " lines.";
        }
        return c;
    }

    int detect_quote (Text const &lines, unsigned prelude, unsigned columns, char fs) {
        std::unordered_map<char, unsigned> cnt;
        unsigned total = 0;
        for (crange const &line: lines) {
            if (total >= 10000) break;
            vector<crange> v;
            split(line, fs, &v);
            for (auto f: v) {
                if (f.begin() + 1 < f.end()) {
                    if (f.begin()[0] == (f.end()-1)[0]) {
                        if (!isalnum(f.begin()[0])) {
                            ++total;
                            ++cnt[f.begin()[0]];
                        }
                    }
                }
            }
        }
        if (cnt.size() > 1) {
            cerr << "Multiple quotes detected:";
            for (auto p: cnt) {
                cerr << ' ' << '\'' << p.second << '\'' << ':' << p.first;
            }
            cerr << endl;
            throw runtime_error("Multiple quotes detected.");
        }
        if (cnt.empty()) {
            LOG(info) << "no quote detected.";
            return 0;
        }
        char quote = cnt.begin()->first;
        LOG(info) << "quote detectd: '" << quote << "'.";
        return quote;
    }

#if 0
    void Format::describe () {
        LOG(inf) << "RS: " << (eol_type == EOL_DOS ? "DOS" : "UNIX") << endl;
        cerr << "FS: '" << fmt->delim << '\'' << /*" " << char2hex(fmt->delim) <<*/ endl;
        cerr << "NF: " << fmt->fields.size() << endl;
        cerr << "PRELUDE: " << fmt->prelude << endl;
    }
#endif

    bool is_numeric (crange const &s) {
        int num = 0;
        int dot = 0;
        char const *p = s.begin();
        if (p >= s.end()) return false;
        if (*p == '-') ++p;
        for (; p != s.end(); ++p) {
            if ((*p >= '0') && (*p <= '9')) {
                ++num;
            }
            else if (*p == '.') {
                ++dot;
            }
            else return false;

        }
        return (num > 0) && (dot <= 1);
    }




    void Format::trainField (vector<crange> const &v, Field *field, FieldExt *ext) {
        field->quoted = false;
        // test numeric value
        do {
            vector<crange> bad;
            unsigned total = 0;
            for (crange const &s: v) {
                if (!is_numeric(s)) {
                    bad.push_back(s);
                    //if (bad.size() >= v.size() * 3 / 4) goto not_numeric;
                    if (bad.front() != bad.back()) goto not_numeric;    // different missing values
                }
                else {
                    ++total;
                }
            }
            if (total == 0) goto not_numeric;
            // we are sure this column is numeric
            field->type = TYPE_NUMERIC;
            if (bad.size() > 0) {
                ext->na = string(bad[0].begin(), bad[0].end());
            }
            return;
        } while (false);
not_numeric:
        // test quote
        ext->missing = 0;
        ext->na.clear();
        if (quote_char) { // missing detection is only viable if we have quote
                         // otherwise no way to distinguish between empty string and missing values
            unordered_map<string, int> cnts;
            for (crange const &s: v) {
                if (s.empty() || s.front() != quote_char) {
                    ++cnts[string(s.begin(), s.end())];
                }
            }
            field->quoted = true;
            if (cnts.size() > 1) {
                cerr << "Multiple missing values detected:";
                for (auto const &p: cnts) {
                    cerr << ' ' << p.first << ':' << p.second;
                }
                cerr << endl;
                throw runtime_error("Multiple missing values detected.");
            }
            if (cnts.size()) {
                ext->na = cnts.begin()->first;
                ext->missing = cnts.begin()->second;
            }
        }
        field->type = TYPE_STRING;
    }

    void Format::train (string const &path, size_t max_buffer) {
        Text lines(path, max_buffer);

        eol_type = detect_and_trim_eol(&lines);
        if (eol_type == EOL_UNIX) {
            eol_str = "\n";
        }
        else {
            eol_str = "\r\n";
        }

        unsigned columns;
        fs_char = detect_fs(lines, &columns);
        // test delims
        // determine prelude
        prelude = detect_prelude(lines, columns, fs_char);
        unsigned first = prelude;
        if (first + 1 < lines.size()) ++first;  // skip potential title row
        quote_char = detect_quote(lines, first, columns, fs_char);

        vector<vector<crange>> matrix(columns);
        fields.resize(columns);
        vector<FieldExt> fields_ext(columns);
        max_line = 0;
        {
            for (auto &v: matrix) {
                v.reserve(lines.size());
            }
            // guess column types
            size_t samples = 0;
            vector<crange> cols;
            vector<crange> cut_breakers;
            
            for (unsigned i = first; i < lines.size(); ++i) {
                size_t sz = lines[i].size();
                if (sz > max_line) max_line = sz;
                split_with_quote(lines[i], fs_char, quote_char, &cols, &cut_breakers);
                if (cols.size() != columns) {
                    LOG(info) << "BAD LINE: " << lines[i];
                    for (auto r: cols) {
                        LOG(info) << "F: " << string(r.begin(), r.end());
                    }
                    continue;
                }
                for (unsigned j = 0; j < cols.size(); ++j) {
                    matrix[j].push_back(cols[j]);
                }
                ++samples;
            }
            LOG(info) << samples << 'x' << columns << " entries loaded.";
            if (cut_breakers.size()) {
                LOG(warning) << "found FS within quoted string, will break cut.";
                unsigned display = cut_breakers.size();
                if (display > LOG_CUT_BREAKER_SAMPLE) {
                    display = LOG_CUT_BREAKER_SAMPLE;
                }
                for (unsigned i = 0; i < display; ++i) {
                    auto p = cut_breakers[i];
                    LOG(info) << "cut-breaker: " << string(p.begin(), p.end());
                }
                if (display < cut_breakers.size()) {
                    LOG(info) << "more cut-breakers found...";
                }
            }
            for (unsigned i = 0; i < columns; ++i) {
                fields[i].column = i;
                trainField(matrix[i], &fields[i], &fields_ext[i]);
            }
        }
        {   // check missing values
            map<string, int> cnts;
            for (auto const &ext: fields_ext) {
                if (ext.missing) {
                    ++cnts[ext.na];
                }
            }
            if (cnts.size() > 1) {
                cerr << "Multiple missing values detected:";
                for (auto p: cnts) {
                        cerr << ' ' << '\'' << p.first << '\'' << ':' << p.second;
                }
                cerr << endl;
                exit(-1);
            }
            if (cnts.size() == 1) {
                na_str = cnts.begin()->first;
                LOG(info) << "detected N/A string: \"" << na_str << "\".";
            }
        }
        has_header = false;
        header_quoted = false;
        if (lines.size() > prelude) { // test first line
            vector<crange> cols;
            split(lines[prelude], fs_char, &cols);
            /*
            cout << cols.size() << endl;
            cout << fmt->fields.size() << endl;
            */
            if (cols.size() == fields.size()) {
                // test column head
                for (unsigned i = 0; i < cols.size(); ++i) {
                    if (fields[i].type == TYPE_NUMERIC) {
                        if (!is_numeric(cols[i])) {
                            has_header = true;
                            break;
                        }
                    }
                    /*
                    else if (fmt->fields[i].type == TYPE_NOMINAL) {
                        if (fmt->fields[i].values.count(unquote(cols[i], fmt->quote)) == 0) {
                            fmt->has_header = true;
                            break;
                        }
                    }
                    */
                    else if (fields[i].quoted && cols[i].size() && (cols[i].front() != quote_char)) {
                        has_header = true;
                        break;
                    }
                }
            }
            if (has_header) {
                LOG(info) << "header line detected.";
                for (unsigned i = 0; i < columns; ++i) {
                    if (is_quoted(cols[i], quote_char)) {
                        fields[i].name = unquote(cols[i], quote_char);
                        if (i > 0) {
                            BOOST_VERIFY(header_quoted);
                        }
                        else {
                            header_quoted = true;
                        }
                    }
                    else {
                        fields[i].name = string(cols[i].begin(), cols[i].end());
                    }
                }
            }

        }
        unsigned start = prelude;
        if (has_header) ++start;
        data_offset = lines[start].begin() - lines.origin();
    }

    void Format::summary (ostream &os, bool details) const {
        for (unsigned i = 0; i < fields.size(); ++i) {
            os << "COL:" << i;
            auto const &field = fields[i];
            os << " NAME:" << field.name;
            os << " TYPE:";
            switch (field.type) {
                case TYPE_NUMERIC: os << "numeric"; break;
                case TYPE_STRING: os << "string"; break;
                default: BOOST_VERIFY(0);
            }
            os << endl;
        }
    }

    int strcmp (crange in, string const &str) {
        return str.compare(0, string::npos, in.begin(), in.size());
    }

    bool Format::parse (crange in, vector<crange> *out) const {
        out->resize(fields.size());
        char const *begin = in.begin();
        char const *end = in.end();
        if (end <= begin) return false;
        // remove end
        {
            char const *back = end - 1;
            if (eol_type == EOL_UNIX) {
                if (back[0] == '\n') {
                    end = back;
                }
            }
            else if (eol_type == EOL_DOS) {
                if (back[0] == '\n') {
                    if (back <= begin) return false;
                    --back;
                    if (back[0] != '\r') return false;
                    end = back;
                }
            }
        }
        // separate to field
        split_with_quote(crange(begin, end), fs_char, quote_char, out);
        if (out->size() != fields.size()) return false;
        for (unsigned i = 0; i < fields.size(); ++i) {
            Field const &field = fields[i];
            auto &col = out->at(i);
            if (strcmp(col, na_str) == 0) {
                col = crange(col.begin(), col.end(), true);
                continue;
            }
            if (field.quoted) {
                if (col.empty()) return false;
                if (col.front() != quote_char) return false;
                if (col.back() != quote_char) return false;
                col = crange(col.begin() + 1, col.end() - 1);
            }
        }
        return true;
    }

    void Format::write_header (ostream &os) const {
        for (unsigned i = 0; i < fields.size(); ++i) {
            if (i) {
                os << fs_char;
            }
            if (header_quoted) {
                os << quote_char << fields[i].name << quote_char;
            }
            else {
                os << fields[i].name;
            }
        }
        os << eol_str;
    }

    void Format::write_line (ostream &os, vector<crange> const &in) const {
        for (unsigned i = 0; i < fields.size(); ++i) {
            if (i) {
                os << fs_char;
            }
            if (in[i].missing()) {
                os << na_str;
            }
            else if (fields[i].quoted) {
                os << quote_char << string(in[i].begin(), in[i].end()) << quote_char;
            }
            else {
                os << string(in[i].begin(), in[i].end());
            }
        }
        os << eol_str;
    }
}

