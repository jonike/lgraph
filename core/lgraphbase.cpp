//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <set>

#include "graph_library.hpp"
#include "iassert.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "pass.hpp"

LGraph_Base::LGraph_Base(const std::string &_path, const std::string &_name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(_path, _name, lgid)
    , LGraph_Node_Type(_path, _name, lgid)
    , LGraph_InstanceNames(path, _name, lgid)
    , input_array(_path + "/lgraph_" + _name + "_inputs")
    , output_array(_path + "/lgraph_" + _name + "_outputs") {
  I(lgid);  // No id zero allowed
}

LGraph_Base::~LGraph_Base() {
  // TODO: This is NOT a bug. The reason is that we need to preserve the
  // string pointers for graph name. Then, we can use string_view maps for all
  // of them. Otherwise, we can not.
  //
  // If deleting becomes a problem, we should preserve the graph names as a
  // table that lives forever once a graph/lgraph is opened.
  //
  // I(false);
}

LGraph_Base::_init::_init() {
  // fmt::print("LGraph_Base static init done\n");
  // Add here sequence of static initialization that may be needed
}

bool LGraph_Base::close() { return Lgraph_base_core::close(); }

std::string_view LGraph_Base::get_subgraph_name(Index_ID nid) const {
  I(node_type_get(nid).op == SubGraph_Op);
  return library->get_name(subgraph_id_get(nid));
}

void LGraph_Base::clear() {
  LGraph_Node_Type::clear();
  LGraph_InstanceNames::clear();

  node_internal.clear();
  input_array.clear();
  output_array.clear();

  Lgraph_base_core::clear();
}

void LGraph_Base::sync() {
  LGraph_Node_Type::sync();
  LGraph_InstanceNames::sync();

  node_internal.sync();

  input_array.sync();
  output_array.sync();

  Lgraph_base_core::sync();
}

void LGraph_Base::emplace_back() {
  I(locked);

  node_internal.emplace_back();

  Index_ID nid = node_internal.size() - 1;
  if (!node_internal.back().is_page_align()) {
    new (&node_internal[nid]) Node_Internal();  // call constructor
    node_internal[nid].set_nid(nid);            // self by default
  }

  LGraph_Node_Type::emplace_back();
  LGraph_InstanceNames::emplace_back();
}

void LGraph_Base::reload() {
  auto sz = library->get_nentries(lgraph_id);

  node_internal.reload(sz);
  // lazy input_array.reload();
  // lazy output_array.reload();

  recompute_io_ports();

  LGraph_Node_Type::reload(sz);
  LGraph_InstanceNames::reload(sz);
}

void LGraph_Base::recompute_io_ports() {
  // WARNING: OK string_view as no memory alloc/dealloc is done in input_array/output_array
  std::map<std::string_view, int> ordered;

  std::vector<int> fixed;
  Port_ID          pos = 1;

  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    auto &p = it.get_field();
    if (p.original_set) {
      p.pos = p.original_pos;
      if (p.original_pos >= fixed.size()) fixed.resize(p.original_pos + 1);
      I(fixed[p.original_pos] == 0);  // original_pos must be unique
      fixed[p.original_pos] = it.get_id();
    } else {
      ordered[it.get_name()] = it.get_id();
    }
  }

  while (fixed.size() > pos && fixed[pos]) {
    pos++;
  }
  for (auto &it : ordered) {
    auto &p = input_array.get_field(it.second);
    p.pos   = pos++;
    while (fixed.size() > pos && fixed[pos]) {
      pos++;
    }
  }

  // Reassign outputs in alphabetical order
  ordered.clear();
  pos = 1;

  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    auto &p = it.get_field();
    if (p.original_set) {
      p.pos = p.original_pos;
      if (p.original_pos >= fixed.size()) fixed.resize(p.original_pos + 1);
      I(fixed[p.original_pos] == 0);  // original_pos must be unique
      fixed[p.original_pos] = it.get_id();
    } else {
      ordered[it.get_name()] = it.get_id();
    }
  }

  while (fixed.size() > pos && fixed[pos]) {
    pos++;
  }
  for (auto &it : ordered) {
    auto &p = output_array.get_field(it.second);
    p.pos   = pos++;
    while (fixed.size() > pos && fixed[pos]) {
      pos++;
    }
  }
}

Index_ID LGraph_Base::add_graph_io_common(std::string_view str, Index_ID nid, uint16_t bits) {
  if (nid == 0) nid = create_node_int();

  if (bits != 0) set_bits(nid, bits);

  node_internal[nid].set_dst_pid(0);
  I(node_internal[nid].is_master_root());

  return nid;
}

