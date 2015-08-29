#include <iostream>
#include <fstream>
#include <unordered_set>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include "csvlint.h"
#include "bigtext.h"

using namespace std;
using namespace boost;
namespace po = boost::program_options; 

struct State {
    vector<csvlint::crange> cols;
    size_t good;
    size_t total;
};

int main (int argc, char *argv[]) {
    unsigned guess_size;
    
    string input_path;
    string output_path;
    po::options_description desc_visible("General options");
    desc_visible.add_options()
    ("help,h", "produce help message.")
    ("input,I", po::value(&input_path), "input path")
    (",M", po::value(&guess_size)->default_value(100), "")
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

    csvlint::Format fmt;
    fmt.train(input_path, guess_size * 1024 * 1024);

    /*
    size_t max_chunk = fmt.max_line * 100;
    size_t constexpr MIN_CHUNK = 12 * 1024 * 1024;
    if (max_chunk < MIN_CHUNK) max_chunk = MIN_CHUNK;
    */

    BigText<State> text(input_path, fmt.data_offset, '\n', fmt.max_line);

    for (auto &state: text) {
        state.total = state.good = 0;
    }

    text.lines ([&fmt](char const *begin, char const *end, State *st, size_t i_in_block) {
        bool r = fmt.parse(csvlint::crange(begin, end), &st->cols);
        if (r) {
            ++st->good;
        }
        ++st->total;
    });

    size_t total = 0, good = 0;
    for (auto const &state: text) {
        total += state.total;
        good += state.good;
    }
    cout << total << " total lines." << endl;
    cout << good << " good lines." << endl;




    return 0;
}
