// Loose-file directory source. Matches game paths case-insensitively onto
// whatever the directory on disk actually contains.
#include <dirent.h>
#include <sys/stat.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "as3d/vfs.h"

namespace as3d {
namespace {

class DirSource : public IFileSource {
public:
    explicit DirSource(std::unordered_map<std::string, std::string> files)
        : files_(std::move(files)) {}

    bool exists(const std::string& path) override {
        return files_.find(path) != files_.end();
    }

    bool read(const std::string& path, Blob& out) override {
        auto it = files_.find(path);
        if (it == files_.end()) return false;
        std::unique_ptr<IStream> s = openFileStream(it->second);
        if (!s) return false;
        out.resize(s->size());
        return out.empty() || s->readAt(0, out.data(), out.size());
    }

    void list(std::vector<std::string>& out) override {
        out.reserve(out.size() + files_.size());
        for (const auto& kv : files_) out.push_back(kv.first);
    }

private:
    std::unordered_map<std::string, std::string> files_;
};

void scanDir(const std::string& osDir, const std::string& relPrefix,
             std::unordered_map<std::string, std::string>& out) {
    DIR* d = opendir(osDir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string osPath = osDir + "/" + name;
        std::string rel = relPrefix.empty() ? name : relPrefix + "/" + name;

        struct stat st{};
        if (stat(osPath.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scanDir(osPath, rel, out);
        } else if (S_ISREG(st.st_mode)) {
            out[normalizePath(rel)] = osPath;
        }
    }
    closedir(d);
}

} // namespace

std::unique_ptr<IFileSource> makeDirSource(const std::string& osDir) {
    std::unordered_map<std::string, std::string> files;
    scanDir(osDir, "", files);
    return std::unique_ptr<IFileSource>(new DirSource(std::move(files)));
}

} // namespace as3d