Index_ID LGraph_Base::add_graph_input_int(std::string_view str, Index_ID nid, uint16_t bits) {
  I(input_array.get_id(str) == 0);  // No name dupliation

  nid = add_graph_io_common(str, nid, bits);
  node_internal[nid].set_graph_io_input();

  IO_port p(nid, 0, false);
  input_array.create_id(str, p);

  recompute_io_ports();

  return nid;
}

Index_ID LGraph_Base::add_graph_input_int(std::string_view str, Index_ID nid, uint16_t bits, Port_ID original_pos) {
  I(input_array.get_id(str) == 0);  // No name dupliation

  nid = add_graph_io_common(str, nid, bits);
  node_internal[nid].set_graph_io_input();

  IO_port p(nid, original_pos, true);
  input_array.create_id(str, p);

  return nid;
}

Index_ID LGraph_Base::add_graph_output_int(std::string_view str, Index_ID nid, uint16_t bits) {
  I(output_array.get_id(str) == 0);  // No name dupliation

  nid = add_graph_io_common(str, nid, bits);
  node_internal[nid].set_graph_io_output();

  IO_port p(nid, 0, false);
  output_array.create_id(str, p);

  recompute_io_ports();

  return nid;
}

Index_ID LGraph_Base::add_graph_output_int(std::string_view str, Index_ID nid, uint16_t bits, Port_ID original_pos) {
  I(output_array.get_id(str) == 0);  // No name dupliation

  nid = add_graph_io_common(str, nid, bits);
  node_internal[nid].set_graph_io_output();

  IO_port p(nid, original_pos, true);
  output_array.create_id(str, p);

  return nid;
}

std::string_view LGraph_Base::get_graph_input_name(Index_ID nid) const {
  I(node_internal[nid].is_graph_io_input());
  I(node_internal[nid].is_master_root());

  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.nid == nid) return it.get_name();
  }

  return unknown_io;
}

std::string_view LGraph_Base::get_graph_output_name(Index_ID nid) const {
  I(node_internal[nid].is_graph_io_output());
  I(node_internal[nid].is_master_root());

  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.nid == nid) return it.get_name();
  }

  return unknown_io;
}

std::string_view LGraph_Base::get_graph_input_name_from_pid(Port_ID pid) const {
  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.pos == pid) return it.get_name();
  }

  return unknown_io;
}

std::string_view LGraph_Base::get_graph_output_name_from_pid(Port_ID pid) const {
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.pos == pid) return it.get_name();
  }

  return unknown_io;
}

Port_ID LGraph_Base::get_graph_pid_from_nid(Index_ID nid) const {
  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.nid == nid) return p.pos;
  }

  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.nid == nid) return p.pos;
  }

  return 0;
}

Index_ID LGraph_Base::get_graph_input_nid_from_pid(Port_ID pid) const {
  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.pos == pid) return p.nid;
  }

  return 0;
}

Index_ID LGraph_Base::get_graph_output_nid_from_pid(Port_ID pid) const {
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    if (p.pos == pid) return p.nid;
  }

  return 0;
}

Node_Pin LGraph_Base::get_graph_input(std::string_view str) const {
  I(input_array.get_id(str) != 0);

  const auto &p = input_array.get_field(str);
  return Node_Pin(p.nid, p.pos, true);
}

Node_Pin LGraph_Base::get_graph_output(std::string_view str) const {
  I(output_array.get_id(str) != 0);

  const auto &p = output_array.get_field(str);
  return Node_Pin(p.nid, p.pos, false);
}

