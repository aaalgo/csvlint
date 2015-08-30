#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <boost/range/iterator_range_core.hpp>

namespace csvlint {
    using std::vector;
    using std::string;
    using std::unordered_map;
    using std::ostream;

    extern vector<char> COMMON_FS;
    extern vector<char> COMMON_QUOTE;
    extern vector<string> COMMON_NA;    // has to be converted to uppercase before comparison

    enum {
        TYPE_NUMERIC = 0,
        TYPE_STRING = 1
    };

    enum {
        EOL_UNIX = 0,
        EOL_DOS = 1
    };

    struct Field {
        string name;
        int type;                // numeric / nominal / string
        bool quoted;
        //vector<string> values;   // values for nominal fields
    };

    struct FieldExt {
        int missing;             // missing count
        string na;               // N/A string
        unordered_map<string, int> popular;
        double p5, p25, p50, p75, p95;
    };

    class crange: public ::boost::iterator_range<const char*> {
        bool na;
    public:
        crange (): na(false) {
        }
        crange (string const &str): ::boost::iterator_range<const char *>(&str[0], &str[0] + str.size()), na(false) {
        }
        crange (char const *begin, char const *end) : ::boost::iterator_range<const char*>(begin, end), na(false) {
        }
        crange (char const *begin, char const *end, bool na_) : ::boost::iterator_range<const char*>(begin, end), na(na_) {
        }

        bool missing () const {
            return na;
        }
    };

    struct Format {
    private:
        void trainField (vector<crange> const &, Field *, FieldExt *);
    public:
        int eol_type;            // end of line types, could be multiple chars
        string eol_str;
        char fs_char;            // field separator
        char quote_char;
        string na_str;           // N/A string
        bool has_header;
        bool header_quoted;
        //bool hasRowId;           // first column is row ID, header missing the row ID column.
        unsigned prelude;
        size_t data_offset;
        size_t max_line;
        vector<Field> fields;
        void load (string const &path);
        void save (string const &path);
        void train (string const &path, size_t max_buffer = 0x6400000); // 100MB
        void summary (ostream &os, bool details) const;
        // whether parse successful
        bool parse (crange in, vector<crange> *out) const;
        void write_header (ostream &os) const;
        void write_line (ostream &os, vector<crange> const &) const;
    };
}
