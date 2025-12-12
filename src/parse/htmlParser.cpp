// src/parse/HtmlParser.cpp
#include "HtmlParser.h"
#include "../util/UrlUtils.h"
#include <string>
#include <vector>
#include <gumbo.h>

// Recursively find <a href="..."> and push resolved URLs into outLinks
static void searchForLinksNode(GumboNode *node, const std::string &baseUrl, std::vector<std::string> &outLinks)
{
    if (!node || node->type != GUMBO_NODE_ELEMENT)
        return;

    if (node->v.element.tag == GUMBO_TAG_A)
    {
        GumboAttribute *href = gumbo_get_attribute(&node->v.element.attributes, "href");
        if (href && href->value)
        {
            std::string rawValue(href->value);
            std::string resolved = resolveUrl(baseUrl, rawValue);
            if (!resolved.empty())
                outLinks.push_back(resolved);
        }
    }

    GumboVector &children = node->v.element.children;
    for (unsigned int i = 0; i < children.length; ++i)
    {
        searchForLinksNode(static_cast<GumboNode *>(children.data[i]), baseUrl, outLinks);
    }
}

void extractLinksFromGumbo(GumboNode *htmlRoot, const std::string &baseUrl, std::vector<std::string> &outLinks)
{
    if (!htmlRoot)
        return;
    searchForLinksNode(htmlRoot, baseUrl, outLinks);
}

// Title extraction (same logic as before)
static std::string extractTitleNode(GumboNode *node)
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
                if (child->v.text.text)
                    title += std::string(child->v.text.text);
            }
            else if (child->type == GUMBO_NODE_ELEMENT)
            {
                title += extractTitleNode(child);
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
            std::string found = extractTitleNode(child);
            if (!found.empty())
                return found;
        }
    }
    return "";
}

std::string extractTitleFromGumboOutput(GumboOutput *output)
{
    if (!output || !output->root)
        return "";
    std::string raw = extractTitleNode(output->root);
    // trim
    size_t a = 0;
    while (a < raw.size() && isspace((unsigned char)raw[a]))
        ++a;
    size_t b = raw.size();
    while (b > a && isspace((unsigned char)raw[b - 1]))
        --b;
    return raw.substr(a, b - a);
}
