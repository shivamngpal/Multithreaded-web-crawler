// src/util/UrlUtils.h
#pragma once
#include <string>

std::string toLowerCopy(const std::string &s);
std::string extractHost(const std::string &url);
bool isInAllowedDomain(const std::string &url, const std::string &allowedDomain);
std::string resolveUrl(const std::string &baseUrl, const std::string &newUrl);

// libcurl write callback signature (exposed so HttpClient or other files can reuse)
size_t write_callback(void *contents, size_t size, size_t nmemb, std::string *userp);
