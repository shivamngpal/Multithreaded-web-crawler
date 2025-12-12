// src/util/UrlUtils.cpp
#include "UrlUtils.h"
#include <algorithm>
#include <cctype>
#include <cstring>

std::string toLowerCopy(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
    return out;
}

std::string extractHost(const std::string &url)
{
    size_t schemeEnd = url.find("://");
    size_t hostStart = (schemeEnd == std::string::npos) ? 0 : schemeEnd + 3;
    size_t hostEnd = url.find('/', hostStart);
    if (hostEnd == std::string::npos)
    {
        return url.substr(hostStart);
    }
    return url.substr(hostStart, hostEnd - hostStart);
}

bool isInAllowedDomain(const std::string &url, const std::string &allowedDomain)
{
    std::string host = extractHost(url);
    return toLowerCopy(host) == toLowerCopy(allowedDomain);
}

size_t write_callback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
    if (!userp)
        return 0;
    userp->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

std::string resolveUrl(const std::string &baseUrl, const std::string &newUrl)
{
    if (newUrl.empty() || newUrl == "(null)")
        return "";
    if (newUrl.rfind("http://", 0) == 0 || newUrl.rfind("https://", 0) == 0)
        return newUrl;
    if (newUrl.rfind("//", 0) == 0)
    {
        // inherit https if base uses https, else http
        if (baseUrl.rfind("https://", 0) == 0)
            return "https:" + newUrl;
        return "http:" + newUrl;
    }
    // root-relative
    if (newUrl[0] == '/')
    {
        // find origin (scheme://host)
        size_t schemeEnd = baseUrl.find("://");
        if (schemeEnd == std::string::npos)
            return "";
        size_t hostStart = schemeEnd + 3;
        size_t hostEnd = baseUrl.find('/', hostStart);
        std::string origin = (hostEnd == std::string::npos) ? baseUrl : baseUrl.substr(0, hostEnd);
        return origin + newUrl;
    }
    // other relative (simple): prepend origin + '/'
    size_t schemeEnd = baseUrl.find("://");
    if (schemeEnd == std::string::npos)
        return "";
    size_t hostStart = schemeEnd + 3;
    size_t hostEnd = baseUrl.find('/', hostStart);
    std::string origin = (hostEnd == std::string::npos) ? baseUrl : baseUrl.substr(0, hostEnd);
    return origin + "/" + newUrl;
}
