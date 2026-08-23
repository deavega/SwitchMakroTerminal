// http.hpp - a tiny blocking HTTPS GET wrapper around libcurl.
#pragma once
#include <string>

namespace http {

struct Response {
    bool ok = false;
    long status = 0;
    std::string body;
    std::string error;
};

// Initialize/teardown curl globally (call once at startup / shutdown).
void globalInit();
void globalCleanup();

// Perform a blocking HTTPS GET. Thread-safe: uses its own easy handle.
// If romfs:/cacert.pem exists it is used for verification; otherwise
// peer verification is disabled (fine for a read-only public dashboard).
// If cookieFile is non-empty, libcurl reads/writes cookies there, so a
// sequence of requests can share a session (needed for Yahoo's crumb flow).
Response get(const std::string& url, long timeoutMs = 8000,
             const std::string& cookieFile = "");

} // namespace http
