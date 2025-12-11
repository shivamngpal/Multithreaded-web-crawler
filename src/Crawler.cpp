#include "Crawler.h"
#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <algorithm> // std::sort, std::unique
#include <cstdlib>   // std::getenv
#include <atomic>    // std::atomic

#include <curl/curl.h>
#include <gumbo.h>

#include "SafeQueue.h" // Updated SafeQueue with stop()
#include <cstring>
#include<cctype>

using namespace std;

struct UrlTask
{
    string url;
    int depth;
};

// shared resources
SafeQueue<UrlTask> urlFrontier; // stores the links to visit
unordered_set<string> visited; // stores already visited strings
// we are using unordered_set because its very fast to look up visited links
mutex visitedMutex;

// const int MAX_THREADS = thread::hardware_concurrency() > 0 ? thread::hardware_concurrency() : 4;
atomic<int> pagesCrawled{0};
const int MAX_PAGES = 200;
const int MAX_DEPTH = 3;
// crawl config
const string ALLOWED_DOMAIN = "info.cern.ch"; // only crawl this domain

string getApiEndpoint()
{
    // getenv returns c-style string,i.e., char array
    const char *env_url = getenv("CRAWLER_API_URL");
    // env_url can be null if we dont have any thing in .env file or if no url is given to CRAWLER_API_URL in .env
    if (env_url == nullptr || strlen(env_url) == 0)
        return "http://localhost:5000/api/pages";

    return string(env_url);
}

// Escape minimal set of chars for JSON string values
static string jsonEscape(const std::string &s)
{
    string out;
    out.reserve(s.size() + 16);
    for (char c : s)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '\"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            // control characters (0x00–0x1F) -> \u00XX
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
                out += buf;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

string toLowerCopy(const string &s)
{
    string out = s;
    transform(out.begin(), out.end(), out.begin(),
              [](unsigned char c)
              { return tolower(c); });
    return out;
}

// Extract host from "scheme://host/path..."
string extractHost(const string &url)
{
    // find "://"
    size_t schemeEnd = url.find("://");
    size_t hostStart = (schemeEnd == string::npos) ? 0 : schemeEnd + 3;
    size_t hostEnd = url.find('/', hostStart);

    if (hostEnd == string::npos)
    {
        // no "/" after host
        return url.substr(hostStart);
    }
    return url.substr(hostStart, hostEnd - hostStart);
}

// Check if a URL belongs to ALLOWED_DOMAIN (case-insensitive)
bool isInAllowedDomain(const string &url)
{
    string host = extractHost(url);
    return toLowerCopy(host) == toLowerCopy(ALLOWED_DOMAIN);
}

// This is a callback function that libcurl uses.
// libcurl gives data in bytes
// When libcurl downloads data, it gives it to us in chunks through this function.
size_t write_callback(void *contents, size_t size, size_t nmemb, string *userp)
{
    // Append the new data to the string that 'userp' points to
    userp->append((char *)contents, size * nmemb);
    // Return the number of bytes we handled
    return size * nmemb;
}

string resolveUrl(const string &baseUrl, const string &newUrl)
{
    // gumbo return "(null)" if href attribute is present but no link is given to it
    if (newUrl.empty() || newUrl == "(null)")
        return "";
    else if (newUrl.find("https") == 0 || newUrl.find("http") == 0)
        return newUrl;
    else if (newUrl.find('/') == 0)
    {
        string absolute_url = baseUrl + newUrl;
        return absolute_url;
    }
    else if (newUrl.find("//") == 0)
        return ("https:" + newUrl);
    else
        return "";
}

// find first <title> node and collect its text children (robust to multiple text nodes)
static string extractTitleFromNode(GumboNode *node)
{
    if (!node || node->type != GUMBO_NODE_ELEMENT)
        return "";

    if (node->v.element.tag == GUMBO_TAG_TITLE)
    {
        string title;
        GumboVector *children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i)
        {
            GumboNode *child = static_cast<GumboNode *>(children->data[i]);
            if (child->type == GUMBO_NODE_TEXT || child->type == GUMBO_NODE_WHITESPACE)
            {
                if (child->type == GUMBO_NODE_TEXT && child->v.text.text)
                {
                    title += string(child->v.text.text);
                }
            }
            else if (child->type == GUMBO_NODE_ELEMENT)
            {
                // In some malformed pages, title text can be nested — collect recursively
                const string nested = extractTitleFromNode(child);
                if (!nested.empty())
                    title += nested;
            }
        }
        return title;
    }

    GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i)
    {
        GumboNode *child = static_cast<GumboNode *>(children->data[i]);
        if (child->type == GUMBO_NODE_ELEMENT)
        {
            string found = extractTitleFromNode(child);
            if (!found.empty())
                return found;
        }
    }
    return "";
}

// Public wrapper: parse HTML and return a trimmed title (or empty string)
static string extractTitleFromHtml(const string &html)
{
    if (html.empty())
        return "";
    GumboOutput *output = gumbo_parse(html.c_str());
    std::string title = extractTitleFromNode(output->root);
    // trim leading/trailing whitespace
    size_t start = 0;
    while (start < title.size() && isspace((unsigned char)title[start]))
        ++start;
    size_t end = title.size();
    while (end > start && isspace((unsigned char)title[end - 1]))
        --end;
    string trimmed = title.substr(start, end - start);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return trimmed;
}

void searchForLinks(GumboNode *node, const string &baseUrl, vector<string> &extractedLinks)
{
    if (node->type != GUMBO_NODE_ELEMENT)
        return;

    if (node->v.element.tag == GUMBO_TAG_A)
    {
        GumboAttribute *href = gumbo_get_attribute(&node->v.element.attributes, "href");
        if (href && href->value)
        {
            string rawValue = href->value;
            string newUrl = resolveUrl(baseUrl, rawValue);
            if (!newUrl.empty())
            {
                extractedLinks.push_back(newUrl);
            }
        }
    }

    GumboVector &children = node->v.element.children;
    for (unsigned int i = 0; i < children.length; ++i)
    {
        searchForLinks(static_cast<GumboNode *>(children.data[i]), baseUrl, extractedLinks);
    }
}

