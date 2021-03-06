//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cassert>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"
#include "pass_dfg.hpp"
#include "eprp_utils.hpp"

void setup_pass_dfg() {
  Pass_dfg p;
  p.setup();
}


void Pass_dfg::setup() {
  Eprp_method m1("pass.dfg.generate", "generate a dfg lgraph from a cfg lgraph", &Pass_dfg::generate);
  m1.add_label_optional("path", "lgraph path");
  m1.add_label_required("name", "lgraph name");

  register_pass(m1);

  Eprp_method m2("pass.dfg.optimize", "optimize a dfg lgraph", &Pass_dfg::optimize);
  m2.add_label_optional("path", "lgraph path");
  m2.add_label_optional("name", "lgraph name");

  register_pass(m2);

  Eprp_method m3("pass.dfg.finalize_bitwidth", "patch fake bitwidth for a dfg lgraph", &Pass_dfg::finalize_bitwidth);
  m3.add_label_optional("path", "lgraph path");
  m3.add_label_optional("name", "lgraph name");

  register_pass(m3);
}

void Pass_dfg::generate(Eprp_var &var) {
  Pass_dfg p;

  std::vector<LGraph *> dfgs;
  for(auto &cfg : var.lgs) {
    if(!absl::EndsWith(cfg->get_name(), "_cfg"))
      continue;

    dfgs = p.hierarchical_gen_dfgs(cfg);
  }

  if(dfgs.empty()) {
    warn(fmt::format("pass.dfg.generate needs an input cfg lgraph. Either name or |> from lgraph.open"));
    return;
  }
}

std::vector<LGraph*> Pass_dfg::hierarchical_gen_dfgs(LGraph *cfg_parent){
  std::vector<LGraph *> dfgs;
  fmt::print("hierarchical dfg generation start!\n");
  fmt::print("topg lgid:{}\n", cfg_parent->lg_id());
  auto dfg_name = cfg_parent->get_name().substr(0, cfg_parent->get_name().size() - 4);
  LGraph *dfg = LGraph::create(cfg_parent->get_path(), dfg_name, cfg_parent->get_name());
  I(dfg);
  do_generate(cfg_parent, dfg);
  dfgs.push_back(dfg);

  cfg_parent->each_sub_graph_fast([cfg_parent, &dfgs, this](Index_ID idx, Lg_type_id lgid, std::string_view iname){
    fmt::print("subgraph lgid:{}\n", lgid);
    LGraph *cfg_child = LGraph::open(cfg_parent->get_path(), lgid);
    if(cfg_child==0){
      Pass::error("hierarchy for {} could not open instance {} with lgid {}", cfg_parent->get_name(), iname, lgid);
    } else {
      I(absl::EndsWith(cfg_child->get_name(),"_cfg"));
      //auto child_dfg_name = cfg_child->get_name().substr(0, cfg_child->get_name().size() - 4);
      std::string_view child_dfg_name(cfg_child->get_name().data(), cfg_child->get_name().size()-4); // _cfg
      LGraph *dfg_child = LGraph::create(cfg_child->get_path(), child_dfg_name, cfg_child->get_name());
      I(dfg_child);

      do_generate(cfg_child, dfg_child);
      dfgs.push_back(dfg_child);
    }
  });

  return dfgs;
}

void Pass_dfg::optimize(Eprp_var &var) {

  for(auto &g : var.lgs) {
    Pass_dfg p;
    p.hierarchical_opt_dfgs(g);
  }
}

void Pass_dfg::hierarchical_opt_dfgs(LGraph *dfg_parent){
  fmt::print("hierarchical dfg optimization start!\n");
  fmt::print("topg lgid:{}\n", dfg_parent->lg_id());
  do_optimize(dfg_parent);

  dfg_parent->each_sub_graph_fast([dfg_parent, this](Index_ID idx, Lg_type_id lgid, std::string_view iname){
    fmt::print("subgraph lgid:{}\n", lgid);
    LGraph *dfg_child = LGraph::open(dfg_parent->get_path(), lgid);
    if(dfg_child==0){
      Pass::error("hierarchy for {} could not open instance {} with lgid {}", dfg_parent->get_name(), iname, lgid);
    } else {
      I(dfg_child);
      do_optimize(dfg_child);
    }
  });
}

void Pass_dfg::finalize_bitwidth(Eprp_var &var) {

  for(auto &g : var.lgs) {
    Pass_dfg p;
    p.hierarchical_finalize_bits_dfgs(g);
    g->close();
  }
}

