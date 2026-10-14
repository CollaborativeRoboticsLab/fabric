#pragma once

#include <sstream>
#include <string>
#include <vector>

#include <Eigen/Dense>

namespace experience::database::detail
{
inline std::string join(const std::vector<std::string>& vec, const char delim = '\x1F')
{
  std::ostringstream oss;
  for (size_t i = 0; i < vec.size(); ++i)
  {
    if (i)
      oss << delim;
    oss << vec[i];
  }
  return oss.str();
}

inline std::vector<std::string> split(const std::string& s, const char delim = '\x1F')
{
  std::vector<std::string> elems;
  std::istringstream iss(s);
  std::string item;
  while (std::getline(iss, item, delim))
    elems.push_back(item);
  return elems;
}

inline std::string joinDoubles(const std::vector<double>& vec)
{
  std::ostringstream oss;
  for (size_t i = 0; i < vec.size(); ++i)
  {
    if (i)
      oss << ',';
    oss << vec[i];
  }
  return oss.str();
}

inline std::vector<double> splitDoubles(const std::string& s)
{
  std::vector<double> values;
  if (s.empty())
    return values;

  std::istringstream iss(s);
  std::string item;
  while (std::getline(iss, item, ','))
  {
    if (!item.empty())
      values.push_back(std::stod(item));
  }
  return values;
}

inline std::string joinBools(const std::vector<bool>& vec)
{
  std::string serialized;
  serialized.reserve(vec.size());
  for (bool value : vec)
    serialized.push_back(value ? '1' : '0');
  return serialized;
}

inline std::vector<bool> splitBools(const std::string& s)
{
  std::vector<bool> values;
  values.reserve(s.size());
  for (char ch : s)
    values.push_back(ch == '1');
  return values;
}

inline std::string serialize_vector(const Eigen::VectorXf& v)
{
  std::ostringstream oss;
  for (int i = 0; i < v.size(); ++i)
  {
    if (i)
      oss << ',';
    oss << v[i];
  }
  return oss.str();
}

inline Eigen::VectorXf deserialize_vector(const std::string& s)
{
  std::vector<float> vals;
  std::istringstream iss(s);
  std::string item;
  while (std::getline(iss, item, ','))
    vals.push_back(std::stof(item));

  Eigen::VectorXf v(vals.size());
  for (size_t i = 0; i < vals.size(); ++i)
    v[i] = vals[i];
  return v;
}
}  // namespace experience::database::detail
