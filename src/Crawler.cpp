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
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

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
atomic<int> activeWorkers{0};
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
// Recursively search for a <title> node and return concatenated text children
static std::string extractTitleFromNode(GumboNode *node)
{
    if (!node || node->type != GUMBO_NODE_ELEMENT)
        return "";

    if (node->v.element.tag == GUMBO_TAG_TITLE)
    {
        std::string title;
        GumboVector *children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i)
        {
            GumboNode *child = static_cast<GumboNode *>(children->data[i]);
            if (child->type == GUMBO_NODE_TEXT || child->type == GUMBO_NODE_WHITESPACE)
            {
                if (child->type == GUMBO_NODE_TEXT && child->v.text.text)
                {
                    title += std::string(child->v.text.text);
                }
            }
            else if (child->type == GUMBO_NODE_ELEMENT)
            {
                // Nested nodes (rare) — collect their text recursively
                title += extractTitleFromNode(child);
            }
        }
        return title;
    }

    // search children
    GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i)
    {
        GumboNode *child = static_cast<GumboNode *>(children->data[i]);
        if (child->type == GUMBO_NODE_ELEMENT)
        {
            std::string found = extractTitleFromNode(child);
            if (!found.empty())
                return found;
        }
    }
    return "";
}

// Wrapper: get title from GumboOutput root; trim whitespace
static std::string extractTitleFromGumboOutput(GumboOutput *output)
{
    if (!output || !output->root)
        return "";
    std::string raw = extractTitleFromNode(output->root);
    // trim
    size_t start = 0;
    while (start < raw.size() && isspace((unsigned char)raw[start]))
        ++start;
    size_t end = raw.size();
    while (end > start && isspace((unsigned char)raw[end - 1]))
        --end;
    return raw.substr(start, end - start);
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

    // Build JSON using nlohmann::json
    json payload;
    payload["url"] = url;
    payload["title"] = title;
    payload["links"] = links;

    std::string jsonStr = payload.dump(); // compact representation

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string api_url = getApiEndpoint();

    curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
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
    CURL *curl = curl_easy_init();
    if (!curl)
        return;

    while (true)
    {
        UrlTask task;

        // Wait for a task; returns false if queue was stopped and empty
        if (!urlFrontier.pop(task))
        {
            break;
        }

        // Mark this worker as active for this task
        ++activeWorkers; // atomic increment

        const std::string currentUrl = task.url;
        const int currentDepth = task.depth;

        // If depth exceeded, finish task and possibly trigger stop
        if (currentDepth > MAX_DEPTH)
        {
            int stillActive = --activeWorkers; // atomic decrement
            if (stillActive == 0 && urlFrontier.empty())
            {
                urlFrontier.stop();
            }
            continue;
        }

        // Domain restriction: skip disallowed domains
        if (!isInAllowedDomain(currentUrl))
        {
            cerr << "[Thread " << thread_id << "] Skipping (outside domain): "
                 << currentUrl << endl;

            int stillActive = --activeWorkers;
            if (stillActive == 0 && urlFrontier.empty())
            {
                urlFrontier.stop();
            }
            continue;
        }

        // Prepare libcurl for fetch
        string html_buffer;
        curl_easy_setopt(curl, CURLOPT_URL, currentUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html_buffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
                         "ShivamCrawler/1.0 (Student Project)");

        // politeness
        this_thread::sleep_for(chrono::milliseconds(200));

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            // PARSE HTML
            GumboOptions options = kGumboDefaultOptions;
            GumboOutput *output = gumbo_parse_with_options(
                &options,
                html_buffer.c_str(),
                html_buffer.size());

            // Extract links from DOM
            vector<string> extractedLinks;
            searchForLinks(output->root, currentUrl, extractedLinks);

            // Extract title from the existing GumboOutput (no reparse)
            std::string title = extractTitleFromGumboOutput(output);

            gumbo_destroy_output(&options, output);

            // REPORT: send url + title + links to backend (even if links empty)
            sendDataToBackend(currentUrl, title, extractedLinks);

            // Dedupe per-page
            sort(extractedLinks.begin(), extractedLinks.end());
            extractedLinks.erase(unique(extractedLinks.begin(), extractedLinks.end()),
                                 extractedLinks.end());

            // Schedule new tasks (depth + domain + visited checks)
            vector<UrlTask> newTasks;
            {
                unique_lock<mutex> lock(visitedMutex);
                for (const string &link : extractedLinks)
                {
                    // depth restriction
                    if (currentDepth + 1 > MAX_DEPTH)
                        continue;
                    // domain restriction (extra safety)
                    if (!isInAllowedDomain(link))
                        continue;

                    if (visited.insert(link).second)
                    {
                        newTasks.push_back(UrlTask{link, currentDepth + 1});
                    }
                }
            } // unlock visitedMutex

            for (const auto &t : newTasks)
            {
                urlFrontier.push(t);
            }

            // Increment global count
            int currentCount = ++pagesCrawled;
            cout << "[Thread " << thread_id << "] Indexed: " << currentUrl
                 << " | Depth: " << currentDepth
                 << " | New tasks: " << newTasks.size()
                 << " | Title: \"" << title << "\""
                 << " | Total pages: " << currentCount << endl;

            // If we reached MAX_PAGES, signal stop and exit task
            if (currentCount >= MAX_PAGES)
            {
                urlFrontier.stop(); // wake all waiting threads
                int stillActive = --activeWorkers;
                break; // exit loop -> cleanup
            }

            // Finished processing this task
            int stillActive = --activeWorkers;
            if (stillActive == 0 && urlFrontier.empty())
            {
                // Last active worker and no queued work -> stop queue so other threads exit
                urlFrontier.stop();
                break;
            }
        }
        else
        {
            // Fetch failed
            cerr << "[Thread " << thread_id << "] Failed: " << currentUrl
                 << " - " << curl_easy_strerror(res) << endl;

            // finished this task, decrement and possibly stop
            int stillActive = --activeWorkers;
            if (stillActive == 0 && urlFrontier.empty())
            {
                urlFrontier.stop();
                break;
            }
        }
    } // while

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

