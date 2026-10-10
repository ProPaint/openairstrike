#include "as3d/vfs.h"

#include <algorithm>

#include "as3d/load_stats.h"

namespace as3d {

void Vfs::mount(std::unique_ptr<IFileSource> source) {
    if (source) sources_.push_back(std::move(source));
}

bool Vfs::exists(const std::string& path) {
    const std::string p = normalizePath(path);
    for (auto it = sources_.rbegin(); it != sources_.rend(); ++it) {
        if ((*it)->exists(p)) return true;
    }
    return false;
}

bool Vfs::read(const std::string& path, Blob& out) {
    const std::string p = normalizePath(path);
    for (auto it = sources_.rbegin(); it != sources_.rend(); ++it) {
        if ((*it)->read(p, out)) {
            LoadStats::fileRead(out.size(), p.c_str());
            if (observer_) observer_(p);
            return true;
        }
    }
    return false;
}

size_t Vfs::prefetch(const std::vector<std::string>& paths) {
    // Each path to the source that serves it (the last mounted one that has it).
    std::vector<std::vector<std::string>> per(sources_.size());
    for (const std::string& path : paths) {
        const std::string p = normalizePath(path);
        for (size_t i = sources_.size(); i-- > 0;) {
            if (sources_[i]->exists(p)) {
                per[i].push_back(p);
                break;
            }
        }
    }
    size_t bytes = 0;
    for (size_t i = 0; i < sources_.size(); ++i)
        if (!per[i].empty()) bytes += sources_[i]->prefetch(per[i]);
    return bytes;
}

void Vfs::dropPrefetched(const std::vector<std::string>* paths) {
    if (!paths) {
        for (auto& s : sources_) s->dropPrefetched(nullptr);
        return;
    }
    std::vector<std::string> norm;
    norm.reserve(paths->size());
    for (const std::string& p : *paths) norm.push_back(normalizePath(p));
    for (auto& s : sources_) s->dropPrefetched(&norm);
}

std::vector<std::string> Vfs::list(const std::string& prefix) {
    const std::string p = normalizePath(prefix);
    std::vector<std::string> all;
    for (auto& src : sources_) {
        std::vector<std::string> names;
        src->list(names);
        for (auto& n : names) {
            if (n.compare(0, p.size(), p) == 0) all.push_back(std::move(n));
        }
    }
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    return all;
}

} // namespace as3d
