#include <iostream>
#include <string>
#include <curl/curl.h> // The header for libcurl
#include <gumbo.h>
#include <queue>
#include <unordered_set>

// This is a callback function that libcurl uses.
// When libcurl downloads data, it gives it to us in chunks through this function.
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    // Append the new data to the string that 'userp' points to
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    // Return the number of bytes we handled
    return size * nmemb;
}

std::string resolveUrl(std::string &baseUrl, std::string &newUrl)
{
    if (newUrl.find("https") == 0 || newUrl.find("http") == 0)
        return newUrl;
    else if (newUrl.find('/') == 0)
    {
        std::string absolute_url = baseUrl + newUrl;
        return absolute_url;
    }
    else if (newUrl.find("//") == 0)
        return ("https:" + newUrl);
    else
        return "";
}

void searchForLinks(GumboNode *node, std::string &baseUrl, std::queue<std::string> &urlFronteir, std::unordered_set<std::string> &visited)
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
            std::string newUrl = href->value;
            std::string absolute_url = resolveUrl(baseUrl, newUrl);

            if (!absolute_url.empty() && visited.find(absolute_url) == visited.end())
            {
                urlFronteir.push(absolute_url);
                visited.insert(absolute_url);
                std::cout << "Found Link : " << absolute_url << std::endl;
            }
        }
    }

    // Recursively search this element's children
    GumboVector &children = element.children;
    for (unsigned int i = 0; i < children.length; ++i)
    {
        searchForLinks(static_cast<GumboNode *>(children.data[i]), baseUrl, urlFronteir, visited);
    }
}

int main()
{
    // stores the links to visit
    std::queue<std::string> urlFronteir;
    // stores already visited strings
    // we are using unordered_set because its very fast to look up visited links
    std::unordered_set<std::string> visited;

    const char *seedUrl = "http://info.cern.ch";

    // pushing very first url in queue and set
    urlFronteir.push(seedUrl);
    visited.insert(seedUrl);

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    while (!urlFronteir.empty())
    {
        CURL *curl;
        CURLcode res;
        std::string html_buffer;

        curl = curl_easy_init();

        if (curl)
        {
            // The URL we want to fetch
            std::string urlString = urlFronteir.front(); // The world's first website!
            urlFronteir.pop();
            // convert urlString to c-style string (char array)
            const char *url = urlString.c_str();
            std::cout << "Attempting to fetch URL: " << url << std::endl;

            // Tell libcurl which URL to get
            curl_easy_setopt(curl, CURLOPT_URL, url);

            // Tell libcurl what function to call when it has data
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

            // Tell libcurl what to pass to our callback function
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html_buffer);

            // Perform the request
            res = curl_easy_perform(curl);

            // Check for errors
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            }
            else
            {
                std::cout << "\nSuccessfully fetched page!" << std::endl;
                std::cout << "-------------------- HTML CONTENT --------------------" << std::endl;
                std::cout << html_buffer << std::endl;
                std::cout << "----------------------------------------------------" << std::endl;

                // parse the fetched html
                // gumbo creates a tree structure of HTML5 file
                GumboOptions options = kGumboDefaultOptions;
                GumboOutput *output = gumbo_parse_with_options(&options, html_buffer.c_str(), html_buffer.length());
                searchForLinks(output->root, urlString, urlFronteir, visited);
                // destroying gumbo object
                gumbo_destroy_output(&options, output);
            }

            // Always cleanup
            curl_easy_cleanup(curl);
        }
    }
    // Global cleanup
    curl_global_cleanup();

    return 0;
}