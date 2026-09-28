#include "as3d/vfs.h"

#include <algorithm>

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
        if ((*it)->read(p, out)) return true;
    }
    return false;
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