Index_ID LGraph_Base::create_node_space(Index_ID last_idx, Port_ID dst_pid, Index_ID master_nid, Index_ID root_nid) {
  Index_ID idx2 = create_node_int();

  I(node_internal[master_nid].is_master_root());
  I(node_internal[last_idx].get_master_root_nid() == master_nid);

  if (root_nid) {
    I(node_internal[root_nid].is_root());
    node_internal[idx2].clear_root();
    node_internal[idx2].set_nid(root_nid);
  } else {
    I(node_internal[idx2].is_root());
    node_internal[idx2].set_nid(master_nid);
  }
  I(node_internal[idx2].get_master_root_nid() == master_nid);

  if (node_internal[master_nid].is_graph_io()) {
    if (node_internal[master_nid].is_graph_io_input()) node_internal[idx2].set_graph_io_input();
    if (node_internal[master_nid].is_graph_io_output()) node_internal[idx2].set_graph_io_output();
    I(dst_pid == node_internal[master_nid].get_dst_pid());  // All the graph_io edges have the same port
  }
  node_internal[idx2].set_dst_pid(dst_pid);

  if (node_internal[last_idx].has_next_space()) {
    node_internal[last_idx].push_next_state(idx2);
    I(node_internal[idx2].has_space(true));
    return idx2;
  }

  const auto &dbg_master = node_internal[last_idx].get_master_root();
  int32_t     dbg_ni     = dbg_master.get_num_inputs();
  int32_t     dbg_no     = dbg_master.get_num_outputs();

  if (node_internal[last_idx].get_dst_pid() == dst_pid) {
    I(root_nid);
    // Nove stuff to idx2, legal

    node_internal[idx2].assimilate_edges(node_internal[last_idx]);
    I(node_internal[last_idx].has_next_space());

    I(node_internal[last_idx].get_master_root_nid() == master_nid);
    I(node_internal[idx2].get_master_root_nid() == master_nid);

    node_internal[last_idx].push_next_state(idx2);

    I(dbg_master.get_num_inputs() == dbg_ni);
    I(dbg_master.get_num_outputs() == dbg_no);

    if (!node_internal[last_idx].has_space(true)) {
      if (node_internal[idx2].has_space(true)) return idx2;
      // This can happen if 3 sedges transfered to 3 ledges in dest
      return create_node_space(idx2, dst_pid, master_nid, root_nid);
    }
    return last_idx;
  }

  // last_idx (pid1) -> idx2 (dst_pid) -> idx3 (pid1)

  Index_ID idx3 = create_node_int();

  // make space in idx so that we can push_next_state
  node_internal[idx3].set_dst_pid(node_internal[last_idx].get_dst_pid());
  node_internal[idx3].clear_root();
  if (node_internal[last_idx].is_root())
    node_internal[idx3].set_nid(last_idx);
  else
    node_internal[idx3].set_nid(node_internal[last_idx].get_nid());
  if (!node_internal[last_idx].has_next_space()) node_internal[idx3].assimilate_edges(node_internal[last_idx]);

  I(node_internal[last_idx].has_next_space());
  I(node_internal[idx2].has_next_space());
  // Link chain
  node_internal[last_idx].push_next_state(idx2);
  node_internal[idx2].push_next_state(idx3);

  // May or may not be I( node_internal[last_idx ].is_root());
  I(!node_internal[idx3].is_root());

  I(!node_internal[last_idx].is_last_state());
  I(!node_internal[idx2].is_last_state());
  I(node_internal[idx3].is_last_state());

  I(node_internal[last_idx].get_master_root_nid() == master_nid);
  I(node_internal[idx3].get_master_root_nid() == master_nid);
  I(node_internal[idx2].get_master_root_nid() == master_nid);

  I(node_internal[last_idx].get_master_root().get_num_inputs() == dbg_ni);
  I(node_internal[last_idx].get_master_root().get_num_outputs() == dbg_no);

  I(node_internal[idx2].has_space(true));
  return idx2;
}

void LGraph_Base::print_stats() const {
  double bytes = 0;

  size_t n_nodes = 1;
  size_t n_extra = 1;
  size_t n_roots = 1;
  for (size_t i = 0; i < node_internal.size(); i++) {
    if (node_internal[i].is_node_state()) {
      n_nodes++;
      if (node_internal[i].is_root()) n_roots++;
    } else {
      n_extra++;
    }
  }

  bytes += node_internal.size() * sizeof(Node_Internal);
  // bytes += node_type_op.size() * sizeof(Node_Type_Op);
  // bytes += node_delay.size()    * sizeof(Node_Delay);

  fmt::print("path:{} name:{}\n", path, name);
  fmt::print("  size:{} kbytes:{} bytes/size:{}\n", node_internal.size(), bytes / 1024, bytes / node_internal.size());
  fmt::print("  total root:{} node:{} extra:{}\n", n_roots, n_nodes, n_extra);
  fmt::print("  total bytes/root:{} bytes/node:{} bytes/extra:{}\n", bytes / n_roots, bytes / n_nodes, bytes / n_extra);

  bytes = node_internal.size() * sizeof(Node_Internal);
  fmt::print("  edges bytes/root:{} bytes/node:{} bytes/extra:{}\n", bytes / n_roots, bytes / n_nodes, bytes / n_extra);
}