void Pass_dfg::hierarchical_finalize_bits_dfgs(LGraph *dfg_parent){
  fmt::print("hierarchical dfg finalize_bits start!\n");
  fmt::print("topg lgid:{}\n", dfg_parent->lg_id());
  do_finalize_bitwidth(dfg_parent);

  dfg_parent->each_sub_graph_fast([dfg_parent, this](Index_ID idx, Lg_type_id lgid, std::string_view iname){
    fmt::print("subgraph lgid:{}\n", lgid);
    LGraph *dfg_child = LGraph::open(dfg_parent->get_path(), lgid);
    if(dfg_child==0){
      Pass::error("hierarchy for {} could not open instance {} with lgid {}", dfg_parent->get_name(), iname, lgid);
    } else {
      I(dfg_child);
      do_finalize_bitwidth(dfg_child);
    }
  });
}


Pass_dfg::Pass_dfg():Pass("dfg"){}

void Pass_dfg::do_generate(const LGraph *cfg, LGraph *dfg) {
  cfg_2_dfg(cfg, dfg);
  dfg->close();//instead of using lg->sync(), you should just call close()
}

void Pass_dfg::do_optimize(LGraph *&ori_dfg) {
  trans(ori_dfg);
  ori_dfg->close();
}

void Pass_dfg::trans(LGraph *dfg) {
  LGraph *sub_graph = nullptr;
  // resolve pending graph instantiation
  for(auto idx : dfg->fast()) {
    if(dfg->node_type_get(idx).op == DfgPendingGraph_Op) {
      //dfg->set_node_instance_name(idx, (std::string)dfg->get_node_wirename(idx)+ "_0");
      fmt::print("subgraph name is:{}\n", dfg->get_node_wirename(idx));
      sub_graph = LGraph::open(dfg->get_path(), dfg->get_node_wirename(idx));
      assert(sub_graph);

      dfg->node_subgraph_set(idx, sub_graph->lg_id());//changing from DfgPendingGraph_Op to normal subgraph_op
      fmt::print("resolve pending subG! lg_id:{}, nid:{}, subG name:{}\n", sub_graph->lg_id(), idx, dfg->get_node_wirename(idx));
      fmt::print("input name:{}\n",  sub_graph->get_graph_input_name_from_pid(1));
      fmt::print("input name:{}\n",  sub_graph->get_graph_input_name_from_pid(2));
      fmt::print("output name:{}\n", sub_graph->get_graph_output_name_from_pid(1));
    }
  }


  //resolve top <-> subgraph IO connection
  for(auto idx : dfg->fast()){
    if(dfg->node_type_get(idx).op == SubGraph_Op){
      fmt::print("resolving connections, subgraph is:{}\n", dfg->get_node_wirename(idx));
      sub_graph = LGraph::open(dfg->get_path(), dfg->get_node_wirename(idx));
      I(sub_graph);

      //resolve subgraph input connections
      std::unordered_map <Node_Pin*, Node_Pin*> subg_inp_edges;
      for(auto &inp : dfg->inp_edges(idx)){
        Index_ID src_nid = inp.get_out_pin().get_nid();
        Index_ID dst_nid = idx;
        Port_ID  src_pid = 0;
        auto inp_name = dfg->get_node_wirename(src_nid);
        //TODO:should change to sub_graph->each_input
        Port_ID  dst_pid = sub_graph->get_graph_input(inp_name).get_pid();

        fmt::print("inp_name:{}\n",inp_name);
        fmt::print("src_nid:{}, src_pid:{}, dst_nid:{}, dst_pid:{}\n", src_nid, src_pid, dst_nid, dst_pid);
        Node_Pin* src_pin = new Node_Pin(src_nid, src_pid, false);
        Node_Pin* dst_pin = new Node_Pin(dst_nid, dst_pid, true);
        subg_inp_edges[src_pin] = dst_pin;
        dfg->del_edge(inp); //WARNNING: don't add_edge and del_edge at the same reference loop!
      }

      for(auto &edge : subg_inp_edges){
        dfg->add_edge(*(edge.first), *(edge.second));
      }

      //resolve subgraph output connections
      std::unordered_map <Node_Pin*, Node_Pin*> subg_out_edges;
      for(auto &out : dfg->out_edges(idx)){
        Index_ID src_nid = idx;
        Index_ID dst_nid = out.get_inp_pin().get_nid();
        Port_ID  dst_pid = out.get_inp_pin().get_pid();
        Port_ID  src_pid = 0;
        sub_graph->each_output([&sub_graph, &src_pid](Index_ID idx, Port_ID pid) {
          fmt::print("outputs of subgraph: idx:{}, pid:{}, name:{}\n",idx, pid, sub_graph->get_graph_output_name_from_pid(pid));
          src_pid = pid;
        });
        fmt::print("src_nid:{}, src_pid:{}, dst_nid:{}, dst_pid:{}\n", src_nid, src_pid, dst_nid, dst_pid);
        Node_Pin* src_pin = new Node_Pin(src_nid, src_pid, false);
        Node_Pin* dst_pin = new Node_Pin(dst_nid, dst_pid, true);
        subg_out_edges[src_pin] = dst_pin;
        //fmt::print("hello~, get_bits_pid:{}\n", sub_graph->get_bits_pid(src_nid,src_pid));
        //dfg->set_bits_pid(src_nid, src_pid, sub_graph->get_bits_pid(src_nid, src_pid));
        dfg->set_bits_pid(src_nid, src_pid, 2);
        dfg->del_edge(out); //WARNNING: don't add_edge and del_edge at the same reference loop!
      }

      for(auto &edge : subg_out_edges){
        dfg->add_edge(*(edge.first), *(edge.second));
      }

      //set wirename of the subg node back to empty to avoid yosys code generation conflict of count_id(cell->name)
      dfg->set_node_wirename(idx,"subg_tmp_wire");
    }
  }

  for(auto idx : dfg->fast()) {
      if(dfg->node_type_get(idx).op == Equals_Op) {
        dfg->set_bits(idx, 1);
      } else if(dfg->node_type_get(idx).op == GreaterEqualThan_Op) {
        dfg->set_bits(idx, 1);
      } else if(dfg->node_type_get(idx).op == GreaterThan_Op) {
        dfg->set_bits(idx, 1);
      } else if(dfg->node_type_get(idx).op == LessEqualThan_Op) {
        dfg->set_bits(idx, 1);
      } else if(dfg->node_type_get(idx).op == LessThan_Op) {
        dfg->set_bits(idx, 1);
      } else {
        ;
      }
  }
}

