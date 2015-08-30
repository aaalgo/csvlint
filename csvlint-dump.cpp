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
    char quote_char;
    char fs_char;
    unsigned guess_size;
    
    string input_path;
    string output_path;
    po::options_description desc_visible("General options");
    desc_visible.add_options()
    ("help,h", "produce help message.")
    ("input,I", po::value(&input_path), "input path")
    ("output,O", po::value(&output_path), "output path")
    (",M", po::value(&guess_size)->default_value(100), "")
    ("fs", po::value(&fs_char)->default_value(','), "")
    ("quote", po::value(&quote_char)->default_value('"'),"")
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
    csvlint::Format tofmt = fmt;

    if (vm.count("fs")) {
        tofmt.fs_char = fs_char;
    }
    if (vm.count("quote")) {
        tofmt.quote_char = quote_char;
    }

    ofstream os(output_path);
    tofmt.write_header(os);
    ifstream is(input_path);
    string line;
    is.seekg(fmt.data_offset);
    vector<csvlint::crange> cols;
    while (getline(is, line)) {
        fmt.parse(csvlint::crange(line), &cols);
        tofmt.write_line(os, cols);
    }

    return 0;
}

