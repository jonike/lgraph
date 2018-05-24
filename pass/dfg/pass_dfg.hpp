#ifndef PASS_DFG_HPP_
#define PASS_DFG_HPP_

#include "cfg_node_data.hpp"
#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_dfg_options_pack : public Options_pack {
public:
  Pass_dfg_options_pack();
  std::string output_name;
  bool        generate_dots_flag;
};

struct CF2DF_State {
  std::unordered_map<std::string, Index_ID> last_refs;
  std::unordered_map<Index_ID, Index_ID>    node_mapping;
};

class Pass_dfg : public Pass {
public:
  Pass_dfg() : Pass("dfg") {}

  void         cfg_2_dfg(LGraph *dfg, const LGraph *cfg);
  void         transform();
  virtual void transform(LGraph *g);

protected:
  Pass_dfg_options_pack opack;

private:
  // CFG 2 DFG conversion methods
  Index_ID find_root(const LGraph *cfg);
  void     process_node(LGraph *      dfg,
                        const LGraph *cfg,
                        CF2DF_State * state,
                        Index_ID      node);
  void     process_assign(LGraph *             dfg,
                          const LGraph *       cfg,
                          CF2DF_State *        state,
                          const CFG_Node_Data &data,
                          Index_ID             node);
  void     process_if(LGraph *             dfg,
                      const LGraph *       cfg,
                      CF2DF_State *        state,
                      const CFG_Node_Data &data,
                      Index_ID             node);
  void     process_operands(LGraph *             dfg,
                            const LGraph *       cfg,
                            CF2DF_State *        state,
                            const CFG_Node_Data &data,
                            Index_ID             node,
                            std::vector<Index_ID> &);

  Index_ID get_child(const LGraph *cfg, Index_ID node);
};

#endif
