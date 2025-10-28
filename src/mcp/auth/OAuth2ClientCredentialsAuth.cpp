#include <string>
#include <sstream>
#include <vector>
#include <mutex>
#include <chrono>
#include <boost/asio/awaitable.hpp>
#include <boost/beast/http.hpp>

#include "mcp/auth/OAuth2ClientCredentialsAuth.hpp"

using namespace std::chrono;

namespace mcp::auth {

static std::string urlEncodeForm(const std::string& s) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << static_cast<char>(c);
        } else if (c == ' ') {
            oss << '+';
        } else {
            oss << '%';
            const char* hex = "0123456789ABCDEF";
            oss << hex[(c >> 4) & 0xFu] << hex[c & 0xFu];
        }
    }
    return oss.str();
}

static bool parseJsonStringField(const std::string& json, const std::string& key, std::string& out) {
    out.clear();
    std::string needle = std::string("\"") + key + std::string("\"");
    std::size_t k = json.find(needle);
    if (k == std::string::npos) {
        return false;
    }
    std::size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    std::size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) {
        return false;
    }
    std::size_t q2 = q1 + 1;
    while (q2 < json.size()) {
        if (json[q2] == '"' && json[q2 - 1] != '\\') {
            break;
        }
        ++q2;
    }
    if (q2 >= json.size()) {
        return false;
    }
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

static bool parseJsonIntField(const std::string& json, const std::string& key, int& out) {
    std::string needle = std::string("\"") + key + std::string("\"");
    std::size_t k = json.find(needle);
    if (k == std::string::npos) {
        return false;
    }
    std::size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    std::size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        ++i;
    }
    std::size_t j = i;
    while (j < json.size() && ((json[j] >= '0' && json[j] <= '9') || json[j] == '-')) {
        ++j;
    }
    try {
        out = std::stoi(json.substr(i, j - i));
        return true;
    } catch (...) {
        return false;
    }
}

OAuth2ClientCredentialsAuth::OAuth2ClientCredentialsAuth(
    std::string tokenUrl,
    std::string clientId,
    std::string clientSecret,
    std::string scope,
    unsigned int tokenRefreshSkewSeconds,
    unsigned int connectTimeoutMs,
    unsigned int readTimeoutMs,
    std::string caFile,
    std::string caPath)
    : tokenUrl(std::move(tokenUrl)),
      clientId(std::move(clientId)),
      clientSecret(std::move(clientSecret)),
      scope(std::move(scope)),
      tokenRefreshSkewSeconds(tokenRefreshSkewSeconds),
      connectTimeoutMs(connectTimeoutMs),
      readTimeoutMs(readTimeoutMs),
      caFile(std::move(caFile)),
      caPath(std::move(caPath)) {}

boost::asio::awaitable<void> OAuth2ClientCredentialsAuth::ensureReady() {
    {
        std::lock_guard<std::mutex> lk(mtx);
        auto now = steady_clock::now();
        if (!cachedAccessToken.empty() && (now + seconds(tokenRefreshSkewSeconds) < tokenExpiry)) {
            co_return;
        }
    }

    std::ostringstream form;
    form << "grant_type=client_credentials";
    if (!clientId.empty()) { form << "&client_id=" << urlEncodeForm(clientId); }
    if (!clientSecret.empty()) { form << "&client_secret=" << urlEncodeForm(clientSecret); }
    if (!scope.empty()) { form << "&scope=" << urlEncodeForm(scope); }

    TokenFetchParams params;
    params.url = tokenUrl;
    params.serverName = std::string();
    params.caFile = caFile;
    params.caPath = caPath;
    params.connectTimeoutMs = connectTimeoutMs;
    params.readTimeoutMs = readTimeoutMs;

    std::string resp = co_await coPostFormUrlencoded(params, form.str(), nullptr, onError);
    if (!resp.empty()) {
        std::string at;
        int expiresIn = 0;
        bool ok1 = parseJsonStringField(resp, std::string("access_token"), at);
        bool ok2 = parseJsonIntField(resp, std::string("expires_in"), expiresIn);
        if (ok1) {
            std::lock_guard<std::mutex> lk(mtx);
            cachedAccessToken = at;
            if (ok2 && expiresIn > 0) {
                tokenExpiry = steady_clock::now() + seconds(static_cast<unsigned int>(expiresIn));
            } else {
                tokenExpiry = steady_clock::now() + seconds(3600);
            }
            co_return;
        }
        if (onError) {
            onError(std::string("OAuth2: token endpoint response missing access_token"));
        }
        co_return;
    } else {
        if (onError) {
            onError(std::string("OAuth2: empty response from token endpoint"));
        }
    }
    co_return;
}

std::vector<HeaderKV> OAuth2ClientCredentialsAuth::headers() const {
    std::lock_guard<std::mutex> lk(mtx);
    if (cachedAccessToken.empty()) return {};
    return { HeaderKV{ "Authorization", std::string("Bearer ") + cachedAccessToken } };
}

void OAuth2ClientCredentialsAuth::setErrorHandler(std::function<void(const std::string&)> fn) {
    onError = std::move(fn);
}

} // namespace mcp::auth
