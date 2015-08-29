#include <iostream>
#include <fstream>
#include <unordered_set>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include "csvlint.h"

using namespace std;
using namespace boost;
namespace po = boost::program_options; 

int main (int argc, char *argv[]) {
    unsigned guess_size;
    
    string input_path;
    string output_path;
    po::options_description desc_visible("General options");
    desc_visible.add_options()
    ("help,h", "produce help message.")
    ("input,I", po::value(&input_path), "input path")
    ("output,O", po::value(&output_path), "output path")
    (",M", po::value(&guess_size)->default_value(100), "")
    ;
    po::options_description desc("Allowed options");
    desc.add(desc_visible);

    po::positional_options_description p;
    p.add("input", 1);
    p.add("output", 1);

    po::variables_map vm; 
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm); 

    if (vm.count("help") || vm.count("input") == 0) {
        cout << "Usage: 101 [OTHER OPTIONS]... <data> [test]" << endl;
        cout << desc_visible << endl;
        return 0;
    }

    csvlint::Format fmt;
    fmt.train(input_path, guess_size * 1024 * 1024);
    fmt.summary(cout, true);

#if 0
    Format fmt;
    Guess(input_path, &fmt, guess_lines);

    if (vm.count("output") == 0) return 0;
    ifstream is(input_path.c_str());
    ofstream os(output_path.c_str());
    string line;
    os << "@RELATION " << quote(input_path) << endl;
    for (unsigned i = 0; i < fmt.prelude; ++i) {
        getline(is, line);
        os << '%' << line << endl;
    }
    for (unsigned i = 0; i < fmt.fields.size(); ++i) {
        auto const &field = fmt.fields[i];
        os << "@ATTRIBUTE ";
        if (field.name.size()) {
            os << field.name;
        }
        else {
            os << "column-" << (i+1);
        }
        if (field.type == TYPE_NOMINAL) {
            os << " {";
            bool first = true;
            for (auto const &s: field.values) {
                if (first) {
                    first = false;
                }
                else {
                    os << ',';
                }
                os << s;
            }
            os << "}" << endl;
        }
        else {
            os << " " << TYPE_STRINGS[field.type] << endl;
        }
        float missing = 1.0 * field.missing / fmt.samples;
        if (field.type == TYPE_NUMERIC) {
            os << boost::format("%% NA:%g 5%%:%g 25%%:%g 50%%:%g 75%%:%g 95%%:%g")
                % missing
                % field.__internal.p5
                % field.__internal.p25
                % field.__internal.p50
                % field.__internal.p75
                % field.__internal.p95 << endl;
        }
        else {
            vector<pair<double, string>> rank;
            for (auto const &p: field.__internal.popular) {
                rank.push_back(std::make_pair(p.second, p.first));
            }
            sort(rank.begin(), rank.end());
            reverse(rank.begin(), rank.end());
            os << "%% NA:" << missing;
            if (rank.size() > 10) rank.resize(10);
            unsigned used = field.missing;
            for (auto const &p: rank) {
                os << ' ' << p.second << ':' << 1.0 * p.first /fmt.samples;
                used += p.first;
            }
            if (used < fmt.samples) {
                os << " OTHERS:" << 1.0 * (fmt.samples - used) / fmt.samples;
            }
            os << endl;
        }
    }

    if (vm.count("header")) return 0;


    if (fmt.hasHeader) {
        getline(is, line);
    }

    os << "@DATA" << endl;
    while (getline(is, line)) {
        vector<string> cols;
        char delim = fmt.delim;
        if (line.size() && line.back() == '\r') {
            line.pop_back();
        }
        split(cols, line, [delim](char c){return c == delim;});
        if (cols.size() != fmt.fields.size()) continue;
        for (unsigned i = 0; i < cols.size(); ++i) {
            if (i) os << ',';
            auto const &f = fmt.fields[i];
            string c = unquote(cols[i], fmt.quote);
            if (f.type == TYPE_NUMERIC) {
                if (c == fmt.missing) {
                    os << '?';
                }
                else {
                    os << c;
                }
            }
            else if (f.type == TYPE_NOMINAL) {
                if (c == fmt.missing) {
                    os << '?';
                }
                else {
                    os << quote(c);
                }
            }
            else {
                os << quote(c);
            }
        }
        os << endl;
    }
#endif

    return 0;
}
