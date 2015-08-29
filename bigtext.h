#ifndef AAALGO_BIGTEXT
#define AAALGO_BIGTEXT

#include <errno.h>
#include <string.h>
#include <iostream>
#include <functional>
#include <vector>
#include <string>
#include <utility>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <boost/progress.hpp>
#include <boost/assert.hpp>
#include <fcntl.h>
#ifdef USE_OPENMP
#include <omp.h>
#endif

template <typename T = void>
class BigText: public std::vector<T> {
    size_t offset;
    int file;
    char delimiter;
    size_t max_line;
    size_t chunk_size;
    size_t total_size;
    size_t chunks;
    std::vector<std::pair<size_t, size_t>> check;

public:
    static char const DEFAULT_DELIMITER = '\n';
    static size_t const DEFAULT_MAX_LINE = 4096;
    static size_t const DEFAULT_MIN_CHUNK = 1 * 1024 * 1024;
    static size_t const DEFAULT_MAX_CHUNK = 128 * 1024 * 1024;

    BigText (std::string const &path,
             size_t off = 0,
             char del = DEFAULT_DELIMITER,
             size_t ml = DEFAULT_MAX_LINE,
             size_t ch = DEFAULT_MAX_CHUNK): offset(off), delimiter(del), max_line(ml), chunk_size(ch) {
        file = open(path.c_str(), O_RDONLY);
        BOOST_VERIFY(file >= 0);
        struct stat st;
        int r = fstat(file, &st);
        BOOST_VERIFY(r == 0);
        total_size = st.st_size; // - offset;

        if (chunk_size < DEFAULT_MIN_CHUNK) chunk_size = DEFAULT_MIN_CHUNK;
        BOOST_VERIFY(max_line < chunk_size);
        chunks = (total_size - offset + chunk_size - 1) / chunk_size;
        this->resize(chunks);
        check.resize(chunks);
    }

    ~BigText () {
        close(file);
    }

    void blocks (std::function<void(char const *, char const *, T *)> cb) {
        boost::progress_display progress(chunks, std::cerr);
#ifdef USE_OPENMP
        std::vector<std::string> bufs(omp_get_max_threads());
#pragma omp parallel for
#else
        std::string buf;
#endif
        for (size_t i = 0; i < chunks; ++i) {
#ifdef USE_OPENMP
            std::string &buf = bufs[omp_get_thread_num()];
#endif
            size_t sz = chunk_size + max_line;
            buf.resize(sz+1);
            off_t off = offset + i * chunk_size;
            ssize_t rsz = pread(file, &buf[0], sz, off);
            if (rsz < 0) {
                std::cerr << "pread(" << file << ',' << "..." << ',' << sz << ',' << offset + i * chunk_size << ')' << std::endl;
                std::cerr << strerror(errno) << std::endl;
                BOOST_VERIFY(0);
            }
            sz = rsz;
            size_t begin = 0;
            if (i) {
                while ((begin < sz) && (buf[begin] != delimiter)) ++begin;
                ++begin;
                BOOST_VERIFY(begin < max_line);
            }
            size_t end = chunk_size;
            if (sz < chunk_size) {
                end = sz;
            }
            else {
                while ((end < sz) && (buf[end] != delimiter)) ++end;
                if (end < sz) ++end;
            }
            check[i] = std::make_pair(off + begin, off + end);
            cb(&buf[begin], &buf[end], &this->at(i));
#ifdef USE_OPENMP
#pragma omp critical
#endif
            ++progress;
        }
        BOOST_VERIFY(check[0].first == offset);
        for (unsigned i = 1; i < chunks; ++i) {
            BOOST_VERIFY(check[i].first == check[i-1].second);
        }
        BOOST_VERIFY(check.back().second == total_size);
    }

    void lines (std::function<void(char const *, char const *, T *, size_t)> cb) {
        blocks([this, cb](char const *b, char const *e, T *state) {
                size_t n = 0;
                while (b < e) {
                    char const *le = b;
                    while (le < e && le[0] != delimiter) {
                        ++le;
                    }
                    if (le < e) {
                        ++le;
                    }
                    cb(b, le, state, n);
                    ++n;
                    b = le;
                }
        });
    }
};


#endif

