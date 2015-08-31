#include <iostream>
#include <fstream>
#include <unordered_set>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include "csvlint.h"

using namespace std;
using namespace boost;
namespace po = boost::program_options; 

ostream &write_sql_string (ostream &os, csvlint::crange e) {
    os.put('\'');
    for (char const *i = e.begin(); i != e.end(); ++i) {
        char c = *i;
        if (c == '\'') {
            os.put('\'');
        }
        os.put(*i);
    }
    os.put('\'');
    return os;
}

int main (int argc, char *argv[]) {
    unsigned guess_size;
    string table_name;
    bool use_column_name;
    
    string input_path;
    string output_path;
    po::options_description desc_visible("General options");
    desc_visible.add_options()
    ("help,h", "produce help message.")
    ("input,I", po::value(&input_path), "input path")
    ("output,O", po::value(&output_path), "output path")
    ("table,t", po::value(&table_name)->default_value("data"), "")
    ("use-column-name", "")
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

    use_column_name = vm.count("use_column_name") > 0;

    csvlint::Format fmt;
    fmt.train(input_path, guess_size * 1024 * 1024);
    csvlint::Format tofmt = fmt;

    ofstream os(output_path);
    // write header
    os << "begin transaction;" << endl;
    os << "create table " << table_name << '(';

    for (unsigned i = 0; i < fmt.fields.size(); ++i) {
        if (i) os << ", ";
        auto const &field = fmt.fields[i];
        if (field.name.size() && use_column_name) {
            if (field.name.size() > 2
                && field.name.front() == fmt.quote_char
                && field.name.back() == fmt.quote_char) {
                os << field.name.substr(1, field.name.size()-2);
            }
            else {
                os << field.name;
            }
        }
        else {
            os << "c" << i;
        }
        if (field.type == csvlint::TYPE_NUMERIC) {
            os << " real";
        }
        else {
            os << " text";
        }
    }
    os << ");" << endl;

    ifstream is(input_path.c_str());
    is.seekg(fmt.data_offset);
    BOOST_VERIFY(is);
    string line;
    vector<csvlint::crange> cols;
    while (getline(is, line)) {
        fmt.parse(csvlint::crange(line), &cols);
        os << "insert into " << table_name << " values(";
        for (unsigned i = 0; i < fmt.fields.size(); ++i) {
            if (i) os << ", ";
            auto const &field = fmt.fields[i];
            auto e = cols[i];
            if (e.missing()) {
                os << "null";
            }
            else if (field.type == csvlint::TYPE_NUMERIC) {
                os << e;
            }
            else {
                write_sql_string(os, e);
            }
        }
        os << ");" << endl;
    }
    os << "commit;" << endl;

    return 0;
}