Index_ID LGraph_Base::get_space_output_pin(Index_ID master_nid, Index_ID start_nid, Port_ID dst_pid, Index_ID root_nid) {
  I(node_internal[master_nid].is_root());

  I(node_internal[start_nid].is_node_state());
  if (node_internal[start_nid].has_space(true) && node_internal[start_nid].get_dst_pid() == dst_pid) {
    return start_nid;
  }

  // Look for space
  Index_ID idx = start_nid;

  while (true) {
    if (node_internal[idx].get_dst_pid() == dst_pid) {
      if (node_internal[idx].is_root()) I(root_nid == idx);

      if (node_internal[idx].has_space(true)) return idx;
    }

    if (node_internal[idx].is_last_state()) return create_node_space(idx, dst_pid, master_nid, root_nid);

    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
  }

  I(false);

  return 0;
}

Index_ID LGraph_Base::find_idx_from_pid_int(Index_ID nid, Port_ID pid) const {
  I(node_internal[nid].is_root());
  I(node_internal[nid].is_node_state());
  if (node_internal[nid].get_dst_pid() == pid) {
    return nid;
  }

  Index_ID idx = nid;
  while (true) {
    if (node_internal[idx].get_dst_pid() == pid) {
      return idx;
    }

    if (node_internal[idx].is_last_state()) {
      return 0;
    }

    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
  }

  I(false);
  return 0;
}

Index_ID LGraph_Base::find_idx_from_pid(Index_ID nid, Port_ID pid) const {
  Index_ID pos = find_idx_from_pid_int(nid, pid);
  I(pos);

  return pos;
}

Index_ID LGraph_Base::get_idx_from_pid(Index_ID nid, Port_ID pid) {
  Index_ID pos = find_idx_from_pid_int(nid, pid);
  if (pos) return pos;

  Index_ID root_nid;
  Index_ID idx_new = get_space_output_pin(nid, pid, root_nid);
  if (root_nid == 0) root_nid = idx_new;

  I(node_internal[root_nid].is_root());

  return idx_new;
}

void LGraph_Base::set_bits_pid(Index_ID nid, Port_ID pid, uint16_t bits) {
  Index_ID idx = get_idx_from_pid(nid, pid);
  set_bits(idx, bits);
}

uint16_t LGraph_Base::get_bits_pid(Index_ID nid, Port_ID pid) const {
  Index_ID idx = find_idx_from_pid(nid, pid);
  return get_bits(idx);
}

uint16_t LGraph_Base::get_bits_pid(Index_ID nid, Port_ID pid) {
  Index_ID idx = get_idx_from_pid(nid, pid);
  return get_bits(idx);
}

Index_ID LGraph_Base::get_space_output_pin(Index_ID start_nid, Port_ID dst_pid, Index_ID &root_nid) {
  I(node_internal[start_nid].is_root());
  I(node_internal[start_nid].is_node_state());
  if (node_internal[start_nid].has_space(false) && node_internal[start_nid].get_dst_pid() == dst_pid) {
    root_nid = start_nid;
    return start_nid;
  }

  // Look for space
  Index_ID idx = start_nid;
  root_nid     = 0;
  while (true) {
    if (node_internal[idx].get_dst_pid() == dst_pid) {
      if (node_internal[idx].is_root()) root_nid = idx;

      if (node_internal[idx].has_space(false)) {
        if (root_nid == 0) root_nid = start_nid;

        if (root_nid != start_nid) I(node_internal[start_nid].get_dst_pid() != node_internal[root_nid].get_dst_pid());
        I(node_internal[root_nid].is_root());
        return idx;
      }
    }

    if (node_internal[idx].is_last_state()) {
      Index_ID idx_new;
      idx_new = create_node_space(idx, dst_pid, start_nid, root_nid);
      if (root_nid == 0) root_nid = idx_new;
      I(node_internal[root_nid].is_root());
      return idx_new;
    }

    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
  }

  I(false);

  return 0;
}

Index_ID LGraph_Base::get_space_input_pin(Index_ID start_nid, Index_ID idx, bool large) {
  // FIXME: Change the get_space_input_pin to insert when dst_pid matches. Then
  // the forward and reverse edge can have src/dst pid
  //
  // The interface has to become more closer to get_space_output_pin

  I(large || node_internal[idx].is_node_state());
  if (node_internal[idx].has_space(large)) return idx;

  // Look for space
  while (true) {
    if (node_internal[idx].has_space(large)) return idx;

    if (node_internal[idx].is_last_state()) {
      I(node_internal[start_nid].is_root());
      Port_ID dst_pid = node_internal[start_nid].get_dst_pid();  // Inputs can go anywhere
      return create_node_space(idx, dst_pid, start_nid, start_nid);
    }

    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
  }

  I(false);

  return 0;
}