void Pass_dfg::do_finalize_bitwidth(LGraph *dfg) {
  for(auto idx : dfg->fast()){
    uint16_t nid_size = dfg->get_bits(idx);
    if(nid_size == 0){
      Node_bitwidth &nb = dfg->node_bitwidth_get(idx);
      fmt::print("nid:{},max:{}\n",idx, nb.i.max);
      dfg->set_bits(idx, ((uint16_t)floor(log2(nb.i.max))+1));
    }
  }

  for(auto idx: dfg->fast()){
    if(dfg->node_type_get(idx).op == Mux_Op) {
      for (auto &inp : dfg->inp_edges(idx)) {
        Index_ID src_nid = inp.get_out_pin().get_nid();
        Index_ID dst_nid = idx;
        //Port_ID  src_pid = inp.get_out_pin().get_pid();
        Port_ID  dst_pid = inp.get_inp_pin().get_pid();

        if(dst_pid == 0)
          continue;

        //assume MIT algo. will at least set mux.bits equals to the larger input
        uint16_t bw_diff = abs(dfg->get_bits(src_nid) - dfg->get_bits(dst_nid));
        if(dfg->get_bits(dst_nid) > dfg->get_bits(src_nid)) {
          //temporarily using unsigned extend to fix mux bitwidth mismatch
          Index_ID unsign_ext_nid = create_const32_node(dfg, 0, bw_diff, false);
          Index_ID nid_join = dfg->create_node().get_nid();
          dfg->node_type_set(nid_join, Join_Op);
          dfg->set_bits(nid_join, dfg->get_bits(dst_nid));
          dfg->add_edge(Node_Pin(unsign_ext_nid, 0, false), Node_Pin(nid_join, 1, true));
          dfg->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(nid_join, 0, true));
          dfg->add_edge(Node_Pin(nid_join, 0, false), Node_Pin(dst_nid, dst_pid, true));
          dfg->del_edge(inp);
        }
      }
    }

    //after MIT algo. and Mux_Op processing, bw of every node should be synced except an output gio connected to a Mux_Op
    if(dfg->node_type_get(idx).op == GraphIO_Op){
      for(auto &inp : dfg->inp_edges(idx)){
        Index_ID src_nid = inp.get_out_pin().get_nid();
        Index_ID dst_nid = idx;
        if(dfg->get_bits(src_nid) > dfg->get_bits(dst_nid))
          dfg->set_bits(dst_nid, dfg->get_bits(src_nid));
        else//Warning! this is workaround will be eventually wrong after MIT could handle subgraph
          dfg->set_bits(src_nid, dfg->get_bits(dst_nid));
      }
    }
  }
}

bool Pass_dfg::cfg_2_dfg(const LGraph *cfg, LGraph *dfg) {
  Index_ID itr = find_cfg_root(cfg);
  Aux_node auxnd_global;
  Aux_tree aux_tree(&auxnd_global);
  process_cfg(dfg, cfg, &aux_tree, itr);
  finalize_gconnect(dfg, &auxnd_global);

  fmt::print("calling sync\n");

  return true; // FIXME: FALSE == failure in dfg generation
}

