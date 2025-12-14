// src/parse/HtmlParser.h
#pragma once
#include <string>
#include <vector>
#include <gumbo.h>

// Extract all absolute/normalized links found in `htmlRoot` (Gumbo tree) using baseUrl to resolve.
// Returns links appended to outLinks.
void extractLinksFromGumbo(GumboNode *htmlRoot, const std::string &baseUrl, std::vector<std::string> &outLinks);

// Extract <title> text from GumboOutput (reads tree rooted at output->root)
std::string extractTitleFromGumboOutput(GumboOutput *output);