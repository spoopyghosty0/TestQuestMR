#include "mrlog.h"

#include <chrono>
#include <cstdio>

#include "log.h"

MrLog &MrLog::get() {
    static MrLog log;
    return log;
}

double MrLog::clock() const {
    using namespace std::chrono;
    double now = duration<double>(steady_clock::now().time_since_epoch()).count();
    return start_ < 0 ? 0 : now - start_;
}

void MrLog::open(const std::string &path) {
    path_ = path;
    std::rename(path.c_str(), (path.substr(0, path.size() - 4) + ".prev.txt").c_str());
    file_ = std::fopen(path.c_str(), "w");
    if (!file_) LOGW("can't write %s", path.c_str());
    // what was logged before the file existed
    for (const LogLine &l : lines_)
        if (file_) std::fprintf((FILE *)file_, "%8.3f %s%s\n", l.t, l.level == 2 ? "ERR " : l.level ? "WARN " : "", l.text.c_str());
    if (file_) std::fflush((FILE *)file_);
}

void MrLog::add(int level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    addv(level, fmt, ap);
    va_end(ap);
}

void MrLog::addv(int level, const char *fmt, va_list ap) {
    if (start_ < 0) {
        using namespace std::chrono;
        start_ = duration<double>(steady_clock::now().time_since_epoch()).count();
    }
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    double t = clock();
    if (level == 2) LOGE("%s", buf);
    else if (level == 1) LOGW("%s", buf);
    else LOGI("%s", buf);
    lines_.push_back({t, level, buf});
    while (lines_.size() > 600) lines_.pop_front();
    ++total_;
    if (level == 2) ++errors_;
    if (file_) {
        std::fprintf((FILE *)file_, "%8.3f %s%s\n", t, level == 2 ? "ERR " : level ? "WARN " : "", buf);
        std::fflush((FILE *)file_);
    }
}
