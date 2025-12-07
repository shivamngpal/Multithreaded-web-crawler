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

using namespace std;

// shared resources
SafeQueue<string> urlFrontier; // stores the links to visit
unordered_set<string> visited; // stores already visited strings
// we are using unordered_set because its very fast to look up visited links
mutex visitedMutex;

// const int MAX_THREADS = thread::hardware_concurrency() > 0 ? thread::hardware_concurrency() : 4;
atomic<int> pagesCrawled{0};
const int MAX_PAGES = 200;

string getApiEndpoint(){
    const char* env_ptr = getenv("CRAWLER_API_URL");
    if(env_ptr == nullptr)
        return "http://localhost:5000/api/pages";
    
    string env_url = env_ptr;
    if(env_url.empty())
        return "http://localhost:5000/api/pages";

    return env_url;
}
// This is a callback function that libcurl uses.
// libcurl gives data in bytes
// When libcurl downloads data, it gives it to us in chunks through this function.
size_t write_callback(void *contents, size_t size, size_t nmemb, string* userp)
{
    // Append the new data to the string that 'userp' points to
    userp->append((char *)contents, size * nmemb);
    // Return the number of bytes we handled
    return size * nmemb;
}

string resolveUrl(const string &baseUrl, const string &newUrl)
{
    if(newUrl.empty() || newUrl == "(null)")
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

void searchForLinks(GumboNode *node, const string &baseUrl, vector<string> &extractedLinks)
{
    if (node->type != GUMBO_NODE_ELEMENT)
        return;

    if (node->v.element.tag == GUMBO_TAG_A)
    {
        GumboAttribute *href = gumbo_get_attribute(&node->v.element.attributes, "href");
        if (href && href->value){
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

void sendDataToBackend(const string &url, const vector<string>& links){
    CURL *curl = curl_easy_init();
    if (!curl)
        return;

    // 1. Construct JSON manually
    // WARNING: Vulnerable to JSON injection if URLs contain quotes.
    string json = "{ \"url\": \"" + url + "\", \"links\": [";
    for (size_t i = 0; i < links.size(); ++i)
    {
        json += "\"" + links[i] + "\"";
        if (i < links.size() - 1)
            json += ",";
    }
    json += "] }";

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    string api_url = getApiEndpoint();

    curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5s timeout for API pushes

    string response;
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

void crawler_worker(int thread_id)
{
    CURL *curl = curl_easy_init(); // Each thread gets its own CURL handle
    if (!curl)
        return;

    while (true)
    {
        string currentUrl;

        // 1. Get a URL from the SafeQueue (Waits if empty)
        // Note: In a real crawler, we'd need a shutdown signal here.
        if (!urlFrontier.pop(currentUrl))
        {
            break;
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
            GumboOutput *output = gumbo_parse_with_options(&options,html_buffer.c_str(), html_buffer.size());

            vector<string>extractedLinks;
            searchForLinks(output->root, currentUrl,extractedLinks); // Extract new links
            gumbo_destroy_output(&options, output);

            if(!extractedLinks.empty())
                sendDataToBackend(currentUrl,extractedLinks);

            sort(extractedLinks.begin(), extractedLinks.end());
            extractedLinks.erase(unique(extractedLinks.begin(), extractedLinks.end()),extractedLinks.end());

            // Collect only truly new links (not yet visited)
            vector<string> newLinks;
            {
                unique_lock<mutex> lock(visitedMutex);
                for (const string &link : extractedLinks)
                {
                    // visited.insert returns {iterator, bool}; bool==true if newly inserted
                    if (visited.insert(link).second)
                    {
                        newLinks.push_back(link);
                    }
                }
            } // visitedMutex unlocked here

            // Push new links into the frontier outside of visitedMutex lock
            for (const string &link : newLinks)
            {
                urlFrontier.push(link);
            }

            // 5. Update global page count and possibly stop
            int currentCount = ++pagesCrawled;
            cout << "[Thread " << thread_id << "] Indexed: " << currentUrl
                 << " | Links: " << extractedLinks.size()
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

int main()
{
    // 1. Global Init for libcurl
    curl_global_init(CURL_GLOBAL_ALL);

    // 2. Seed the frontier with a safe, small root
    string seed = "http://info.cern.ch";
    urlFrontier.push(seed);
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

    return 0;
    // // Initialize libcurl
    // curl_global_init(CURL_GLOBAL_DEFAULT);

    // const char *seedUrl = "http://info.cern.ch";
    // // pushing very first url in queue and set
    // urlFrontier.push(seedUrl);
    // visited.insert(seedUrl);


    // while (!urlFrontier.empty())
    // {
    //     CURL *curl;
    //     CURLcode res;
    //     std::string html_buffer;

    //     curl = curl_easy_init();

    //     if (curl)
    //     {
    //         // The URL we want to fetch
    //         std::string urlString = urlFrontier.push(); // The world's first website!
    //         urlFronteir.pop();
    //         // convert urlString to c-style string (char array)
    //         const char *url = urlString.c_str();
    //         std::cout << "Attempting to fetch URL: " << url << std::endl;

    //         // Tell libcurl which URL to get
    //         curl_easy_setopt(curl, CURLOPT_URL, url);

    //         // Tell libcurl what function to call when it has data
    //         curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    //         // Tell libcurl what to pass to our callback function
    //         curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html_buffer);

    //         // Perform the request
    //         res = curl_easy_perform(curl);

    //         // Check for errors
    //         if (res != CURLE_OK)
    //         {
    //             std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
    //         }
    //         else
    //         {
    //             std::cout << "\nSuccessfully fetched page!" << std::endl;
    //             std::cout << "-------------------- HTML CONTENT --------------------" << std::endl;
    //             std::cout << html_buffer << std::endl;
    //             std::cout << "----------------------------------------------------" << std::endl;

    //             // parse the fetched html
    //             // gumbo creates a tree structure of HTML5 file
    //             GumboOptions options = kGumboDefaultOptions;
    //             GumboOutput *output = gumbo_parse_with_options(&options, html_buffer.c_str(), html_buffer.length());
    //             searchForLinks(output->root, urlString);
    //             // destroying gumbo object
    //             gumbo_destroy_output(&options, output);
    //         }

    //         // Always cleanup
    //         curl_easy_cleanup(curl);
    //     }
    // }
    // // Global cleanup
    // curl_global_cleanup();

    // return 0;
}