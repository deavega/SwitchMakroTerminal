#include "net/http.hpp"
#include <curl/curl.h>
#include <sys/stat.h>
#include <string>

namespace http {

static bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

void globalInit()    { curl_global_init(CURL_GLOBAL_DEFAULT); }
void globalCleanup() { curl_global_cleanup(); }

Response get(const std::string& url, long timeoutMs, const std::string& cookieFile) {
    Response r;
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "curl_easy_init failed";
        return r;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   // gzip/deflate if built in

    // Cookie engine (shared session for Yahoo's crumb flow).
    if (!cookieFile.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookieFile.c_str()); // read
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR,  cookieFile.c_str()); // write
    }

    // MENGUBAH USER-AGENT MENJADI BROWSER STANDAR (Windows Chrome)
    // Yahoo sangat sensitif terhadap bot/scraper.
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36");

    // TLS verification: prefer a bundled CA, else disable (read-only public data).
    if (fileExists("romfs:/cacert.pem")) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        r.error = curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
        r.ok = (r.status >= 200 && r.status < 300);
        if (!r.ok) r.error = "HTTP status " + std::to_string(r.status);
    }

    curl_easy_cleanup(curl);
    return r;
}

} // namespace http