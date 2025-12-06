#include <iostream>
#include <string>
#include <curl/curl.h> // The header for libcurl
#include <gumbo.h>
#include <queue>
#include <unordered_set>
#include <thread>
#include <mutex>
#include "../src/SafeQueue.h"

using namespace std;

// shared resources
SafeQueue<string> urlFrontier; // stores the links to visit
unordered_set<string> visited; // stores already visited strings
// we are using unordered_set because its very fast to look up visited links
mutex visitedMutex;

const int MAX_THREADS = 5;

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

string resolveUrl(string &baseUrl, string &newUrl)
{
    if (newUrl.find("https") == 0 || newUrl.find("http") == 0)
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

void searchForLinks(GumboNode *node, string &baseUrl)
{
    if (node->type != GUMBO_NODE_ELEMENT)
        return;

    GumboElement &element = node->v.element;

    // we have found an anchor tag
    // now check if its hyperlink exists or not
    if (element.tag == GUMBO_TAG_A)
    {
        GumboAttribute *href = gumbo_get_attribute(&element.attributes, "href");
        // anchor tag has href
        // so we have found hyperlink successfully
        // if found url is not visited
        // add it in urlFronteir queue to process it
        // also add in visited set because if we dont do this
        // and find the same link from different webpage then we will push same link multiple times in queue
        // which is wrong

        if (href)
        {
            // this function resolveUrl() makes the invalid urls -> valid
            string newUrl = href->value;
            string absolute_url = resolveUrl(baseUrl, newUrl);

            if (!absolute_url.empty()){
                unique_lock<mutex>lock(visitedMutex);
                if (visited.find(absolute_url) == visited.end()){
                    visited.insert(absolute_url);
                    lock.unlock();
                    urlFrontier.push(absolute_url); 
                    cout << "Found Link : " << absolute_url << endl;
                }
            }
        }
    }

    // Recursively search this element's children
    GumboVector &children = element.children;
    for (unsigned int i = 0; i < children.length; i++)
    {
        searchForLinks(static_cast<GumboNode *>(children.data[i]), baseUrl);
    }
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

        this_thread::sleep_for(chrono::milliseconds(200));
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            // 2. Parse HTML
            GumboOptions options = kGumboDefaultOptions;
            GumboOutput *output = gumbo_parse(html_buffer.c_str());
            searchForLinks(output->root, currentUrl); // Extract new links
            gumbo_destroy_output(&options, output);

            cout << "[Thread " << thread_id << "] Finished: " << currentUrl << endl;
        }
        else
        {
            cerr << "[Thread " << thread_id << "] Failed: " << currentUrl << endl;
        }
    }

    curl_easy_cleanup(curl);
}

int main()
{
    // 1. Initialization
    curl_global_init(CURL_GLOBAL_ALL);

    // 2. Seed the Frontier
    string seed = "http://info.cern.ch"; // A larger site for testing
    urlFrontier.push(seed);
    visited.insert(seed);

    cout << "Starting Crawler with " << MAX_THREADS << " threads..." << std::endl;

    // 3. Spawn Worker Threads
    // emplace_back constructs an object in-place at the end of a vector, rather than creating it elsewhere and copying it over.
    vector<thread> threads;
    for (int i = 0; i < MAX_THREADS; ++i)
    {
        threads.emplace_back(crawler_worker, i);
    }

    // 4. Wait for threads (In this infinite crawler, they technically never join)
    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }

    curl_global_cleanup();
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