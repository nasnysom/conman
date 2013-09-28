/** Copyright (c) 2013, Jonathan Bohren, all rights reserved. 
 * This software is released under the BSD 3-clause license, for the details of
 * this license, please see LICENSE.txt at the root of this repository. 
 */

#include <string>
#include <vector>
#include <iterator>

#include <rtt/os/startstop.h>

#include <ocl/DeploymentComponent.hpp>
#include <ocl/TaskBrowser.hpp>
#include <ocl/LoggingService.hpp>
#include <rtt/Logger.hpp>
#include <rtt/deployment/ComponentLoader.hpp>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>

#include <conman/conman.h>
#include <conman/scheme.h>
#include <conman/hook.h>

#include <boost/assign/std/vector.hpp>
using namespace boost::assign;

#include "gtest/gtest.h"

class InvalidBlock : public RTT::TaskContext {
public:
  InvalidBlock(const std::string &name) : RTT::TaskContext(name) { }
};

class ValidBlock : public RTT::TaskContext {
public:
  ValidBlock(const std::string &name) : RTT::TaskContext(name) { 
    conman_hook_ = conman::Hook::GetHook(this);
  }
  boost::shared_ptr<conman::Hook> conman_hook_;
};

class SchemeTest : public ::testing::Test {
protected:
  SchemeTest() : scheme("Scheme") { }

  conman::Scheme scheme;
};

TEST_F(SchemeTest, Init) {
  std::vector<std::vector<std::string> > cycles;
  EXPECT_EQ(scheme.getFlowCycles(cycles),0);
  EXPECT_EQ(cycles.size(),0);

  EXPECT_EQ(scheme.getExecutionCycles(cycles),0);
  EXPECT_EQ(cycles.size(),0);

  std::vector<std::string> path;
  EXPECT_EQ(scheme.latchCount(path),0);

  EXPECT_EQ(scheme.maxLatchCount(),0);
  EXPECT_EQ(scheme.minLatchCount(),0);

  EXPECT_EQ(scheme.executable(),true);
}

class BlocksTest : public SchemeTest { };

TEST_F(BlocksTest, GetBlocks) {
  std::vector<std::string> blocks;
  
  blocks = scheme.getBlocks();
  EXPECT_EQ(blocks.size(),0);

  scheme.getBlocks(blocks);
  EXPECT_EQ(blocks.size(),0);
}

TEST_F(BlocksTest, AddBlocks) {
  EXPECT_FALSE(scheme.addBlock(""));
  EXPECT_FALSE(scheme.addBlock("fail"));
  EXPECT_FALSE(scheme.addBlock(NULL));

  InvalidBlock ib1("ib1");
  EXPECT_FALSE(scheme.addBlock(&ib1));
  EXPECT_EQ(scheme.getBlocks().size(),0);

  ValidBlock vb1("vb1");
  EXPECT_FALSE(scheme.addBlock("vb1"));
  EXPECT_TRUE(scheme.addPeer(&vb1));
  EXPECT_TRUE(scheme.addBlock("vb1"));
  EXPECT_EQ(scheme.getBlocks().size(),1);

  ValidBlock vb2("vb2");
  EXPECT_TRUE(scheme.addBlock(&vb2));

  EXPECT_EQ(scheme.getBlocks().size(),2);
}

TEST_F(BlocksTest, RemoveBlocks) {
  EXPECT_FALSE(scheme.removeBlock(""));
  EXPECT_FALSE(scheme.removeBlock("fail"));
  EXPECT_FALSE(scheme.removeBlock(NULL));

  ValidBlock vb1("vb1");
  EXPECT_TRUE(scheme.addPeer(&vb1));
  EXPECT_TRUE(scheme.removeBlock("vb1"));
  EXPECT_EQ(scheme.getBlocks().size(),0);
}

class GroupsTest : public SchemeTest { };

TEST_F(GroupsTest, GetGroups) {
  EXPECT_FALSE(scheme.hasGroup("fail"));

  std::vector<std::string> members;
  EXPECT_FALSE(scheme.getGroupMembers("fail",members));
}

TEST_F(GroupsTest, AddGroups) {
  EXPECT_TRUE(scheme.addGroup(""));
  EXPECT_TRUE(scheme.addGroup("win"));
  EXPECT_TRUE(scheme.addGroup("win"));
}

TEST_F(GroupsTest, SetGroups) {
  std::vector<std::string> members;

  EXPECT_TRUE(scheme.setGroup("",members));
  EXPECT_TRUE(scheme.setGroup("win",members));

  members.push_back("not_a_peer");
  EXPECT_FALSE(scheme.setGroup("fail",members));

}

TEST_F(GroupsTest, AddToGroups) {
  std::vector<std::string> members, members2;

  EXPECT_FALSE(scheme.addToGroup("fail",""));

  ValidBlock vb1("vb1");
  scheme.addBlock(&vb1);
  EXPECT_FALSE(scheme.addToGroup("win","vb1"));
  EXPECT_TRUE(scheme.addGroup("win"));
  EXPECT_TRUE(scheme.addToGroup("win","vb1"));

  EXPECT_TRUE(scheme.getGroupMembers("win",members2));
  EXPECT_EQ(members2.size(),1);
}

TEST_F(GroupsTest, NestedGroups) {
  std::vector<std::string> members, members_get;

  ValidBlock vb1("vb1");
  scheme.addBlock(&vb1);
  ValidBlock vb2("vb2");
  scheme.addBlock(&vb2);
  ValidBlock vb3("vb3");
  scheme.addBlock(&vb3);

  EXPECT_TRUE(scheme.setGroup("win1","vb1"));
  EXPECT_TRUE(scheme.setGroup("win2","vb2"));
  EXPECT_TRUE(scheme.setGroup("win3","vb3"));
  EXPECT_TRUE(scheme.addGroup("win4"));

  std::vector<std::string> win123_members;
  win123_members += "win1", "win2", "win3", "win4", "win123";

  EXPECT_TRUE(scheme.setGroup("win123",win123_members));

  EXPECT_TRUE(scheme.getGroupMembers("win123",members_get));
  EXPECT_EQ(members_get.size(),3);
}

TEST_F(GroupsTest, RemoveFromGroups) {
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  // Initialize Orocos
  __os_init(argc, argv);

  // Import conman plugin
  RTT::ComponentLoader::Instance()->import("conman", "" );

  return RUN_ALL_TESTS();
}
