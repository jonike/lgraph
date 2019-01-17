#pragma once

#include <vector>
#include <string>

// List of useful methods for parsers in lgraph
class Eprp_utils {
public:
  static std::string get_exe_path();

  static void clean_dir(std::string_view dir);
};