void Pass_dfg::finalize_gconnect(LGraph *dfg, const Aux_node *auxnd_global) {
  fmt::print("finalize global connect\n");
  for(const auto &pair : auxnd_global->get_pendtab()) {
    if(is_output(pair.first)) {
      Index_ID dst_nid = dfg->get_graph_output(pair.first.substr(1)).get_nid();
      Index_ID src_nid = pair.second;
      Port_ID  src_pid = 0;
      dfg->add_edge(Node_Pin(src_nid, src_pid, false), Node_Pin(dst_nid, 0, true));
      fmt::print("add edge, src_nid:{}, src_pid:{}, dst_nid:{}, dst:pid:{}\n", src_nid, src_pid, dst_nid, 0);
    } else if(is_register(pair.first)) {
      ; // balabala
    }
  }
}

Index_ID Pass_dfg::process_cfg(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, Index_ID top_node) {
  Index_ID itr      = top_node;
  Index_ID last_itr = 0;

  while(itr != 0) {
    last_itr = itr;

    Index_ID tmp = process_node(dfg, cfg, aux_tree, itr);
    fmt::print("process_node return cfg_nid:{}!!\n\n", tmp);
    itr = tmp;
    fmt::print("cfg nid:{} process finished!!\n\n", last_itr);
  }
  aux_tree->print_cur_auxnd();
  fmt::print("\n\n");
  return last_itr;
}

Index_ID Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, Index_ID cfg_node) {

  const CFG_Node_Data data(cfg, cfg_node);

  fmt::print("Processing CFG node:{}\n", cfg_node);
  fmt::print("target:[{}], operator:[{}], ", data.get_target(), data.get_operator());
  fmt::print("operands:[");
  for(const auto &i : data.get_operands())
    fmt::print("{}, ", i);
  fmt::print("]");
  fmt::print("\n");

  switch(cfg->node_type_get(cfg_node).op) {
  case CfgAssign_Op:
    process_assign(dfg, aux_tree, data);
    return get_cfg_child(cfg, cfg_node);
  case CfgFunctionCall_Op:
    process_func_call(dfg, cfg, aux_tree, data);
    return get_cfg_child(cfg, cfg_node);
  case CfgIf_Op: {
    aux_tree->print_cur_auxnd();
    Index_ID tmp = process_if(dfg, cfg, aux_tree, data, cfg_node);
    return tmp;
  }
  case CfgIfMerge_Op:
    return 0;
  default:
    fmt::print("\n\n*************Unrecognized cfg_node type[n={}]: {}\n", cfg_node, cfg->node_type_get(cfg_node).get_name());
    return get_cfg_child(cfg, cfg_node);
  }
}

void Pass_dfg::process_func_call(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  // for func_call, all the node should be created before, you just connect them. No need to create target node
  const auto &target    = data.get_target();
  const auto &oprds     = data.get_operands();
  const auto &oprd_ids  = process_operands(dfg, aux_tree, data); // all operands should be in auxtab, just retrieve oprd_ids
  assert(!oprds.empty());
  Index_ID subg_root_nid = aux_tree->get_alias(oprds[0]);

  fmt::print("------>process function call for {}!!!!!\n", dfg->get_node_wirename(subg_root_nid));
    // FIXME: LGraph::open is a costly op, do you really need to check this for each wire?????

  LGraph *sub_graph = LGraph::open(cfg->get_path(), dfg->get_node_wirename(subg_root_nid));
  if(sub_graph) {
    dfg->node_subgraph_set(subg_root_nid, sub_graph->lg_id());
    fmt::print("set subgraph on nid:{}, name:{}, lgid:{}\n", subg_root_nid, dfg->get_node_wirename(subg_root_nid), sub_graph->lg_id());
  } else {
    dfg->node_type_set(subg_root_nid, DfgPendingGraph_Op);
    //re-assign correct subgraph name
    dfg->set_node_wirename(subg_root_nid, oprds.at(0));
    fmt::print("set pending graph on nid:{}, sub_graph name should be:{}\n", subg_root_nid, dfg->get_node_wirename(subg_root_nid));
  }

  aux_tree->set_alias(target, oprd_ids[0]);

  // connect 1st operand with [2nd,3rd,...] operands
  std::vector<Index_ID> subg_input_ids(oprd_ids.begin() + 1, oprd_ids.end());
  process_connections(dfg, subg_input_ids, subg_root_nid);
}

