#pragma once

#include <any>
#include <stdexcept>
#include <string>
#include <vector>

namespace fabric
{

struct options_exception : public std::runtime_error
{
  using std::runtime_error::runtime_error;

  options_exception(const std::string& what) : std::runtime_error(what)
  {
  }

  virtual const char* what() const noexcept override
  {
    return std::runtime_error::what();
  }
};

enum class OptionType
{
  BOOL,
  DOUBLE,
  INT,
  STRING,
  UNCONVERTED,
  VECTOR_BOOL,
  VECTOR_DOUBLE,
  VECTOR_INT,
  VECTOR_STRING
};

struct Parameter
{
  std::string key;
  std::vector<std::string> value;
  OptionType type;

  Parameter() = default;

  std::any get_value()
  {
    switch (type)
    {
      case OptionType::BOOL:
        return value[0] == "true";
      case OptionType::DOUBLE:
        return std::stod(value[0]);
      case OptionType::INT:
        return std::stoi(value[0]);
      case OptionType::STRING:
        return value[0];
      case OptionType::VECTOR_BOOL: {
        std::vector<bool> vec;
        for (const auto& v : value)
          vec.push_back(v == "true");
        return vec;
      }
      case OptionType::VECTOR_DOUBLE: {
        std::vector<double> vec;
        for (const auto& v : value)
          vec.push_back(std::stod(v));
        return vec;
      }
      case OptionType::VECTOR_INT: {
        std::vector<int> vec;
        for (const auto& v : value)
          vec.push_back(std::stoi(v));
        return vec;
      }
      case OptionType::VECTOR_STRING:
        return value;
      default:
        throw options_exception("Unsupported OptionType");
    }
  }

  void set_value(std::string new_key, std::any new_value, OptionType new_type)
  {
    key = new_key;
    type = new_type;

    switch (type)
    {
      case OptionType::BOOL:
        value.clear();
        value.push_back(std::any_cast<bool>(new_value) ? "true" : "false");
        return;
      case OptionType::DOUBLE:
        value.clear();
        value.push_back(std::to_string(std::any_cast<double>(new_value)));
        return;
      case OptionType::INT:
        value.clear();
        value.push_back(std::to_string(std::any_cast<int>(new_value)));
        return;
      case OptionType::STRING:
        value.clear();
        value.push_back(std::any_cast<std::string>(new_value));
        return;
      case OptionType::VECTOR_BOOL: {
        const auto& vec = std::any_cast<std::vector<bool>>(new_value);
        value.clear();
        for (const auto& v : vec)
          value.push_back(v ? "true" : "false");
        return;
      }
      case OptionType::VECTOR_DOUBLE: {
        const auto& vec = std::any_cast<std::vector<double>>(new_value);
        value.clear();
        for (const auto& v : vec)
          value.push_back(std::to_string(v));
        return;
      }
      case OptionType::VECTOR_INT: {
        const auto& vec = std::any_cast<std::vector<int>>(new_value);
        value.clear();
        for (const auto& v : vec)
          value.push_back(std::to_string(v));
        return;
      }
      case OptionType::VECTOR_STRING:
        value = std::any_cast<std::vector<std::string>>(new_value);
        return;
      default:
        throw options_exception("Unsupported OptionType");
    }
  }
};

struct EventParameters
{
  std::vector<Parameter> options = {};

  bool is_empty() const
  {
    return options.empty();
  }

  bool has_value(const std::string& key) const
  {
    for (const auto& option : options)
      if (option.key == key)
        return true;
    return false;
  }

  std::any get_value(const std::string& key, std::any default_value)
  {
    if (has_value(key))
      for (auto& option : options)
      {
        if (option.key == key)
        {
          try
          {
            return option.get_value();
          }
          catch (const std::exception& e)
          {
            throw options_exception("Failed to convert option '" + option.key + "': " + e.what());
          }
        }
      }
    else
      return default_value;

    return default_value;
  }

  void set_value(const std::string& key, const std::any& value, const OptionType& type)
  {
    for (auto& option : options)
      if (option.key == key)
      {
        try
        {
          option.set_value(key, value, type);
          return;
        }
        catch (const std::exception& e)
        {
          throw options_exception("Failed to set option '" + option.key + "': " + e.what());
        }
      }

    Parameter new_option;
    new_option.set_value(key, value, type);
    options.push_back(new_option);
  }
};

}  // namespace fabric