// The MR call log: every call, its result and every event, kept for the panel's Log tab, sent to logcat (tag TQMR)
// and appended to mr-log.txt in the app's files folder (the previous run's log is kept as mr-log.prev.txt).
#pragma once

#include <cstdarg>
#include <deque>
#include <string>

struct LogLine {
    double t;   // seconds since the app started
    int level;  // 0 info, 1 warning (unexpected answer), 2 error
    std::string text;
};

class MrLog {
public:
    static MrLog &get();
    void open(const std::string &path);  // starts the file (rotating the old one)
    void add(int level, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
    void addv(int level, const char *fmt, va_list ap);
    const std::deque<LogLine> &lines() const { return lines_; }
    int total() const { return total_; }
    int errors() const { return errors_; }
    double clock() const;
    std::string path() const { return path_; }

private:
    std::deque<LogLine> lines_;
    int total_ = 0, errors_ = 0;
    std::string path_;
    void *file_ = nullptr;
    double start_ = -1;
};

#define MLOG(...) MrLog::get().add(0, __VA_ARGS__)
#define MWARN(...) MrLog::get().add(1, __VA_ARGS__)
#define MERR(...) MrLog::get().add(2, __VA_ARGS__)
