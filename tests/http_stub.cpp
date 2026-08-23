// Host-only stub so yahoo.cpp links without libcurl during unit tests.
#include "net/http.hpp"
namespace http {
void globalInit() {}
void globalCleanup() {}
Response get(const std::string&, long, const std::string&) {
    Response r; r.ok = false; r.error = "stub"; return r;
}
} // namespace http