Index_ID LGraph_Base::add_edge_int(Index_ID dst_nid, Port_ID inp_pid, Index_ID src_nid, Port_ID dst_pid) {
  bool            sused;
  SEdge_Internal *sedge;
  LEdge_Internal *ledge;

  I(node_internal.size() > src_nid);
  I(node_internal.size() > dst_nid);

  // Do not point to intermediate nodes which can be remapped, just root nodes
  I(node_internal[dst_nid].is_master_root());
  I(node_internal[src_nid].is_master_root());

  // WARNING: Graph IO have alphabetical port IDs assigned to be mapped between
  // graphs. It should not use local port b
  if (node_internal[src_nid].is_graph_io()) {
    // dst_pid = node_internal[src_nid].get_dst_pid();
    dst_pid = 0;
  } else {
    I(node_internal[src_nid].get_dst_pid() == 0);
  }

  if (node_internal[dst_nid].is_graph_io()) {
    // inp_pid = node_internal[dst_nid].get_dst_pid();
    inp_pid = 0;
  } else {
    I(node_internal[dst_nid].get_dst_pid() == 0);
  }

  I(node_internal[dst_nid].is_node_state());
  I(node_internal[src_nid].is_node_state());

#ifndef NDEBUG
  // Do not insert twice check
  for (const auto &v : out_edges(src_nid)) {
    if (v.get_idx() == dst_nid && v.get_dst_pid() == dst_pid && v.get_inp_pid() == inp_pid) {
      I(false);  // edge added twice
    }
  }
#endif

  Index_ID idx;
  //-----------------------
  // ADD output edge in source (src_nid) with dst_pid to the destination (dst_nid) with inp_pid
  Index_ID root_nid = 0;
  idx               = get_space_output_pin(src_nid, dst_pid, root_nid);
  I(root_nid != 0);
  I(node_internal[root_nid].is_root());

  int o = node_internal[idx].next_free_output_pos();

  sedge = (SEdge_Internal *)&node_internal[idx].sedge[o];
  sused = sedge->set(dst_nid, inp_pid, false);

  if (sused) {
    node_internal[idx].inc_outputs();
  } else {
    idx = get_space_output_pin(src_nid, idx, dst_pid, root_nid);
    o   = node_internal[idx].next_free_output_pos() - 2;
    node_internal[idx].inc_outputs(true);  // WARNING: Before next_free_output_pos to reserve space (decreasing insert)

    ledge = (LEdge_Internal *)(&node_internal[idx].sedge[o]);
    ledge->set(dst_nid, inp_pid, false);
  }
  Index_ID out_idx = idx;

  //-----------------------
  // Reverse from before:
  // ADD input edge in destination (dst_nid) with ANY inp_pid (0) to  (src_nid) with dst_pid

  Index_ID inp_root_nid = 0;
  idx                   = get_space_output_pin(dst_nid, inp_pid, inp_root_nid);
  I(inp_root_nid != 0);
  I(node_internal[inp_root_nid].is_root());
  int i = node_internal[idx].next_free_input_pos();

  sedge = (SEdge_Internal *)&node_internal[idx].sedge[i];
  sused = sedge->set(src_nid, dst_pid, true);

  if (sused) {
    node_internal[idx].inc_inputs();
  } else {
    idx   = get_space_output_pin(dst_nid, idx, inp_pid, inp_root_nid);
    i     = node_internal[idx].next_free_input_pos();
    ledge = (LEdge_Internal *)(&node_internal[idx].sedge[i]);
    ledge->set(src_nid, dst_pid, true);

    node_internal[idx].inc_inputs(true);  // WARNING: after next_free_input_pos (increasing insert)
  }

#ifndef NDEBUG
  Index_ID master_nid = src_nid;
  I(node_internal[master_nid].is_master_root());
  Index_ID          j = master_nid;
  std::set<Port_ID> mset;

  while (true) {
    if (node_internal[j].is_root()) {
      if (mset.find(node_internal[j].get_dst_pid()) != mset.end())
        fmt::print("multiple nid:{} port:{}\n", j, node_internal[j].get_dst_pid());

      I(mset.find(node_internal[j].get_dst_pid()) == mset.end());
    }
    mset.insert(node_internal[j].get_dst_pid());

    if (node_internal[j].is_last_state()) break;
    j = node_internal[j].get_next();
  }
#endif

  if (root_nid != src_nid) I(node_internal[src_nid].get_dst_pid() != node_internal[root_nid].get_dst_pid());

  if (node_internal[src_nid].get_dst_pid() == node_internal[out_idx].get_dst_pid() && src_nid != out_idx)
    I(!node_internal[out_idx].is_root());

  I(node_internal[root_nid].is_root());
  return root_nid;
}