void sendDataToBackend(const std::string &url, const std::string &title, const std::vector<std::string> &links)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return;

    std::string escapedUrl = jsonEscape(url);
    std::string escapedTitle = jsonEscape(title);

    // Build JSON safely (manual but escaped)
    std::string json = "{ \"url\": \"" + escapedUrl + "\", \"title\": \"" + escapedTitle + "\", \"links\": [";
    for (size_t i = 0; i < links.size(); ++i)
    {
        json += "\"" + jsonEscape(links[i]) + "\"";
        if (i + 1 < links.size())
            json += ",";
    }
    json += "] }";

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string api_url = getApiEndpoint();

    curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        std::cerr << "API Error: " << curl_easy_strerror(res) << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void crawler_worker(int thread_id)
{
    CURL *curl = curl_easy_init(); // Each thread gets its own CURL handle
    if (!curl)
        return;

    while (true)
    {
        UrlTask task;
        // 1. Get a URL from the SafeQueue (Waits if empty)
        if (!urlFrontier.pop(task))
        {
            break;
        }

        std::string currentUrl = task.url;
        int currentDepth = task.depth;

        // Safety: if somehow depth exceeded, skip
        if (currentDepth > MAX_DEPTH)
        {
            continue;
        }

        // 🔒 DOMAIN RESTRICTION: skip anything outside ALLOWED_DOMAIN
        if (!isInAllowedDomain(currentUrl))
        {
            cerr << "[Thread " << thread_id << "] Skipping (outside domain): "
                 << currentUrl << endl;
            continue;
        }

        // std::cout << "[Thread " << thread_id << "] Crawling: " << currentUrl << std::endl;

        string html_buffer;
        curl_easy_setopt(curl, CURLOPT_URL, currentUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html_buffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);       // 10 second timeout per page
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
                         "ShivamCrawler/1.0 (Student Project)");

        this_thread::sleep_for(chrono::milliseconds(200));
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            // 2. Parse HTML
            GumboOptions options = kGumboDefaultOptions;
            GumboOutput *output = gumbo_parse_with_options(&options, html_buffer.c_str(), html_buffer.size());

            vector<string> extractedLinks;
            searchForLinks(output->root, currentUrl, extractedLinks); // Extract new links
                                                                      // Extract title from raw HTML buffer (more reliable) OR from Gumbo tree
            string title = extractTitleFromHtml(html_buffer);

            gumbo_destroy_output(&options, output);

            // 3. Report to Backend (always include title even if no links)
            sendDataToBackend(currentUrl, title, extractedLinks);

            sort(extractedLinks.begin(), extractedLinks.end());
            // erase all the duplicate elements
            // unique elements are present at start of vector and unique returns iterator at the end of unique ele
            // erase from end of unique to last of extractedLinks vector.
            extractedLinks.erase(unique(extractedLinks.begin(), extractedLinks.end()), extractedLinks.end());

            // Collect only truly new links (not yet visited)
            vector<UrlTask> newTasks;
            {
                unique_lock<mutex> lock(visitedMutex);
                for (const string &link : extractedLinks)
                {
                    // depth limit: only schedule if next depth is within MAX_DEPTH
                    if (currentDepth + 1 > MAX_DEPTH)
                        continue;

                    // 🌐 DOMAIN RESTRICTION: only schedule URLs from ALLOWED_DOMAIN
                    if (!isInAllowedDomain(link))
                        continue;

                    if (visited.insert(link).second)
                    {
                        newTasks.push_back(UrlTask{link, currentDepth + 1});
                    }
                }
            } // visitedMutex unlocked

            for (const auto &t : newTasks)
            {
                urlFrontier.push(t);
            }

            // 5. Update global page count and possibly stop
            int currentCount = ++pagesCrawled;
            cout << "[Thread " << thread_id << "] Indexed: " << currentUrl
                 << " | Depth: " << currentDepth
                 << " | Links: " << extractedLinks.size()
                 << " | Title: \"" << title << "\""
                 << " | Total pages: " << currentCount << endl;

            if (currentCount >= MAX_PAGES)
            {
                // Signal all threads that we are done
                urlFrontier.stop();
                break; // This thread can exit now
            }
        }
        else
        {
            cerr << "[Thread " << thread_id << "] Failed: " << currentUrl
                 << " - " << curl_easy_strerror(res) << endl;
        }
    }

    curl_easy_cleanup(curl);
}

void runCrawler(){
    // 1. Global Init for libcurl
    curl_global_init(CURL_GLOBAL_ALL);

    // 2. Seed the frontier with a safe, small root
    string seed = "http://info.cern.ch";
    UrlTask seedTask{seed, 0};
    urlFrontier.push(seedTask);
    visited.insert(seed);

    cout << "Starting Production Crawler..." << endl;
    cout << "API Target: " << getApiEndpoint() << endl;

    // 3. Determine thread count dynamically
    unsigned int cores = thread::hardware_concurrency();
    int num_threads = (cores == 0) ? 5 : static_cast<int>(cores);

    cout << "Detected " << cores << " cores. Spawning "
         << num_threads << " threads." << endl;

    // 4. Spawn workers
    vector<thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(crawler_worker, i);
    }

    // 5. Wait for all workers to finish
    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }

    // 6. Cleanup
    curl_global_cleanup();
    cout << "Crawler finished. Total pages crawled: "
         << pagesCrawled.load() << endl;
}

