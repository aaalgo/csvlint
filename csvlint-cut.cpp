#include <iostream>
#include <fstream>
#include <unordered_set>
#include <boost/format.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#include "csvlint.h"

using namespace std;
using namespace boost;
namespace po = boost::program_options; 

int main (int argc, char *argv[]) {
    char quote_char;
    char fs_char;
    unsigned guess_size;
    vector<string> fields;
    
    string input_path;
    string output_path;
    po::options_description desc_visible("General options");
    desc_visible.add_options()
    ("help,h", "produce help message.")
    ("input,I", po::value(&input_path), "input path")
    ("output,O", po::value(&output_path), "output path")
    (",M", po::value(&guess_size)->default_value(10), "")
    ("fs", po::value(&fs_char), "")
    ("quote", po::value(&quote_char),"")
    ("field,f", po::value(&fields), "")
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
    boost::log::add_console_log(cerr);
    {
        vector<string> fs;
        for (auto const &f: fields) {
            using namespace boost::algorithm;
            vector<string> ss;
            split(ss, f, is_any_of(","), token_compress_on);
            for (string const &s: ss) {
                fs.push_back(s);
            }
        }
        fields.swap(fs);
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

    {
        tofmt.fields.clear();
        map<string, unsigned> lookup;
        for (unsigned i = 0; i < fmt.fields.size(); ++i) {
            lookup[fmt.fields[i].name] = i;
        }
        for (string const &s: fields) {
            auto it = lookup.find(s);
            if (it == lookup.end()) {
                cerr << "Field " << s << " not found." << endl;
                return 1;
            }
            tofmt.fields.push_back(fmt.fields[it->second]);
        }
    }

    ofstream os_file;
    if (output_path.size()) {
        os_file.open(output_path.c_str());
    }
    ostream &os = output_path.size() ? os_file : cout;
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

