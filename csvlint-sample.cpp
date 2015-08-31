#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
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

int main (int argc, char *argv[]) {
    unsigned guess_size;
    float rate;
    unsigned key;
    
    string input_path;
    string output_path;
    po::options_description desc_visible("General options");
    desc_visible.add_options()
    ("help,h", "produce help message.")
    ("input,I", po::value(&input_path), "input path")
    ("output,O", po::value(&output_path), "output path")
    ("rate,r", po::value(&rate)->default_value(0.1), "")
    ("key", po::value(&key), "stratify by this field")
    (",M", po::value(&guess_size)->default_value(10), "")
    ;
    po::options_description desc("Allowed options");
    desc.add(desc_visible);

    po::positional_options_description p;
    p.add("input", 1);
    p.add("output", 1);

    po::variables_map vm; 
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm); 

    if (vm.count("help") || vm.count("input") == 0 || vm.count("output") == 0 || vm.count("key") == 0) {
        cout << "Usage: csvlint-sample [OTHER OPTIONS]... <input> <output>" << endl;
        cout << desc_visible << endl;
        return 0;
    }

    BOOST_VERIFY(rate > 0 && rate < 1);

    boost::log::add_console_log(cerr);

    csvlint::Format fmt;
    fmt.train(input_path, guess_size * 1024 * 1024);
    BOOST_VERIFY(key < fmt.fields.size());

    unordered_map<string, vector<string>> all;

    ifstream is(input_path.c_str());
    is.seekg(fmt.data_offset);
    BOOST_VERIFY(is);
    string line;
    vector<csvlint::crange> cols;
    while (getline(is, line)) {
        if (!fmt.parse(csvlint::crange(std::ref(line)), &cols)) continue;
        csvlint::crange e = cols[key];
        if (e.missing()) continue;
        all[string(e.begin(), e.end())].push_back(line);
    }

    ofstream os(output_path.c_str());

    fmt.write_header(os);
    for (auto &p: all) {
        auto &v = p.second;
        size_t use = round(v.size() * rate);
        if (use == 0) use = 1;
        if (use > v.size()) use = v.size();
        random_shuffle(v.begin(), v.end());
        for (size_t i = 0; i < use; ++i) {
            os.write(&v[i][0], v[i].size());
            os.put('\n');
        }
    }



    return 0;
}
