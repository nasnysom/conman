
#include <boost/bind.hpp>

#include <rtt/extras/SlaveActivity.hpp>

#include <conman/scheme.h>
#include <conman/hook.h>

// function_property_map isn't available until version 1.51
#include <boost/version.hpp>
#if BOOST_VERSION / 100000 >= 1 && BOOST_VERSION / 100 % 1000 >= 51
#include <boost/property/function_property_map.hpp>
#else
#include "function_property_map.hpp"
#endif

ORO_LIST_COMPONENT_TYPE(conman::Scheme);

using namespace conman;

Scheme::Scheme(std::string name) 
 : RTT::TaskContext(name)
{
  // Add operations
  this->addOperation("addBlock", 
      (bool (Scheme::*)(const std::string&))&Scheme::addBlock, this, 
      RTT::OwnThread)
    .doc("Add a conman block into this scheme.");
  
  this->addOperation("removeBlock", 
      (bool (Scheme::*)(const std::string&))&Scheme::removeBlock, this, 
      RTT::OwnThread)
    .doc("Remove a conman block from this scheme.");

  this->addOperation("getBlocks", 
      &Scheme::getBlocks, this, 
      RTT::OwnThread)
    .doc("Get the list of all blocks.");

  // Block runtime management
  this->addOperation("enableBlock", 
      (bool (Scheme::*)(const std::string&, bool))&Scheme::enableBlock, this, 
      RTT::OwnThread)
    .doc("Enable a block in this scheme.");

  this->addOperation("disableBlock", 
      (bool (Scheme::*)(const std::string&))&Scheme::disableBlock, this, 
      RTT::OwnThread)
    .doc("Disable a block in this scheme.");

  this->addOperation("switchBlocks", 
      &Scheme::switchBlocks, this, 
      RTT::OwnThread)
    .doc("Simultaneousy enable and disable a list of blocks, any block not in either list will remain in its current state.");

  this->addOperation("setEnabledBlocks", 
      &Scheme::setEnabledBlocks, this, 
      RTT::OwnThread)
    .doc("Set the list running blocks, any block not on the list will be disabled.");
}


///////////////////////////////////////////////////////////////////////////////

std::vector<std::string> Scheme::getBlocks() 
{
  using namespace conman::graph;

  std::vector<std::string> block_names(blocks_.size());

  std::vector<std::string>::iterator str_it = block_names.begin();
  std::map<std::string,DataFlowVertex::Ptr>::iterator block_it = 
    blocks_.begin();

  for(; str_it != block_names.end() && block_it != blocks_.end();
      ++str_it, ++block_it)
  {
    *str_it = block_it->first;
  }

  return block_names;
}

void getBlocks(const std::vector<std::string> &blocks)
{
  blocks = this->getBlocks();
}

bool Scheme::addBlock(const std::string &block_name)
{
  RTT::Logger::In in("Scheme::addBlock(string)");

  // Make sure the block exists as a peer of the scheme
  if(!this->hasPeer(block_name)) {
    RTT::TaskContext::PeerList peers = this->getPeerList();

    RTT::log(RTT::Error)
      << "Requested block to add named \"" << block_name << "\" was not a peer"
      "of this Scheme." << std::endl
      << "  Available blocks include:" << std::endl;

    for(RTT::TaskContext::PeerList::iterator it = peers.begin();
        it != peers.end();
        ++it) 
    {
      RTT::log(RTT::Error) << "    " << *it << std::endl;
    }

    RTT::log(RTT::Error) << RTT::endlog();

    return false;
  }

  // Get the newly loaded block
  RTT::TaskContext *new_block = this->getPeer(block_name);

  // Add the block to the graphs
  return this->addBlock(new_block);
}

bool Scheme::addBlock(RTT::TaskContext *new_block)
{
  using namespace conman::graph;

  RTT::Logger::In in("Scheme::addBlock");

  // Nulls are bad
  if(new_block == NULL) {
    RTT::log(RTT::Error) << "Requested block to add is NULL." << RTT::endlog();
    return false;
  }

  // Make sure the block has the conman hook service
  if(!conman::Hook::HasHook(new_block)) {
    RTT::log(RTT::Error) << "Requested block to add does not have the conman"
      " hook service." << RTT::endlog();
    return false;
  }

  // Try to add this block as a peer
  if(!this->connectPeers(new_block)) {
    RTT::log() << RTT::Logger::Error << "Could not connect peer: " <<
      new_block->getName() << RTT::endlog();
  }
  
  // Get the block name
  const std::string block_name = new_block->getName();

  // Create the vertex properties
  DataFlowVertex::Ptr new_vertex = boost::make_shared<DataFlowVertex>();
  new_vertex->index = blocks_.size();
  new_vertex->latched_input = false;
  new_vertex->latched_output = false;
  new_vertex->block = new_block;
  new_vertex->hook = conman::Hook::GetHook(new_block);

  // Add this block to the set of blocks
  blocks_[block_name] = new_vertex;
  // Add this block to the block index (used for re-indexing)
  block_indices_.push_back(new_vertex);

  // Model the block in the DFG and ESG structures
  if(!addBlockToGraph(new_vertex)) {
    // Cleanup on failure
    RTT::log() << RTT::Logger::Error << "Could not add TaskContext \"" <<
      block_name <<"\" to the scheme." << RTT::endlog();
    // Remove the block
    if(!this->removeBlock(new_block)) {
      // This is 
      RTT::log(RTT::Fatal) << "Could not clean up block \"" << block_name <<
        "\" when trying to remove it. Something is terribly wrong." <<
        RTT::endlog();
    }
    return false;
  }

  // Compute conflicts for this block and represent them in the RCG
  this->computeConflicts(new_vertex);

  // Set the block's activity to be a slave to the scheme's
  new_block->setActivity(
      new RTT::extras::SlaveActivity(
          this->getActivity(),
          new_block->engine()));

  // Print out the ordering
  this->printExecutionOrdering();

  return true;
}

