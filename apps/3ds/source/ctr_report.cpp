#include "ctr_report.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "ctr_perf.h"
#include "run_config.h"
#include "soak_report.h"

namespace ctrapp {
namespace {

constexpr size_t kSocBufferSize = 0x100000; // libctru's soc service buffer (1 MB, 4 KB aligned)
constexpr int kConnectTimeoutMs = 3000;

bool sendAll(int sock, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t n = send(sock, p, size > 0x4000 ? 0x4000 : size, 0);
        if (n <= 0) return false;
        p += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

long fileSize(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fclose(f);
    return n;
}

// One file: its header, then its bytes. The size is taken first; a file that changes meanwhile is
// padded or cut to it, so the framing always holds.
bool sendFile(int sock, const std::string& name, uint64_t* bytes) {
    const std::string path = std::string(kDataDir) + "/" + name;
    const long size = fileSize(path);
    if (size < 0) return true; // gone: skipped
    const std::string head = reportFileHeader(name, static_cast<uint64_t>(size));
    if (!sendAll(sock, head.data(), head.size())) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    std::vector<char> buf(0x10000);
    long left = size;
    while (left > 0) {
        size_t want = static_cast<size_t>(left) < buf.size() ? static_cast<size_t>(left) : buf.size();
        size_t got = f ? std::fread(buf.data(), 1, want, f) : 0;
        if (got < want) std::fill(buf.begin() + static_cast<long>(got), buf.begin() + static_cast<long>(want), 0);
        if (!sendAll(sock, buf.data(), want)) {
            if (f) std::fclose(f);
            return false;
        }
        left -= static_cast<long>(want);
    }
    if (f) std::fclose(f);
    *bytes += head.size() + static_cast<uint64_t>(size);
    return true;
}

// A non-blocking connect with a timeout; the socket is blocking again afterwards. -1 on failure
// (*why says how).
int connectTo(const std::string& host, int port, std::string* why) {
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_aton(host.c_str(), &addr.sin_addr) == 0) {
        *why = "bad address";
        return -1;
    }
    const int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        *why = "socket() failed, errno " + std::to_string(errno);
        return -1;
    }
    const int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (rc != 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY)) {
        pollfd pfd = {};
        pfd.fd = sock;
        pfd.events = POLLOUT;
        rc = poll(&pfd, 1, kConnectTimeoutMs);
        if (rc == 0) {
            *why = "no answer within 3 s";
            close(sock);
            return -1;
        }
        // The console's socket service does not answer SO_ERROR as Linux does (the first console
        // run got -26 from it on a connection the PC had accepted): a peer address is the proof
        // of a connection, the poll's error bits the proof of a failure.
        sockaddr_in peer = {};
        socklen_t len = sizeof peer;
        if (rc < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) ||
            getpeername(sock, reinterpret_cast<sockaddr*>(&peer), &len) != 0) {
            *why = "connection refused or unreachable (errno " + std::to_string(errno) + ", revents " +
                   std::to_string(static_cast<int>(pfd.revents)) + ")";
            close(sock);
            return -1;
        }
    } else if (rc != 0) {
        *why = "connect() failed, errno " + std::to_string(errno);
        close(sock);
        return -1;
    }
    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
    return sock;
}

} // namespace

bool pushReport(const std::string& reportTo) {
    std::string host;
    int port = 0;
    if (!parseHostPort(reportTo, &host, &port)) return false;
    Perf log; // the log of this launch (appended)
    log.open(false);
    const uint64_t t0 = nowTicks();
    void* socBuffer = memalign(0x1000, kSocBufferSize);
    if (!socBuffer) {
        AS3D_WARN("AS3D_REPORT to=%s not sent: no memory for the soc buffer; the files stay on the SD card", reportTo.c_str());
        log.close();
        return false;
    }
    const Result rc = socInit(static_cast<u32*>(socBuffer), kSocBufferSize);
    if (R_FAILED(rc)) {
        AS3D_WARN("AS3D_REPORT to=%s not sent: socInit failed (0x%08lx); the files stay on the SD card", reportTo.c_str(),
                  static_cast<unsigned long>(rc));
        free(socBuffer);
        log.close();
        return false;
    }
    std::string why;
    const int sock = connectTo(host, port, &why);
    bool ok = false;
    if (sock < 0) {
        AS3D_WARN("AS3D_REPORT to=%s not sent: %s; the files stay on the SD card (ftpd: tools/3ds_fetch_logs.sh)",
                  reportTo.c_str(), why.c_str());
    } else {
        // log.txt last, so that it holds this line and everything before the push.
        // Only what this launch wrote (noteRunFile): an older soak.txt or perf.csv stays behind.
        std::vector<std::string> names;
        for (const std::string& n : runFiles())
            if (n != "log.txt" && isReportFile(n) && fileSize(std::string(kDataDir) + "/" + n) >= 0) names.push_back(n);
        names.push_back("log.txt");
        AS3D_INFO("AS3D_REPORT to=%s connected in %.0f ms, sending %d files", reportTo.c_str(), ticksToMs(nowTicks() - t0),
                  static_cast<int>(names.size()));
        log.flush();
        uint64_t bytes = 0;
        ok = sendAll(sock, kReportMagic, 6);
        for (const std::string& n : names) {
            if (!ok) break;
            ok = sendFile(sock, n, &bytes);
        }
        ok = ok && sendAll(sock, kReportEnd, 4);
        shutdown(sock, SHUT_WR);
        close(sock);
        if (ok)
            AS3D_INFO("AS3D_REPORT to=%s sent %d files, %llu bytes in %.0f ms", reportTo.c_str(), static_cast<int>(names.size()),
                      static_cast<unsigned long long>(bytes), ticksToMs(nowTicks() - t0));
        else
            AS3D_WARN("AS3D_REPORT to=%s broken after %llu bytes (errno %d); the files stay on the SD card", reportTo.c_str(),
                      static_cast<unsigned long long>(bytes), errno);
    }
    socExit();
    free(socBuffer);
    log.close();
    return ok;
}

} // namespace ctrapp
