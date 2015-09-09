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
    vector<float> floats;
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

// percentiles must be previously sorted
void percentiles (vector<float> &all, vector<float> &ps) {
    vector<size_t> offs(ps.size());
    for (unsigned i = 0; i < offs.size(); ++i) {
        offs[i] = round(ps[i] * all.size());
        if (offs[i] >= all.size()) {
            offs[i] = all.size() - 1;
        }
        if (i) {
            BOOST_VERIFY(offs[i] > offs[i-1]);
        }
    }
    size_t last = 0;
    for (unsigned i = 0; i < offs.size(); ++i) {
        nth_element(all.begin() + last, all.begin() + offs[i], all.end());
        ps[i] = all[offs[i]];
        last = offs[i];
    }
}

void stat_number (csvlint::Field const &f,  vector<Chunk> const &chunks, string *out) {
    vector<float> all(good(chunks));
    unsigned col = f.column;
    unsigned off = 0;
    size_t missing = 0;
    Acc acc;
    for (auto const &ch: chunks) {
        for (float e: ch.data[col].floats) {
            all[off++] = e;
            acc(e);
        }
        missing += ch.data[col].missing;
    }
    all.resize(off);
    vector<float> ps{0, 0.25, 0.5, 0.75, 1.0};
    percentiles(all, ps);
    ostringstream ss;
    ss << 'N' << f.column <<':' << f.name 
        << ',' << missing
        << ',' << all.size()
        << ',' << ba::mean(acc)
        << ',' << sqrt(ba::variance(acc));
    for (auto v: ps) {
        ss << ',' << v;
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
    vector<pair<size_t,string>> sz(cnts.size());
    {
        size_t o = 0;
        for (auto const &p: cnts) {
            sz[o].first = p.second;
            sz[o].second = p.first;
            ++o;
        }
    }
    sort(sz.begin(), sz.end());
    reverse(sz.begin(), sz.end());
    ostringstream ss;
    ss << C << f.column << ':' << f.name << ",NA:" << missing << ",VALUES:" << sz.size();
    if (sz.size() > topk) sz.resize(topk);
    for (auto const &e: sz) {
        ss << ",'" << e.second << "':" << e.first;
    }
    if (cnts.size() > sz.size()) {
        ss << ",...";
    }
    *out = ss.str();
}

void stat_column (csvlint::Field const &f, vector<Chunk> const &chunks, string *out) {
    if (f.type == csvlint::TYPE_NUMERIC) {
        string o1, o2;
        stat_number(f, chunks, &o1);
        stat_string(f, chunks, &o2, 'I');
        o1.push_back('\n');
        *out = o1 + o2;
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
                auto const &field = fmt.fields[i];
                csvlint::crange e = cols[i];
                if (e.missing()) {
                    ++data.missing;
                }
                else {
                    if (field.type == csvlint::TYPE_NUMERIC) {
                        float v;
                        qi::parse(e.begin(), e.end(), qi::float_, v);
                        data.floats.push_back(v);
                    }
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
        cout << st << endl;
    }

    return 0;
}