void printExecutionOrdering() 
{
  RTT::log(RTT::Info) << ;
  std::vector<std::string> ordered_names;
  ordered_names.reserve(exec_ordering_.size());

  for(ExecutionOrdering::const_iterator it = exec_ordering_.begin();
      it != exec_ordering_.end();
      ++it) 
  {
    ordered_names.push_back(flow_graph_[*it]->block->getName());
  }
  RTT::log(RTT::Info) << "Scheme ordering: [ " <<
    boost::algorithm::join(ordered_names, ",") << " ] " << RTT::endlog();
}

///////////////////////////////////////////////////////////////////////////////

bool Scheme::removeBlock(const std::string &block_name)
{
  RTT::Logger::In in("Scheme::removeBlock(string)");

  // Make sure the block exists as a peer of the scheme
  if(!this->hasPeer(block_name)) {
    RTT::TaskContext::PeerList peers = this->getPeerList();

    RTT::log(RTT::Error)
      << "Requested block to remove named \"" << block_name << "\" was not a"
      " peer" "of this Scheme." << std::endl << "  Available blocks include:"
      << std::endl;

    for(RTT::TaskContext::PeerList::iterator it = peers.begin();
        it != peers.end();
        ++it) 
    {
      RTT::log(RTT::Error) << "    " << *it << std::endl;
    }

    RTT::log(RTT::Error) << RTT::endlog();

    return false;
  }

  // Get the newly loaded block
  RTT::TaskContext *block = this->getPeer(block_name);

  // Add the block to the graphs
  return this->removeBlock(block);
}