void Pass_dfg::process_assign(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  fmt::print("process_assign\n");
  const auto &                    target = data.get_target();
  const std::vector<std::string> &oprds  = data.get_operands();
  auto             op     = data.get_operator();
  Index_ID                        oprd_id0;
  Index_ID                        oprd_id1;
  assert(oprds.size() > 0);
  if(is_pure_assign_op(op)) {
    if(is_output(target) && !dfg->is_graph_output(target.substr(1)))
      create_output(dfg, aux_tree, target);
    oprd_id0 = process_operand(dfg, aux_tree, oprds[0]);
    aux_tree->set_alias(target, oprd_id0);
    aux_tree->set_pending(target, oprd_id0);
  } else if(is_label_op(op)) {
    assert(oprds.size() > 1);
    if(oprds[0] == "__bits") {
      fmt::print("__bits size assign\n");
      Index_ID floating_id = process_operand(dfg, aux_tree, oprds[1]);
      aux_tree->set_alias(target, floating_id);
    } else if(oprds[0] == "__fluid") {
      ; //
    } else {
      fmt::print("function argument assign\n");
      oprd_id1 = process_operand(dfg, aux_tree, oprds[1]);
      aux_tree->set_alias(target, oprd_id1);
      //oprd_id0 is the io name of subgraph, record in the src node wirename
      dfg->set_node_wirename(oprd_id1, oprds[0]);
      fmt::print("dst_subG_io:{}, src_nid:[{},{}]\n",oprds[0], oprd_id1, oprds[1]);
    }
  } else if(is_as_op(op)) {
    oprd_id0 = process_operand(dfg, aux_tree, oprds[0]);
    // process target
    if(is_input(target)) {
      assert(dfg->node_value_get(oprd_id0));
      auto     bits      = dfg->node_value_get(oprd_id0);             // to be checked
      Index_ID target_id = create_input(dfg, aux_tree, target, bits); // to be checked
      fmt::print("create node for input target:{}, nid:{}\n", target, target_id);
    } else if(is_output(target)) {
      assert(dfg->node_value_get(oprd_id0));
      auto     bits      = dfg->node_value_get(oprd_id0);
      Index_ID target_id = create_output(dfg, aux_tree, target, bits);
      fmt::print("create node for output target:{}, nid:{}\n", target, target_id);
    } else
      aux_tree->set_alias(target, oprd_id0);
  } else if(is_unary_op(op)) {
    oprd_id0 = process_operand(dfg, aux_tree, oprds[0]);
    aux_tree->set_alias(target, oprd_id0);
  } else if(is_compute_op(op)) {
    assert(oprds.size() > 1);
    std::vector<Index_ID> oprd_ids;
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[0]));
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[1]));
    Index_ID target_id = create_node(dfg, aux_tree, target);
    fmt::print("create node for internal target:{}, nid:{}\n", target, target_id);
    dfg->node_type_set(target_id, node_type_from_text(op));
    process_connections(dfg, oprd_ids, target_id);
  } else if(is_compare_op(op)) {
    assert(oprds.size() > 1);
    fmt::print("{} is compare op\n", op);
    std::vector<Index_ID> oprd_ids;
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[0]));
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[1]));
    Index_ID target_id = create_node(dfg, aux_tree, target);
    fmt::print("create node for internal target:{}, nid:{}\n", target, target_id);
    dfg->node_type_set(target_id, node_type_from_text(op));
    process_connections(dfg, oprd_ids, target_id);
  }
}

void Pass_dfg::process_connections(LGraph *dfg, const std::vector<Index_ID> &src_nids, const Index_ID &dst_nid) {
  bool one_srcs_is_signed = false;
  for(const auto& i:src_nids ){
    if(dfg->node_bitwidth_get(i).e.sign){
      one_srcs_is_signed = true;
      break;
    }
  }
  for(uint16_t i = 0; i < src_nids.size(); i++) {
    Index_ID src_nid = src_nids.at(i);
    Port_ID src_pid = 0;
    //assert(Node_Type_Sum::get_input_match("Au") == 1);
    //assert(dfg->node_type_get(dst_nid).op != SubGraph_Op); // Handled separate as it is a more complicated case
    Port_ID dst_pid =
        (dfg->node_type_get(dst_nid).op == Sum_Op &&  one_srcs_is_signed) ? (uint16_t)0 :
        (dfg->node_type_get(dst_nid).op == Sum_Op && !one_srcs_is_signed) ? (uint16_t)1 :
        (dfg->node_type_get(dst_nid).op == LessThan_Op && i == 0)         ? (uint16_t)0 :
        (dfg->node_type_get(dst_nid).op == LessThan_Op && i == 1)         ? (uint16_t)2 :
        (dfg->node_type_get(dst_nid).op == GreaterThan_Op && i == 0)      ? (uint16_t)0 :
        (dfg->node_type_get(dst_nid).op == GreaterThan_Op && i == 1)      ? (uint16_t)2 :
        (dfg->node_type_get(dst_nid).op == LessEqualThan_Op && i == 0)    ? (uint16_t)0 :
        (dfg->node_type_get(dst_nid).op == LessEqualThan_Op && i == 1)    ? (uint16_t)2 :
        (dfg->node_type_get(dst_nid).op == GreaterEqualThan_Op && i == 0) ? (uint16_t)0 :
        (dfg->node_type_get(dst_nid).op == GreaterEqualThan_Op && i == 1) ? (uint16_t)2 :
        (dfg->node_type_get(dst_nid).op == DfgPendingGraph_Op)            ? (uint16_t)i :
        (dfg->node_type_get(dst_nid).op == SubGraph_Op)                   ? (uint16_t)i : (uint16_t)0;

    // the subgraph IOs connection cannot be resolved at the first pass
    // so just casually connect the top<->subgraph IOs so we could traverse edges and
    // resoved connections later when we resolves the subgraph instantiation".
    dfg->add_edge(Node_Pin(src_nid, src_pid, false), Node_Pin(dst_nid, dst_pid, true));
    fmt::print("add edge, src_nid:{}, src_pid:{}, dst_nid:{}, dst:pid:{}\n", src_nid, 0, dst_nid, 2);
  }
}

