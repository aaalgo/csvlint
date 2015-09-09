#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <boost/format.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/progress.hpp>
#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#define LOG(x) BOOST_LOG_TRIVIAL(x)
#include "csvlint.h"
#define USE_OPENMP 1
#include "bigtext.h"

using namespace std;
using namespace boost;
namespace po = boost::program_options; 
namespace ba = boost::accumulators;
namespace qi = boost::spirit::qi;
typedef ba::accumulator_set<double, ba::stats<ba::tag::mean, ba::tag::variance>> Acc;

size_t topk = 20;

struct Column {
    vector<csvlint::crange> strings;
    size_t missing;
};

struct Chunk {
    vector<string> lines;
    vector<csvlint::crange> cols;
    vector<Column> data;
    size_t total;
};

size_t total (vector<Chunk> const &chunks) {
    size_t v = 0;
    for (auto const &ch: chunks) {
        v += ch.total;
    }
    return v;
}

size_t good (vector<Chunk> const &chunks) {
    size_t v = 0;
    for (auto const &ch: chunks) {
        v += ch.data.size();
    }
    return v;
}

unsigned c999 (string const &s) {
    unsigned i = 0; 
    while (i < s.size() && s[i] == '9') {
        ++i;
    }
    return i;
}

void stat_number (csvlint::Field const &f,  vector<Chunk> const &chunks, string *out) {
    unordered_map<string, size_t> cnts;
    unsigned col = f.column;
    size_t missing = 0;
    unsigned max_len = 0;
    for (auto const &ch: chunks) {
        for (auto e: ch.data[col].strings) {
            ++cnts[string(e.begin(), e.end())];
            unsigned sz = e.end() - e.begin();
            if (sz > max_len) {
                max_len = sz;
            }
        }
        missing += ch.data[col].missing;
    }
    if ((missing > 0) && (cnts.size() <= 1)) return;
    if ((missing == 0) && (cnts.size() <= 2)) return;
    vector<string> special;
    
    {
        string minus;
        for (auto const &e: cnts) {
            if (e.first.empty()) continue;
            if (e.first[0] == '-') {
                if (minus.size()) goto pass;
                minus = e.first;
            }
        }
        if (minus.size()) {
            special.push_back(minus);
        }
pass:;
    }

    {
        for (auto const &e: cnts) {
            if (e.first.empty()) continue;
            unsigned c = c999(e.first);
            if (c < 2) continue;
            if (c + 1 < max_len) continue;
            if (e.first.size() < max_len) continue;
            special.push_back(e.first);
        }
    }
    if (special.empty() && missing == 0) return;
    ostringstream ss;
    if (missing) {
        ss << f.name << "\tNA" << endl;
    }
    for (string const &s: special) {
        ss << f.name << '\t' << s << endl;
    }
    *out = ss.str();
}

void stat_string (csvlint::Field const &f, vector<Chunk> const &chunks, string *out, char C='S') {
    unordered_map<string, size_t> cnts;
    unsigned col = f.column;
    size_t missing = 0;
    for (auto const &ch: chunks) {
        for (auto e: ch.data[col].strings) {
            ++cnts[string(e.begin(), e.end())];
        }
        missing += ch.data[col].missing;
    }
    if ((missing > 0) && (cnts.size() <= 1)) return;
    if ((missing == 0) && (cnts.size() <= 2)) return;
    int sp = 0;
    ostringstream ss;
    size_t n = cnts[""];
    if (n> 0) {
        ss << f.name << "\t\"\"" << endl;
        ++sp;
    }
    n = cnts["-1"];
    if (n> 0) {
        ss << f.name << "\t\"-1\"" << endl;
        ++sp;
    }
    n = cnts["NA"];
    if (n> 0) {
        ss << f.name << "\t\"NA\"" << endl;
        ++sp;
    }
    n = cnts["N/A"];
    if (n> 0) {
        ss << f.name << "\t\"N/A\"" << endl;
        ++sp;
    }
    if (sp) {
        *out = ss.str();
    }
}

void stat_column (csvlint::Field const &f, vector<Chunk> const &chunks, string *out) {
    if (f.type == csvlint::TYPE_NUMERIC) {
        stat_number(f, chunks, out);
    }
    else if (f.type == csvlint::TYPE_STRING) {
        stat_string(f, chunks, out);
    }
    else BOOST_VERIFY(0);
}

int main (int argc, char *argv[]) {
    unsigned guess_size;
    
    string input_path;
    string output_path;
    po::options_description desc_visible("General options");
    desc_visible.add_options()
    ("help,h", "produce help message.")
    ("input,I", po::value(&input_path), "input path")
    (",M", po::value(&guess_size)->default_value(10), "")
    ("topk", po::value(&topk)->default_value(topk), "")
    ;
    po::options_description desc("Allowed options");
    desc.add(desc_visible);

    po::positional_options_description p;
    p.add("input", 1);

    po::variables_map vm; 
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm); 

    if (vm.count("help") || vm.count("input") == 0) {
        cout << "Usage: csvlint-stat [OTHER OPTIONS]... <data>" << endl;
        cout << desc_visible << endl;
        return 0;
    }

    boost::log::add_console_log(cerr);

    csvlint::Format fmt;
    fmt.train(input_path, guess_size * 1024 * 1024);

    cerr << "Parsing text..." << endl;
    BigText<Chunk> text(input_path, fmt.data_offset, '\n', fmt.max_line, 10 * 1024*1024);

    for (auto &ch: text) {
        ch.total = 0;
        ch.data.resize(fmt.fields.size());
        for (auto &v: ch.data) {
            v.missing = 0;
        }
    }
    text.lines ([&fmt](char const *begin, char const *end, Chunk *ch, size_t i_in_block) {
        auto &cols = ch->cols;
        ch->lines.emplace_back(begin, end);
        bool r = fmt.parse(csvlint::crange(std::ref(ch->lines.back())), &cols);
        if (r) {
            for (unsigned i = 0; i < fmt.fields.size(); ++i) {
                Column &data = ch->data[i];
                csvlint::crange e = cols[i];
                if (e.missing()) {
                    ++data.missing;
                }
                else {
                    data.strings.push_back(e);
                }
            }
        }
        ++ch->total;
    });

    cerr << total(text) << " total lines." << endl;
    cerr << good(text) << " good lines." << endl;

    vector<string> stats(fmt.fields.size());

    cerr << "Counting numbers..." << endl;
    progress_display progress(fmt.fields.size(), cerr);
#pragma omp parallel for
    for (unsigned i = 0; i < fmt.fields.size(); ++i) {
        stat_column(fmt.fields[i], text, &stats[i]);
#pragma omp critical
        ++progress;
    }

    for (auto &st: stats) {
        if (st.empty()) continue;
        cout << st;
    }

    return 0;
}
