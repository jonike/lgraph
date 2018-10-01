//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "live_options.hpp"
#include "core/lglog.hpp"

void Live_pass_options::set(const std::string &key, const std::string &value) {

  try {
    if (is_opt(key, "original_lgdb")) {
      original_lgdb = value;
    } else if (is_opt(key, "modified_lgdb")) {
      modified_lgdb = value;
    } else if (is_opt(key, "synth_lgdb")) {
      synth_lgdb = value;
    } else if (is_opt(key, "boundaries_name")) {
      boundaries_name = value;
    } else if (is_opt(key, "delta_lgdb")) {
      delta_lgdb = value;
    } else if (is_opt(key, "diff_file")) {
      diff_file = value;
    } else {
      set_val(key, value);
    }
  } catch (const std::invalid_argument& ia) {
    fmt::print("ERROR: key {} has an invalid argument {}\n", key);
  }
}