Index_ID Pass_dfg::process_operand(LGraph *dfg, Aux_tree *aux_tree, const std::string &oprd) {
  Index_ID oprd_id;
  if(aux_tree->has_alias(oprd)) {
    oprd_id = aux_tree->get_alias(oprd);
    fmt::print("operand:{} has an alias:{}\n", oprd, oprd_id);
  } else {
    if(is_constant(oprd)) { // as __bits is processed here!
      oprd_id = resolve_constant(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for constant operand:{}, nid:{}\n", oprd, oprd_id);
    } else if(is_input(oprd)) {
      oprd_id = create_input(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for input operand:{}, nid:{}\n", oprd, oprd_id);
    } else if(is_output(oprd)) {
      oprd_id = create_output(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for output operand:{}, nid:{}\n", oprd, oprd_id);
    } else if(is_reference(oprd)) {
      oprd_id = create_reference(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for reference operand:{}, nid:{}\n", oprd, oprd_id);
    } else {
      oprd_id = create_private(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for private operand:{}, nid:{}\n", oprd, oprd_id);
    }
    // else if (is_register(oprd)){
    //  //oprd_id = create_register(dfg, aux_tree, oprd);
    //  //aux_tree->set_alias(oprd, oprd_id);
    //  //fmt::print("create node for register operand:{}, nid:{}\n", oprd, oprd_id);
    //}
  }
  // if (aux_tree->fluid_df() && is_input(oprd))
  //  add_read_marker(dfg, aux_tree, oprd);
  return oprd_id;
}

Index_ID Pass_dfg::process_if(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data, Index_ID cfg_node) {
  fmt::print("process if start!\n");
  assert(aux_tree->has_alias(data.get_target()));
  Index_ID    cond     = aux_tree->get_alias(data.get_target());
  const auto &operands = data.get_operands();
  auto *      tauxnd   = new Aux_node;
  auto *      fauxnd   = new Aux_node; //don't dynamic allocate here!!
  auto *pauxnd = aux_tree->get_cur_auxnd(); // parent aux

  assert(operands.size() > 1);
  Index_ID tbranch = (Index_ID)std::stol(operands[0]);
  Index_ID fbranch = (Index_ID)std::stol(operands[1]);

  aux_tree->set_parent_child(pauxnd, tauxnd, true);
  Index_ID tb_next = get_cfg_child(cfg, process_cfg(dfg, cfg, aux_tree, tbranch));
  fmt::print("branch true finish! tb_next:{}\n", tb_next);

  if(fbranch != 0) { // there is an 'else' clause
    aux_tree->set_parent_child(pauxnd, fauxnd, false);
    Index_ID fb_next = get_cfg_child(cfg, process_cfg(dfg, cfg, aux_tree, fbranch));
    assert(tb_next == fb_next);
    fmt::print("branch false finish! tb_next:{}\n", tb_next);
  }

  // The auxT,F should be empty and are safe to be deleted after
  // TODO:put assertion on auxT, F emptiness
  resolve_phis(dfg, aux_tree, pauxnd, tauxnd, fauxnd, cond);

  if(fbranch != 0) {
    aux_tree->disconnect_child(aux_tree->get_cur_auxnd(), fauxnd, false);
    aux_tree->auxes_stack_pop();
  }

  aux_tree->disconnect_child(aux_tree->get_cur_auxnd(), tauxnd, true);
  aux_tree->auxes_stack_pop();

  fmt::print("process if done!!\n");
  return tb_next;
}

void Pass_dfg::assign_to_true(LGraph *dfg, Aux_tree *aux_tree, const std::string &v) {
  Index_ID node = create_node(dfg, aux_tree, v);
  fmt::print("create node nid:{}\n", node);
  dfg->node_type_set(node, Or_Op);

  dfg->add_edge(Node_Pin(create_true_const(dfg, aux_tree), 0, false), Node_Pin(node, 0, true));
  dfg->add_edge(Node_Pin(create_true_const(dfg, aux_tree), 0, false), Node_Pin(node, 0, true));
}

void Pass_dfg::attach_outputs(LGraph *dfg, Aux_tree *aux_tree) {
  ;
}

void Pass_dfg::add_fluid_behavior(LGraph *dfg, Aux_tree *aux_tree) {
  std::vector<Index_ID> inputs, outputs;
  add_fluid_ports(dfg, aux_tree, inputs, outputs);
}

void Pass_dfg::add_fluid_ports(LGraph *dfg, Aux_tree *aux_tree, std::vector<Index_ID> &data_inputs,
                               std::vector<Index_ID> &data_outputs) {
  ;
}

void Pass_dfg::add_fluid_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs,
                               const std::vector<Index_ID> &data_outputs) {
  ;
}

void Pass_dfg::add_abort_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs,
                               const std::vector<Index_ID> &data_outputs) {
  ;
}

Index_ID Pass_dfg::find_cfg_root(const LGraph *cfg) {
  Index_ID root_id=0;
  cfg->each_input([&root_id](Index_ID idx){
    root_id = idx;
  });
  I(root_id!=0);

  fmt::print("root_id:{}\n", root_id);
  assert(root_id);
  return root_id;
}

Index_ID Pass_dfg::get_cfg_child(const LGraph *cfg, Index_ID node) {
  for(const auto &cedge : cfg->out_edges(node))
    return cedge.get_inp_pin().get_nid();

  return 0;
}

std::vector<Index_ID> Pass_dfg::process_operands(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  const std::vector<std::string> &oprds = data.get_operands();
  std::vector<Index_ID>           oprd_ids(oprds.size());
  // const std::string &op = data.get_operator();
  for(size_t i = 0; i < oprd_ids.size(); i++) {
    if(aux_tree->has_alias(oprds[i])) {
      oprd_ids[i] = aux_tree->get_alias(oprds[i]);
      fmt::print("operand:{} has an alias:{}\n", oprds[i], oprd_ids[i]);
    } else {
      if(is_constant(oprds[i])) {
        // oprd_ids[i] = create_default_const(dfg, aux_tree);
        oprd_ids[i] = resolve_constant(dfg, aux_tree, oprds[i]);
        fmt::print("create node for constant operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      } else if(is_input(oprds[i])) {
        oprd_ids[i] = create_input(dfg, aux_tree, oprds[i]);
        fmt::print("create node for input operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      } else if(is_output(oprds[i])) {
        oprd_ids[i] = create_output(dfg, aux_tree, oprds[i]);
        fmt::print("create node for output operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      } else if(is_reference(oprds[i])) {
        oprd_ids[i] = create_reference(dfg, aux_tree, oprds[i]);
        fmt::print("create node for reference operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      } else {
        oprd_ids[i] = create_private(dfg, aux_tree, oprds[i]);
        fmt::print("create node for private operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      }
      // else if (is_register(oprds[i])){
      //  //oprd_ids[i] = create_register(dfg, aux_tree, oprds[i]);
      //  //fmt::print("create node for register operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      //}
    }

    // if (aux_tree->fluid_df() && is_input(oprds[i]))
    //  add_read_marker(dfg, aux_tree, oprds[i]);
  }

  return oprd_ids;
}

void Pass_dfg::resolve_phis(LGraph *dfg, Aux_tree *aux_tree, Aux_node *pauxnd, Aux_node *tauxnd, Aux_node *fauxnd, Index_ID cond) {
  fmt::print("resolve phis\n");
  // resolve phi in branch true
  auto iter = tauxnd->get_pendtab().begin();
  while(iter != tauxnd->get_pendtab().end()) {
    fmt::print("key is:{}, ", iter->first);
    if(fauxnd && fauxnd->has_pending(iter->first)) {
      fmt::print("has same pend in fault\n");
      Index_ID tid = iter->second;
      Index_ID fid = fauxnd->get_pending(iter->first); // return Index_ID
      fauxnd->del_pending(iter->first);
      create_mux(dfg, pauxnd, tid, fid, cond, iter->first);
    } else if(pauxnd->has_pending(iter->first)) {
      fmt::print("has same pend in parent\n");
      Index_ID tid = iter->second;
      Index_ID fid = pauxnd->get_pending(iter->first);
      pauxnd->del_pending(iter->first);
      create_mux(dfg, pauxnd, tid, fid, cond, iter->first);
    } else {
      fmt::print("has no same pend\n");
      Index_ID tid = iter->second;
      Index_ID fid = aux_tree->has_pending(iter->first) ? aux_tree->get_pending(iter->first) : create_default_const(dfg);
      create_mux(dfg, pauxnd, tid, fid, cond, iter->first);
    }
    tauxnd->del_pending(iter++->first);
  }
  // resolve phi in branch false
  iter = fauxnd->get_pendtab().begin();
  while(iter != fauxnd->get_pendtab().end()) {
    if(pauxnd->has_pending(iter->first)) {
      Index_ID tid = pauxnd->get_pending(iter->first);
      Index_ID fid = iter->second;
      pauxnd->del_pending(iter->first);
      create_mux(dfg, pauxnd, tid, fid, cond, iter->first);
    } else {
      Index_ID tid = aux_tree->has_pending(iter->first) ? aux_tree->get_pending(iter->first) : create_default_const(dfg);
      Index_ID fid = iter->second;
      create_mux(dfg, pauxnd, tid, fid, cond, iter->first);
    }
    fauxnd->del_pending(iter++->first);
  }
  //so far pendtab of tauxnd and fauxnd should be empty
  //TODO:put an assertion instead of just comment
}

void Pass_dfg::create_mux(LGraph *dfg, Aux_node *pauxnd, Index_ID tid, Index_ID fid, Index_ID cond, const std::string &var) {
  fmt::print("create mux:{}, tid:{}, fid:{}\n", var, tid, fid);
  Index_ID phi = dfg->create_node().get_nid();
  dfg->node_type_set(phi, Mux_Op);
  auto tp = dfg->node_type_get(phi);

  Port_ID tin = tp.get_input_match("B");
  Port_ID fin = tp.get_input_match("A");
  Port_ID cin = tp.get_input_match("S");

  dfg->add_edge(Node_Pin(tid, 0, false), Node_Pin(phi, tin, true));
  dfg->add_edge(Node_Pin(fid, 0, false), Node_Pin(phi, fin, true));
  dfg->add_edge(Node_Pin(cond, 0, false), Node_Pin(phi, cin, true));
  pauxnd->set_alias(var, phi);
  pauxnd->set_pending(var, phi);
}

// bits inference: first round: deal with src bw > dst bw
/*
for(auto idx : dfg->fast()) {
  for(const auto &out : dfg->out_edges(idx)) {
    Index_ID src_nid = idx;
    Index_ID dst_nid = out.get_idx();
    Port_ID  src_pid = out.get_out_pin().get_pid();
    //Port_ID  dst_pid = out.get_inp_pin().get_pid();
    uint16_t src_nid_size = dfg->get_bits(src_nid);
    uint16_t dst_nid_size = dfg->get_bits(dst_nid);

    if(dfg->node_type_get(idx).op == SubGraph_Op) {
      LGraph *    subgraph = LGraph::open(dfg->get_path(), dfg->subgraph_id_get(idx));
      assert(subgraph);
      //fmt::print("node_wirename:{}\n",dfg->get_node_wirename(idx));
      //problem2: inou_yosys got empty inst_name for cell type sp_add
      //dfg->set_node_instance_name(idx, dfg->get_node_wirename(idx));//problem3: it seems fail and trigger char_array assertion fail
      //fmt::print("has instance name:{}\n", dfg->has_instance_name(dfg->get_node_wirename(idx)));
      //fmt::print("get instance name:{}\n", dfg->get_node_instancename(idx));
      //const char *out_name = subgraph->get_graph_output_name_from_pid(1);//problem1:make source pid = 1 will work, but this is not a true pid
      const char *out_name = subgraph->get_graph_output_name_from_pid(src_pid);//src_pid = 0 will fail, is it a new bug!?
      fmt::print("nid:{}, subgraph_lg_id:{}, out_name:{}\n", idx, subgraph->lg_id(), out_name);
      uint16_t    out_size = subgraph->get_bits(subgraph->get_graph_output(out_name).get_nid());
      dfg->set_bits(dst_nid, out_size);
    } else if(dfg->node_type_get(dst_nid).op == Mux_Op) {
      ;
    } else if(dfg->node_type_get(dst_nid).op == Equals_Op) {
      ; // don't infetence when dst is a comparator, the result should be a bool
    } else if(dfg->node_type_get(dst_nid).op == GreaterEqualThan_Op) {
      ; // don't infetence when dst is a comparator, the result should be a bool
    } else if(dfg->node_type_get(dst_nid).op == GreaterThan_Op) {
      ; // don't infetence when dst is a comparator, the result should be a bool
    } else if(dfg->node_type_get(dst_nid).op == LessEqualThan_Op) {
      ; // don't infetence when dst is a comparator, the result should be a bool
    } else if(dfg->node_type_get(dst_nid).op == LessThan_Op) {
      ; // don't infetence when dst is a comparator, the result should be a bool
    } else {
      if(src_nid_size > dst_nid_size)
        dfg->set_bits(dst_nid, src_nid_size);
    }
  }
}*/