void LGraph_Base::del_edge(const Edge &edge) { Node_Internal::get(&edge).del(edge); }

void LGraph_Base::del_node(Index_ID idx) {
  // TODO: do this more effiently (no need to build iterator)

  fmt::print("del_node {}\n", idx);

  I(node_internal[idx].is_master_root());
  // Deleting can break the iterator. Restart each time
  bool deleted;
  do {
    deleted = false;
    for (auto &c : inp_edges(idx)) {
      fmt::print("del_node {} inp {}\n", idx, c.get_self_idx());
      I(Node_Internal::get(&c).get_master_root_nid() == idx);
      node_internal[c.get_self_idx()].del(c);
      deleted = true;
      break;
    }
  } while (deleted);

  do {
    deleted = false;
    for (auto &c : out_edges(idx)) {
      fmt::print("del_node {} out {}\n", idx, c.get_self_idx());
      I(Node_Internal::get(&c).get_master_root_nid() == idx);
      node_internal[c.get_self_idx()].del(c);
      deleted = true;
      break;
    }
  } while (deleted);

  I(node_internal[idx].get_inp_pos() == 0);
  I(node_internal[idx].get_out_pos() == 0);

  // clear child nodes
  del_int_node(idx);
}

void LGraph_Base::del_int_node(Index_ID idx) {
  if (!node_internal[idx].is_last_state()) {
    del_int_node(node_internal[idx].get_next());
  }
  node_internal[idx].reset();
}

Edge_iterator LGraph_Base::out_edges(Index_ID idx) const {
  I(node_internal[idx].is_master_root());

  const SEdge *s = 0;
  while (true) {
    s = node_internal[idx].get_output_begin();
    if (node_internal[idx].is_last_state()) break;
    if (node_internal[idx].has_local_outputs()) break;
    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
  }

  const SEdge *e = 0;
  if (node_internal[idx].has_local_outputs()) e = node_internal[idx].get_output_end();

  while (true) {
    if (node_internal[idx].is_last_state()) break;
    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
    if (node_internal[idx].has_local_outputs()) e = node_internal[idx].get_output_end();
  }

  if (e == 0)  // empty list of outputs
    e = s;

  return Edge_iterator(s, e, false);
}

Edge_iterator LGraph_Base::inp_edges(Index_ID idx) const {
  I(node_internal[idx].is_master_root());

  const SEdge *s = 0;

  while (true) {
    s = node_internal[idx].get_input_begin();
    if (node_internal[idx].is_last_state()) break;
    if (node_internal[idx].has_local_inputs()) break;
    Index_ID idx2 = node_internal[idx].get_next();
    if (idx2 >= node_internal.size()) break;
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
  }

  const SEdge *e = 0;
  if (node_internal[idx].has_local_inputs()) e = node_internal[idx].get_input_end();

  while (true) {
    if (node_internal[idx].is_last_state()) break;
    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
    if (node_internal[idx].has_local_inputs()) {
      e = node_internal[idx].get_input_end();
    }
  }

  if (e == 0)  // empty list of inputs
    e = s;

  return Edge_iterator(s, e, true);
}

void LGraph_Base::each_sub_graph_fast_direct(
    const std::function<bool(const Index_ID &, const Lg_type_id &, std::string_view)> fn) const {
  const bm::bvector<> &bm  = get_sub_graph_ids();
  Index_ID             cid = bm.get_first();
  while (cid) {
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_root());

    auto       iname = get_node_instancename(cid);
    Lg_type_id lgid  = subgraph_id_get(cid);

    bool cont = fn(cid, lgid, iname);
    if (!cont) return;

    cid = bm.get_next(cid);
  }
}

void LGraph_Base::each_root_fast_direct(std::function<bool(const Index_ID &)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;

    bool cont = f1(ni.get_nid());
    if (!cont) return;
  }
}
