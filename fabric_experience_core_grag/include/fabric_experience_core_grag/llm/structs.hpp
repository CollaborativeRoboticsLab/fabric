#pragma once

#include <string>
#include <vector>

namespace fabric::experience::llm
{

struct EmbeddingResult
{
    std::string text;
    std::vector<float> embedding;
};

struct EntityInfo
{
    std::string name;
    std::string type;
    std::string description;
};

struct RelationshipInfo
{
    std::string source;
    std::string target;
    std::string relationship;
};

}  // namespace fabric::experience::llm
