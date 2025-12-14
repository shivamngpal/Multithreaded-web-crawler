// src/Crawler.cpp
#include "../include/Crawler.hpp"

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
#include <cstring>
#include <cctype>

#include <curl/curl.h>
#include <gumbo.h>

#include "../include/SafeQueue.hpp"        // SafeQueue template
#include "util/UrlUtils.h"    // resolveUrl, isInAllowedDomain, write_callback, etc.
#include "parse/HtmlParser.h" // extractLinksFromGumbo, extractTitleFromGumboOutput

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace std;

// Simple URL task with depth
struct UrlTask
{
    string url;
    int depth;
};

// Shared resources (crawler state)
SafeQueue<UrlTask> urlFrontier;
unordered_set<string> visited;
mutex visitedMutex;

atomic<int> pagesCrawled{0};
atomic<int> activeWorkers{0};

const int MAX_PAGES = 200;
const int MAX_DEPTH = 3;
const string ALLOWED_DOMAIN = "info.cern.ch"; // change if you want a different domain

// Get API endpoint from environment or default
string getApiEndpoint()
{
    const char *env_url = std::getenv("CRAWLER_API_URL");
    if (!env_url || std::strlen(env_url) == 0)
    {
        return "http://localhost:5000/api/pages";
    }
    return string(env_url);
}

// Send crawled data to backend using nlohmann::json
void sendDataToBackend(const std::string &url, const std::string &title, const std::vector<std::string> &links)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return;

    json payload;
    payload["url"] = url;
    payload["title"] = title;
    payload["links"] = links;

    std::string jsonStr = payload.dump();

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string api_url = getApiEndpoint();

    curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    // write_callback is provided by util/UrlUtils.cpp
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        cerr << "API Error: " << curl_easy_strerror(res) << endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

// Worker thread function
void crawler_worker(int thread_id)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return;

    while (true)
    {
        UrlTask task;

        // Pop a task (blocks until available or stopped)
        if (!urlFrontier.pop(task))
        {
            break; // queue was stopped and empty
        }

        // Mark worker active for this task
        ++activeWorkers;

        const string currentUrl = task.url;
        const int currentDepth = task.depth;

        // Depth guard
        if (currentDepth > MAX_DEPTH)
        {
            int stillActive = --activeWorkers;
            if (stillActive == 0 && urlFrontier.empty())
                urlFrontier.stop();
            continue;
        }

        // Domain restriction (uses util/isInAllowedDomain which accepts url and compares to ALLOWED_DOMAIN)
        if (!isInAllowedDomain(currentUrl, ALLOWED_DOMAIN))
        {
            cerr << "[Thread " << thread_id << "] Skipping (outside domain): " << currentUrl << endl;
            int stillActive = --activeWorkers;
            if (stillActive == 0 && urlFrontier.empty())
                urlFrontier.stop();
            continue;
        }

        // Prepare libcurl
        string html_buffer;
        curl_easy_setopt(curl, CURLOPT_URL, currentUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html_buffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "ShivamCrawler/1.0 (Student Project)");

        // Politeness
        this_thread::sleep_for(chrono::milliseconds(200));

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK)
        {
            // Parse HTML with Gumbo
            GumboOptions options = kGumboDefaultOptions;
            GumboOutput *output = gumbo_parse_with_options(&options, html_buffer.c_str(), html_buffer.size());

            // Extract links using parser module (already resolves relative -> absolute)
            vector<string> extractedLinks;
            extractLinksFromGumbo(output->root, currentUrl, extractedLinks);

            // Extract title directly from GumboOutput (no reparse)
            string title = extractTitleFromGumboOutput(output);

            gumbo_destroy_output(&options, output);

            // Report to backend (title + links)
            sendDataToBackend(currentUrl, title, extractedLinks);

            // Dedupe links found on this page
            sort(extractedLinks.begin(), extractedLinks.end());
            extractedLinks.erase(unique(extractedLinks.begin(), extractedLinks.end()), extractedLinks.end());

            // Schedule new tasks (depth, domain, visited checks)
            vector<UrlTask> newTasks;
            {
                unique_lock<mutex> lock(visitedMutex);
                for (const string &link : extractedLinks)
                {
                    if (currentDepth + 1 > MAX_DEPTH)
                        continue;
                    if (!isInAllowedDomain(link, ALLOWED_DOMAIN))
                        continue;
                    if (visited.insert(link).second)
                    {
                        newTasks.push_back(UrlTask{link, currentDepth + 1});
                    }
                }
            } // unlock visited

            for (const auto &t : newTasks)
                urlFrontier.push(t);

            // Increment counters and log
            int currentCount = ++pagesCrawled;
            cout << "[Thread " << thread_id << "] Indexed: " << currentUrl
                 << " | Depth: " << currentDepth
                 << " | New tasks: " << newTasks.size()
                 << " | Title: \"" << title << "\""
                 << " | Total pages: " << currentCount << endl;

            // If we reached max pages, stop everything
            if (currentCount >= MAX_PAGES)
            {
                urlFrontier.stop();
                int stillActive = --activeWorkers;
                break;
            }

            // Finish task: decrement activeWorkers and possibly stop the queue if nobody's left
            int stillActive = --activeWorkers;
            if (stillActive == 0 && urlFrontier.empty())
            {
                urlFrontier.stop();
                break;
            }
        }
        else
        {
            // Fetch failed
            cerr << "[Thread " << thread_id << "] Failed: " << currentUrl
                 << " - " << curl_easy_strerror(res) << endl;

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

// Public entrypoint called from main
void runCrawler()
{
    // Global init
    curl_global_init(CURL_GLOBAL_ALL);

    // Seed
    string seed = "http://info.cern.ch";
    UrlTask seedTask{seed, 0};
    urlFrontier.push(seedTask);
    visited.insert(seed);

    cout << "Starting Production Crawler..." << endl;
    cout << "API Target: " << getApiEndpoint() << endl;
    cout << "Allowed domain: " << ALLOWED_DOMAIN << " | Max depth: " << MAX_DEPTH << " | Max pages: " << MAX_PAGES << endl;

    // Spawn threads
    unsigned int cores = thread::hardware_concurrency();
    int num_threads = (cores == 0) ? 5 : static_cast<int>(cores);
    cout << "Detected " << cores << " cores. Spawning " << num_threads << " threads." << endl;

    vector<thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(crawler_worker, i);

    for (auto &t : threads)
        if (t.joinable())
            t.join();

    curl_global_cleanup();

    cout << "Crawler finished. Total pages crawled: " << pagesCrawled.load() << endl;
}