bool Scheme::removeBlock(
    RTT::TaskContext *block)
{
  using namespace conman::graph;

  RTT::Logger::In in("Scheme::removeBlock");

  // Succeed if the block isn't already in the scheme
  if(blocks_.find(block->getName()) == blocks_.end()) {
    return true;
  }

  // Make sure the block has the conman hook
  if(!Hook::HasHook(block)) {
    return false;
  }

  // Check if the block is in the scheme
  if(flow_vertex_map_.find(block) != flow_vertex_map_.end()) {
    // Get the vertex properties pointer
    DataFlowVertex::Ptr vertex = flow_graph[flow_vertex_map_[block]];
    // Remove the vertex from the graph
    if(!this->removeBlockFromGraph(vertex)) {
      // Complain
      RTT::log(RTT::Fatal) << "Failed to remove block \"" << block->getName()
        << "\" from scheme." <<
        RTT::endlog();
      // Set failure
      return false;
    }
  }

  // Remove block from conflict graph / map
  boost::clear_vertex(conflict_vertex_map_[block], conflict_graph_);
  boost::remove_vertex(conflict_vertex_map_[block], conflict_graph_);
  conflict_vertex_map_.erase(block);

  // Remove the block from the block map
  blocks_.erase(block->getName());

  // Re-index the vertices 
  unsigned int i=0;
  std::list<DataFlowVertex::Ptr>::iterator it = block_indices_.begin();
  for(; it != block_indices_.end(); )
  {
    // Remove the block when we get to it
    if((*it)->block == block) {
      it = block_indices_.erase(it);
    } else {
      // Update index of the blocks we don't remove
      (*it)->index = i;
      // Increment the iterator
      ++it;
      // Increment the index
      ++i;
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////

bool Scheme::createGroup(
    const std::string &group_name,
    const std::vector<std::string> &members) 
{ 
  RTT::Logger::In in("Scheme::createGroup");

  // Check if the group name collides with a real block
  if(blocks_.find(group_name) != blocks_.end())
  {
    RTT::log(RTT::Error) << "Block group named \"" << group_name << "\""
      "cannot be created because a block with the same name already exists."
      << RTT::endlog();
    return false;
  }

  // Make sure all the blocks are in the scheme
  for(std::vector<std::string>::const_iterator it=grouped_blocks.begin();
      it != grouped_blocks.end();
      ++it)
  {
    // Check if the block is in the scheme
    if( blocks_.find(*it) == blocks_.end() &&
        block_groups_.find(*it) == block_groups_.end())
    {
      RTT::log(RTT::Error) << "Block named \"" << *it << "\""
        "is not in the scheme." << RTT::endlog();
      return false;
    }
  }

  // Check if the group already exists
  if(block_groups_.find(group_name) != block_groups_.end()) {
    RTT::log(RTT::Warning) << "Block group named \"" << group_name << "\""
      " already exists. Over-writing." << RTT::endlog();
  }

  // Flatten and store the group 
  block_groups_[group_name] = 
    std::set<std::string>(grouped_blocks.begin(), grouped_blocks.end());

  return true; 
}

bool Scheme::addToGroup(
    const std::string &group_name,
    const std::string &new_block) 
{
  RTT::Logger::In in("Scheme::addToGroup");

  // Check if the group exists
  std::map<std::string, std::set<std::string> >::iterator group =
    block_groups_.find(group_name);
  if(group == block_groups_.end()) {
    return false;
  }

  // Check if the block is in the scheme
  if( blocks_.find(new_block) == blocks_.end()) {
    RTT::log(RTT::Error) << "Block named \"" << new_block << "\" is not in the"
      " scheme." << RTT::endlog();
    return false;
  }

  // Return the group constituents
  group->second.insert(new_block);

  return true; 
}

bool Scheme::removeFromGroup(
    const std::string &group_name,
    const std::string &block) 
{
  // Check if the group exists
  std::map<std::string, std::set<std::string> >::iterator group = 
    block_groups_.find(group_name);

  if(group == block_groups_.end()) {
    return false;
  }

  // Check if the block is in the group
  if( group->second.find(block) == group->second.end()) {
    // It's already gone
    return true;
  }

  // Remove the block from the group
  group->second.erase(block);

  return true; 
}

bool Scheme::disbandGroup( const std::string &group_name) 
{
  // Check if the group exists
  if(block_groups_.find(group_name) != block_groups_.end()) {
    block_groups_.erase(group_name);
  }
  
  return true; 
}

bool Scheme::getGroup(
    const std::string &group_name,
    std::vector<std::string> &members) 
{
  // Expand the group recursively
  std::set<std::string> member_set;
  bool success = getGroupMembers(group_name, member_set);

  // Copy the set to vector
  members.assign(member_set.begin(), member_set.end());

  return success;
}

bool Scheme::getGroupMembers(
    const std::string &group_name,
    std::set<std::string> &member_set) 
{
  // Check if the group is a single block
  if(blocks_.find(group_name)) {
    member_set.insert(group_name);
    return true;
  }

  // Check if the group exists
  GroupMap::iterator group = block_groups_.find(group_name);

  if(group == block_groups_.end()) {
    return false;
  }

  // Return the group members
  success = true;
  for(std::set<std::string>::const_iterator it=group->second.begin();
      it != group->second.end();
      ++it)
  {
    success &= this->getGroupMembers(it, member_set);
  }

  return success; 
}

///////////////////////////////////////////////////////////////////////////////

bool latchConnections(
    const std::string &source_name,
    const std::string &sink_name
    const bool latch)
{
  // Self-loops are implicitly latched
  if(source_name == sink_name) {
    return true;
  }

  // Check if the source is a group name
  std::vector<std::string> sources, sinks;

  this->getGroup(source_name, sources);
  this->getGroup(sink_name, sinks);

  return this->latchConnections(source, sinks, latch);
}

bool latchConnections(
    const std::vector<std::string> &source_names,
    const std::vector<std::string> &sink_names,
    bool latch)
{
  // Latch connections between all sources and sinks
  bool success = true;
  for(std::vector<std::string>::const_iterator source_it = sources.begin();
      source_it != sources.end();
      ++source_it)
  {
    for(std::vector<std::string>::const_iterator sink_it = sinks.begin();
        sink_it != sinks.end();
        ++sink_it)
    {
      RTT::TaskContext source = this->getPeer(*source_it)
      RTT::TaskContext sink = this->getPeer(*sink_it)
      success &= this->latchConnections(source, sink, latch, false);
    }
  }

  return success;
}

bool latchConnections(
    RTT::TaskContext *source,
    RTT::TaskContext *sink,
    const bool latch,
    const bool strict)
{
  using namespace conman::graph;

  // Make sure source and sink are valid
  if(!source || !sink) {
    return false;
  }

  // Get edge between the source and sink
  DataFlowEdgeDescriptor edge;
  bool edge_found;
  boost::tie(edge, edge_found) = boost::edge(
      flow_vertex_map_[source], 
      flow_vertex_map_[sink], 
      flow_graph_);

  // Latch the edge
  if(edge_found) {
    // Set the latch flag
    flow_graph_[edge]->latched = latch;

    // Either remove or add the edge in the execution graph
    if(letch) {
      boost::remove_edge(
          exec_vertex_map_[source],
          exec_vertex_map_[sink],
          exec_graph_);
    } else {
      boost::add_edge(
          exec_vertex_map_[source],
          exec_vertex_map_[sink],
          flow_graph_[edge],
          exec_graph_);
    }
  } else if(strict) {
    // Only error if strict
    RTT::log(RTT::Error) << "Tried to " << 
      ((latch) ? ("latch") : ("un_latch"))
      << " a non-existent connection." <<
      RTT::logend();
    return false;
  }

  return true;
}

bool latchInputs(const std::string &sink_name, const bool latch)
{
  std::vector<std::string> sources, sinks;

  // Get the sources (all blocks)
  this->getBlocks(sources);
  // Get the sinks (potentially a group)
  this->getGroup(sink_name, sinks);
  
  // Set latching flags for all vertices
  for(std::vector<std::string>::const_iterator it=sinks.begin();
      it != sinks.end();
      ++it)
  {
    blocks_[*it]->latched_input = latch;
  }

  return this->latchConnections(sources, sinks, latch);
}

bool latchInputs(RTT::TaskContext *block, const bool latch)
{
  return block && this->latchInputs(block->getName(), latch);
}

bool latchOutputs(const std::string &name, const bool latch)
{
  std::vector<std::string> sources, sinks;

  // Get the sources (potentially a group)
  this->getGroup(source_name, sources);
  // Get the sinks (all blocks)
  this->getBlocks(sinks);
  
  // Set latching flags for all vertices
  for(std::vector<std::string>::const_iterator it=sources.begin();
      it != sources.end();
      ++it)
  {
    blocks_[*it]->latched_output = latch;
  }

  return this->latchConnections(sources, sinks, latch);
}

bool latchOutputs(RTT::TaskContext *block, const bool latch)
{
  return block && this->latchOutputs(block->getName(), latch);
}

///////////////////////////////////////////////////////////////////////////////

int Scheme::latchCount(
    const conman::graph::DataFlowPath &path,
    const conman::graph::DataFlowGraph &flow_graph)
{
  using namespace conman::graph;

  // If there are fewer than two vertices, there are no edges on the path
  if(path.size() < 2) {
    return 0;
  }

  int latch_count = 0;

  // Iterate over pairs of vertices
  for(size_t i=1; i<path.size(); i++) {
    DataFlowVertex u = path.at(i-1);
    DataFlowVertex v = path.at(i);

    // Get parallel edges
    DataFlowOutEdgeIterator par_edges_it, par_edges_end;
    boost::tie(par_edges_it, par_edges_end) =
      boost::edge_range(u, v, flow_graph);

    // Check each parallel edge for latching
    for(;par_edges_it != par_edges_end; ++par_edges_end) {
      if(flow_graph[*par_edges_it]->latched) {
        latch_count++;
        break;
      }
    }
  }

  return latch_count;
}

int Scheme::computeCycles(
    const conman::graph::DataFlowGraph &data_flow_graph,
    std::vector<conman::graph::DataFlowPath> &cycles)
{
  using namespace conman::graph;

  // Check if the graph has no cycles (this can be done very fast)
  if(this->executable()) {
    return 0;
  }

  // Construct a cycle visitor for extracting cycles
  FlowCycleVisitor visitor(cycles);

  try {
    // Find all cycles 
    boost::tiernan_all_cycles(
        data_flow_graph,
        visitor);
  }

  return cycles.size();
}

bool Scheme::computeSchedule(
    const conman::graph::DataFlowGraph &data_flow_graph
    conman::graph::ExecutionOrdering &ordering, 
    const bool quiet)
{
  try{
    // Recompute the topological sort
    // NOTE: We need to use an external vertex index property for this
    // algorithm to work since our adjacency_list uses a list as the underlying
    // vertex data structure. See the documentation for DataFlowVertexIndex for
    // more info.
    boost::topological_sort( 
        data_flow_graph, 
        std::front_inserter(ordering),
        boost::vertex_index_map(
            boost::make_function_property_map<DataFlowVertexDescriptor>(
                boost::bind(&DataFlowVertexIndex,_1,data_flow_graph))));

  } catch(std::exception &ex) {
    // Complain unless quiet flag is true
    if(!quiet) {
      RTT::log(RTT::Error)
        << "Cannot regenerate topological ordering in conman scheme because: "
        << ex.what() << RTT::endlog();
    }
    return false;
  }
}


///////////////////////////////////////////////////////////////////////////////

bool Scheme::executable()
{
  using namespace conman::graph;

  // Compute the schedule on a throw-away ordering
  ExecutionOrdering ordering;
  this->computeSchedule(execution_scheduling_graph_, ordering, true)

  return true;
}

int Scheme::getExecutionCycles(
    std::vector<std::vector<std::string> > &component_cycles)
{
  using namespace conman::graph;
  std::vector<DataFlowPath> cycles;

  this->computeCycles(execution_scheduling_graph_, cycles);

  // Clear cycle component names
  component_cycles.resize(cycles.size());

  // Copy the names of the components associated with the verticies for each
  // cycle
  for(size_t c=0; c < cycles.size(); c++) {
    component_cycles[c].resize(v);
    for(size_t v=0; v < cycles[c].size(); v++) {
      component_cycles[c][v] = 
        execution_scheduling_graph_[cycles[c][v]]->task->getName();
    }
  }

  return component_cycles.size();
}

///////////////////////////////////////////////////////////////////////////////

void Scheme::computeConflicts() 
{
  std::map<std::string,graph::DataFlowVertex::Ptr>::iterator it;
  for(it = blocks_.begin(); it != blocks_.end(); ++it) {
    this->computeConflicts(it->second->block);
  }
}

void Scheme::computeConflicts(const std::string &block_name) 
{
  if(blocks_.find(block_name) != blocks_.end()) {
    this->computeConflicts(blocks_[block_name]);
  }
}

void Scheme::computeConflicts(const std::vector<std::string> &block_names)
{
  for(std::vector<std::string>::const_iterator it = block_names.begin();
      it != block_names.end();
      ++it)
  {
    this->computeConflicts(*it);
  }
}

void Scheme::computeConflicts(conman::graph::DataFlowVertex::Ptr vertex)
{
  using namespace conman::graph;

  RTT::TaskContext *& block = vertex->block;

  if(conflict_vertex_map_.find(block) == conflict_vertex_map_.end()) {
    // Add this block to the conflict graph / map
    conflict_vertex_map_[block] = boost::add_vertex(vertex,conflict_graph_);
  }

  // Iterator for out edges
  DataFlowOutEdgeIterator out_edge_it, out_edge_end;
  DataFlowInEdgeIterator in_edge_it, in_edge_end;

  // Get all output ports for this block
  boost::tie(out_edge_it, out_edge_end) =
    boost::out_edges(flow_vertex_map_[block], flow_graph_);

  // Handle conflicts resulting from each output port
  for(;out_edge_it != out_edge_end; ++out_edge_it) {
    // Get a reference to the edge properties for convenience
    DataFlowEdge::Ptr edge = flow_graph_[*out_edge_it];

    // Get a reference to the vertex properties of the sink for convenience
    DataFlowVertexDescriptor sink_vertex_descriptor = 
      boost::target(*out_edge_it, flow_graph_);

    DataFlowVertex::Ptr sink_vertex = 
      flow_graph_[sink_vertex_descriptor];

    // Get the exclusivity of this port
    const conman::Exclusivity::Mode mode =
      sink_vertex->hook->getInputExclusivity(edge->sink_port->getName());

    // Only exclusive ports can induce conflicts
    if(mode != conman::Exclusivity::EXCLUSIVE) {
      continue;
    }

    // Get input edges for the sink vertex
    boost::tie(in_edge_it, in_edge_end) =
      boost::in_edges(sink_vertex_descriptor, flow_graph_);

    // Add conflicts with each other block that also has a connection to this
    // input port
    for(;in_edge_it != in_edge_end; ++in_edge_it) {
      // Pointer comparison to check if this edge corresponds to the sink port 
      if(flow_graph_[*in_edge_it]->sink_port != edge->sink_port) {
        continue;
      }

      // Add conflict between the seed block and the source block for this edge
      DataFlowVertex::Ptr conflicting_vertex =
        flow_graph_[boost::source(*in_edge_it,flow_graph_)];

      // Make sure the block is in the conflict map, and isn't itself
      if( conflict_vertex_map_.find(block) == conflict_vertex_map_.end() ||
          conflict_vertex_map_.find(conflicting_vertex->block) == conflict_vertex_map_.end() ||
          block == conflicting_vertex->block) 
      {
        continue;
      }

      // Add an edge in the conflict graph
      add_edge(
          conflict_vertex_map_[block],
          conflict_vertex_map_[conflicting_vertex->block],
          conflict_graph_);

      // Debug output
      RTT::log(RTT::Debug) << "Added conflict between blocks "<<
        block->getName() << " and " <<
        conflicting_vertex->block->getName() << RTT::endlog();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////

bool Scheme::addBlockToGraphs(conman::graph::DataFlowVertex::Ptr new_vertex)
{
  using namespace conman::graph;

  RTT::Logger::In in("Scheme::addBlockToGraph");

  // Make sure the vertex isn't null
  if(!new_vertex) {
    RTT::log(RTT::Error) << "DataFlowVertex::Ptr is NULL." << RTT::endlog();
    return false;
  }

  // Get a reference to the block pointer
  TaskContext *&new_block = new_vertex->block;

  // Make sure the block isn't null
  if(new_block == NULL) {
    RTT::log(RTT::Error) << "TaskContext is NULL." << RTT::endlog();
    return false;
  }

  // Make sure the block has the conman hook service
  if(!conman::Hook::HasHook(new_block)) {
    RTT::log(RTT::Error) << "Requested block to add does not have the conman"
      "hook service." << RTT::endlog();
    return false;
  }

  // Add this block to the DFG & ESG
  flow_vertex_map_[new_block] = boost::add_vertex(new_vertex, flow_graph_);
  exec_vertex_map_[new_block] = boost::add_vertex(new_vertex, exec_graph_);

  RTT::log(RTT::Debug) << "Created vertex: "<< new_vertex->index << " (" <<
    flow_vertex_map_[new_block] ", " << exec_vertex_map_[new_block] << ")" <<
    RTT::endlog();

  // Regenerate the topological ordering
  if(!this->regenerateGraphs()) {
    // Report error if we can't regenerate the graphs
    RTT::log(RTT::Error) << "Cannot connect block \"" << new_block->getName()
      << "\" in conman scheme." << RTT::endlog();

    // Clean up this graph (but not the others, yet)
    this->removeBlockFromGraph(new_vertex);

    return false;
  }

  return true;
}

bool Scheme::removeBlockFromGraphs(conman::graph::DataFlowVertex::Ptr vertex)
{
  using namespace conman::graph;

  RTT::Logger::In in("Scheme::removeBlockFromGraph");

  // Succeed if the vertex already doesn't exist
  if(flow_vertex_map_.find(vertex->block) == flow_vertex_map_.end()) {
    return true;
  }

  // Remove the edges, the vertex itself, and the reference in the flow map
  boost::clear_vertex(flow_vertex_map_[vertex->block], flow_graph_);
  boost::remove_vertex(flow_vertex_map_[vertex->block], flow_graph_);
  flow_vertex_map_.erase(vertex->block);

  // Remove the edges, the vertex itself, and the reference in the exec map
  boost::clear_vertex(exec_vertex_map_[vertex->block], exec_graph_);
  boost::remove_vertex(exec_vertex_map_[vertex->block], exec_graph_);
  exec_vertex_map_.erase(vertex->block);

  // Remove the edges, the vertex itself, and the reference in the conflict map
  boost::clear_vertex(conflict_vertex_map_[vertex->block], conflict_graph_);
  boost::remove_vertex(conflict_vertex_map_[vertex->block], conflict_graph_);
  conflict_vertex_map_.erase(vertex->block);

  // Regenerate the graph without the vertex
  return this->regenerateGraphs();
}

bool Scheme::regenerateGraphs()
{
  using namespace conman::graph;

  RTT::Logger::In in("Scheme::regenerateGraph");

  // Initialize the modification flag
  bool topology_modified = ordering.size() != flow_vertex_map.size();

  // Iterate over all vertex structures
  std::map<std::string, conman::graph::DataFlowVertex::Ptr>::iterator vert_it;
  for(vert_it = blocks_.begin(); vert_it != vert_it_end; ++vert_it) 
  {
    // Temporary variable for readability
    DataFlowVertex::Ptr source_vertex = vert_it->second;

    // Get the output ports for a given taskcontext
    const std::vector<RTT::base::PortInterface*> &ports =
      source_vertex->block->ports()->getPorts();

    // Create graph arcs for each port between blocks
    std::vector<RTT::base::PortInterface*>::const_iterator port_it;
    for(port_it = ports.begin(); port_it != ports.end(); ++port_it) 
    {
      // Get the port, for readability
      const RTT::base::PortInterface *port = *port_it;

      // Only start from output ports
      if(!dynamic_cast<const RTT::base::OutputPortInterface*>(port)) {
        continue;
      }

      // Get the port connections (to get endpoints)
      std::list<RTT::internal::ConnectionManager::ChannelDescriptor> channels = port->getManager()->getChannels();
      std::list<RTT::internal::ConnectionManager::ChannelDescriptor>::iterator channel_it;

      // Create graph arcs for each connection
      for(channel_it = channels.begin(); channel_it != channels.end(); ++channel_it) 
      {
        // Get the connection descriptor
        RTT::base::ChannelElementBase::shared_ptr connection = channel_it->get<1>();

        // Pointers to the endpoints of this connection
        RTT::base::PortInterface  
          *source_port = connection->getInputEndPoint()->getPort(), 
          *sink_port = connection->getOutputEndPoint()->getPort();

        // Make sure the ports and components are not null
        if( source_port == NULL && source_port->getInterface() == NULL
            && sink_port == NULL && sink_port->getInterface() == NULL) 
        {
          continue;
        }

        // Get the source and sink components
        RTT::TaskContext
          *source_block = source_port->getInterface()->getOwner(),
          *sink_block = sink_port->getInterface()->getOwner();

        // Make sure both blocks are in the DFG and ESG
        if( flow_vertex_map_.find(source_block) == flow_vertex_map_.end() || 
            flow_vertex_map_.find(sink_block)   == flow_vertex_map_.end() ||
            exec_vertex_map_.find(source_block) == exec_vertex_map_.end() || 
            exec_vertex_map_.find(sink_block)   == exec_vertex_map_.end()) 
        {
          continue;
        }

        // Get the source and sink flow vertex descriptors
        DataFlowVertexDescriptor flow_source_desc = flow_vertex_map_[source_block];
        DataFlowVertexDescriptor flow_sink_desc = flow_vertex_map_[sink_block];

        // Get the sink vertex properties
        DataFlowVertex::Ptr sink_vertex = flow_graph_[flow_sink_desc];

        // Get an existing edge between these two blocks in the DFG
        DataFlowEdgeDescriptor flow_edge_desc;
        bool flow_edge_found;

        boost::tie(flow_edge_desc, flow_edge_found) = boost::edge(
            flow_source_desc,
            flow_sink_desc,
            flow_graph_);

        // Pointer to flow edge properties
        DataFlowEdge::Ptr flow_edge;

        // Only create edge if it isn't already there
        if(flow_edge_found) {
          RTT::log(RTT::Debug) << "Found DFG edge "
            << source_block->getName() << "." << source_port->getName() << " --> "
            << sink_block->getName() << "." << sink_port->getName() << RTT::endlog();

          // Get the existing DFG edge
          flow_edge = flow_graph_[flow_edge_desc];
        } else {
          // Create a new edge representing the connections between these two vertices
          flow_edge = boost::make_shared<DataFlowEdge>();
          flow_edge->connections.push_back(
              DataFlowEdge::Connection(source_port, sink_port));

          // Add the edge to the DFG
          flow_edge_desc = boost::add_edge(
              flow_source_desc, 
              flow_sink_desc, 
              flow_edge, 
              flow_graph_);

          // Set the topo flag since we've modified edges
          topology_modified = true;

          RTT::log(RTT::Debug) << "Created DFG edge "
            <<source_name<<"."<<source_port->getName()<<" --> "
            <<sink_name<<"."<<sink_port->getName()<< RTT::endlog();
        }

        // Check if this connection is already modeled in the data flow edge
        bool connection_exists = false;
        std::vector<DataFlowEdge::Connection>::const_iterator edge_connection_it;
        for(edge_connection_it = flow_edge->connections.begin();
            edge_connection_it != flow_edge->connections.end();
            ++edge_connection_it) 
        {
          if( edge_connection_it->source_port == source_port &&
              edge_connection_it->sink_port == sink_port) 
          {
            connection_exists = true;
            break;
          }
        }

        // Store the data flow connection in the edge if it doesn't already exist
        if(!connection_exists) {
          flow_edge->connections.push_back(DataFlowEdge::Connection(source_port, sink_port));
        }

        // Check if either of the blocks involved in this connection are latched
        if(source_vertex->latched_output || sink_vertex->lateched_input) {
          flow_edge->latched = true;
        }

        // Get the source and sink exec vertex descriptors
        DataFlowVertexDescriptor exec_source_desc = exec_vertex_map_[source_block];
        DataFlowVertexDescriptor exec_sink_desc = exec_vertex_map_[sink_block];

        // Get the edge in the exec graph
        DataFlowEdgeDescriptor exec_edge_desc;
        bool exec_edge_found;
        boost::tie(exec_edge_desc, exec_edge_found) = boost::edge(
            exec_source_desc,
            exec_sink_desc,
            exec_graph_);

        if(flow_edge->latched) {
          if(exec_edge_found) {
            // Remove the edge from the exec graph
            boost::remove_edge(exec_edge_desc, exec_graph_);
            topology_modified = true;
          }
        } else {
          if(!exec_edge_found) {
            // Add the edge to the exec graph
            exec_edge_desc = boost::add_edge(
                exec_source_desc,
                exec_sink_desc,
                flow_edge,
                exec_graph_);
            topology_modified = true;
          }
        }
      }
    }
  }

  // Recompute the execution schedule if the topology changed
  if(topology_modified) {
    this->computeSchedule(exec_graph_, exec_ordering_, true);
    RTT::log(RTT::Debug) << "Regenerated topological ordering." << RTT::endlog();
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////

bool Scheme::enableBlock(const std::string &block_name, const bool force)
{
  // First check if this block is a group
  std::map<std::string, std::set<std::string> >::iterator group = 
    block_groups_.find(block_name);

  if(group != block_groups_.end()) {
    // Enable the blocks in this group
    return this->enableBlocks(
        std::vector<std::string>(group->second.begin(),group->second.end()),
        true,
        force);
  }

  // Enable the block by name
  return this->enableBlock(this->getPeer(block_name), force);
}

bool Scheme::enableBlock(RTT::TaskContext *block, const bool force)
{
  using namespace conman::graph;

  RTT::Logger::In in("Scheme::enableBlock");

  if(block == NULL) { 
    return false; 
  }

  const std::string &block_name = block->getName();
  std::map<std::string,conman::graph::DataFlowVertex::Ptr>::const_iterator block_vertex_it = blocks_.find(block_name);

  if(block_vertex_it == blocks_.end()) {
    RTT::log(RTT::Error) << "Could not enable block \""<< block_name << "\""
      " because it has not been added to the scheme." << RTT::endlog();
    return false;
  }

  DataFlowVertex::Ptr block_vertex = block_vertex_it->second;

  // Make sure the block is configured
  if(!block->isConfigured()) {
    RTT::log(RTT::Error) << "Could not enable block \""<< block_name << "\""
      " because it has not been confiugre()ed." << RTT::endlog();
    return false;
  }

  // Check if the block is already enabled
  if(block->getTaskState() == RTT::TaskContext::Running) {
    // If it's already running, then we're going to assume for now that the
    // user isn't doing anything dirty.
    // TODO: Keep track of whether or not a block has been properly enabled.
    return true;
  }

  // Get the blocks that conflict with this block
  ConflictAdjacencyIterator conflict_it, conflict_end;

  boost::tie(conflict_it, conflict_end) =
    boost::adjacent_vertices(conflict_vertex_map_[block], conflict_graph_);

  // Check if conflicting blocks are running
  for(; conflict_it != conflict_end; ++conflict_it)
  {
    RTT::TaskContext *&conflict_block = conflict_graph_[*conflict_it]->block;

    // Check if the conflicting block is running
    if(conflict_block->getTaskState() == RTT::TaskContext::Running) {
      // If force is selected, disable the conflicting block
      if(force) {
        RTT::log(RTT::Info) << "Force-enabling block \""<< block_name << "\""
          " involves disabling block \"" << conflict_block->getName() << "\""
          << RTT::endlog();

        // Make sure we can actually disable it
        if(this->disableBlock(conflict_block) == false) {
          RTT::log(RTT::Error) << "Could not disable block \"" <<
            conflict_block->getName() << "\"" << RTT::endlog();
          return false;
        }
      } else {
        RTT::log(RTT::Error) << "Could not enable block \""<< block_name <<
          "\" because it conflicts with block \"" << conflict_block->getName()
          << "\"" << RTT::endlog();
        return false;
      }
    }
  }

  // Initialize the hook
  block_vertex->hook->init(last_update_time_);

  // Try to start the block
  if(!block->start()) {
    RTT::log(RTT::Error) << "Could not enable block \""<< block_name << "\""
      " because it could not be start()ed." << RTT::endlog();
    return false;
  }

  return true;
}

bool Scheme::disableBlock(const std::string &block_name)
{
  // First check if this block is a group
  std::map<std::string, std::set<std::string> >::iterator group = 
    block_groups_.find(block_name);

  if(group != block_groups_.end()) {
    // Enable the blocks in this group
    return this->disableBlocks(
        std::vector<std::string>(group->second.begin(),group->second.end()),
        true);
  }

  // Disable the block by name
  return this->disableBlock(this->getPeer(block_name));
}

bool Scheme::disableBlock(RTT::TaskContext* block) 
{
  if(block == NULL) { return false; }

  // Stop a block
  if(block->isRunning()) {
    if(!block->stop()) {
      RTT::log(RTT::Error) 
        << "Could not disable block \""<< block->getName() << "\" because it"
        " could not be stop()ed." << RTT::endlog();
      return false;
    }
  }

  return true;
}

bool Scheme::enableBlocks(
    const std::vector<std::string> &block_names,
    const bool strict,
    const bool force)
{
  using namespace conman::graph;

  // First make sure all the blocks can be enabled
  if(!force) {
    for(std::vector<std::string>::const_iterator it = block_names.begin();
        it != block_names.end();
        ++it)
    {
      // Get the blocks that conflict with this block
      ConflictAdjacencyIterator conflict_it, conflict_end;

      boost::tie(conflict_it, conflict_end) =
        boost::adjacent_vertices(conflict_vertex_map_[blocks_[*it]->block], conflict_graph_);

      // Check if conflicting blocks are running
      for(; conflict_it != conflict_end; ++conflict_it)
      {
        RTT::TaskContext *&conflict_block = conflict_graph_[*conflict_it]->block;

        // Check if the conflicting block is running
        if(conflict_block->getTaskState() == RTT::TaskContext::Running) {
          return false;
        }
      }
    }
  }

  // Enable the blocks
  bool success = true;

  for(std::vector<std::string>::const_iterator it = block_names.begin();
      it != block_names.end();
      ++it)
  {
    // Try to start the block
    success &= this->enableBlock(*it,force);

    // Break on failure if strict
    if(!success && strict) { return false; }
  }

  return success;
}

bool Scheme::disableBlocks(const bool strict)
{
  bool success = true;

  for(std::map<std::string,graph::DataFlowVertex::Ptr>::const_iterator it = blocks_.begin();
      it != blocks_.end();
      ++it)
  {
    // Try to disable the block
    success &= this->disableBlock(it->second->block);

    // Break on failure if strict
    if(!success && strict) { return false; }
  }

  return success;
}

bool Scheme::disableBlocks(
    const std::vector<std::string> &block_names,
    const bool strict)
{
  bool success = true;

  for(std::vector<std::string>::const_iterator it = block_names.begin();
      it != block_names.end();
      ++it)
  {
    // Try to disable the block
    success &= this->disableBlock(*it);

    // Break on failure if strict
    if(!success && strict) { return false; }
  }

  return success;
}

bool Scheme::switchBlocks(
    const std::vector<std::string> &disable_block_names,
    const std::vector<std::string> &enable_block_names,
    const bool strict,
    const bool force)
{
  // First disable blocks, so that "force" can be used appropriately when
  // enabling blocks. Also note that we used & instead of && in order to prevent
  // short-circuiting.
  return disableBlocks(disable_block_names, strict) & 
    enableBlocks(enable_block_names, strict, force);
}

bool Scheme::setEnabledBlocks(
    const std::vector<std::string> &enabled_block_names,
    const bool strict)
{
  return this->disableBlocks(strict) & 
    this->enableBlocks(enabled_block_names, strict, false);
}

///////////////////////////////////////////////////////////////////////////////

bool Scheme::configureHook()
{
  return true;
}

bool Scheme::startHook()
{
  return true;
}

void Scheme::updateHook() 
{
  using namespace conman::graph;

  RTT::Logger::In in("Scheme::updateHook");

  // What time is it
  RTT::os::TimeService::nsecs now = RTT::os::TimeService::Instance()->getNSecs();
  RTT::os::TimeService::Seconds 
    time = RTT::nsecs_to_Seconds(now),
    period = RTT::nsecs_to_Seconds(RTT::os::TimeService::Instance()->getNSecs(last_update_time_));
  
  // Store update time
  // NOTE: We maintain a single update time for all blocks so that any blocks
  // running at the same rate are executed in the same update() cycle
  last_update_time_ = now;

  // Execute the blocks in the appropriate order
  for(ExecutionOrdering::iterator block_it = exec_ordering_.begin();
      block_it != exec_ordering_.end();
      ++block_it) 
  {
    // Temporary variable for readability
    DataFlowVertex::Ptr block_vertex = flow_graph_[*block_it];

    // Get the state of the task
    const RTT::base::TaskCore::TaskState block_state = block_vertex->block->getTaskState();

    // Check if the task is running 
    if(block_state == RTT::TaskContext::Running) { 
      // Update the task
      if(!block_vertex->hook->update(time)) {
        // Signal an error
        this->error();
      }
    }
  }
}
