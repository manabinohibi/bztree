// Copyright (c) Simon Fraser University
//
// Authors:
// Tianzheng Wang <tzwang@sfu.ca>
// Xiangpeng Hao <xiangpeng_hao@sfu.ca>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "bztree.h"

class LeafNodeFixtures : public ::testing::Test {
 public:
  void EmptyNode() {
    delete node;
    node = (bztree::LeafNode *) malloc(bztree::LeafNode::kNodeSize);
    memset(node, 0, bztree::LeafNode::kNodeSize);
    new(node) bztree::LeafNode;
  }

  // Dummy value:
  // sorted -> 0:10:100
  // unsorted -> 200:10:300
  void InsertDummy() {
    for (uint32_t i = 0; i < 100; i += 10) {
      auto str = std::to_string(i);
      node->Insert(0, str.c_str(), (uint16_t) str.length(), i, pool);
    }
    auto *new_node = node->Consolidate(pool);
    for (uint32_t i = 200; i < 300; i += 10) {
      auto str = std::to_string(i);
      new_node->Insert(0, str.c_str(), (uint16_t) str.length(), i, pool);
    }
    delete node;
    node = new_node;
  }

 protected:
  pmwcas::DescriptorPool *pool;
  bztree::LeafNode *node;
  void SetUp() override {
    pmwcas::InitLibrary(pmwcas::TlsAllocator::Create,
                        pmwcas::TlsAllocator::Destroy,
                        pmwcas::LinuxEnvironment::Create,
                        pmwcas::LinuxEnvironment::Destroy);
    pool = reinterpret_cast<pmwcas::DescriptorPool *>(
        pmwcas::Allocator::Get()->Allocate(sizeof(pmwcas::DescriptorPool)));
    new(pool) pmwcas::DescriptorPool(1000, 1, nullptr, false);

    node = (bztree::LeafNode *) malloc(bztree::LeafNode::kNodeSize);
    memset(node, 0, bztree::LeafNode::kNodeSize);
    new(node) bztree::LeafNode;
  }

  void TearDown() override {
    delete node;
  }
};
TEST_F(LeafNodeFixtures, Read) {
  pool->GetEpoch()->Protect();
  InsertDummy();
  ASSERT_EQ(node->Read("0", 1), 0);
  ASSERT_EQ(node->Read("10", 2), 10);
  ASSERT_EQ(node->Read("100", 3), 0);

  ASSERT_EQ(node->Read("200", 3), 200);
  ASSERT_EQ(node->Read("210", 3), 210);
  ASSERT_EQ(node->Read("280", 3), 280);

  pool->GetEpoch()->Unprotect();
}

TEST_F(LeafNodeFixtures, Insert) {
  pool->GetEpoch()->Protect();

  ASSERT_TRUE(node->Insert(0, "def", 3, 100, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "bdef", 4, 101, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "abc", 3, 102, pool).IsOk());
  ASSERT_EQ(node->Read("def", 3), 100);
  ASSERT_EQ(node->Read("abc", 3), 102);

  node->Dump();

  auto *new_node = node->Consolidate(pool);
  new_node->Dump();
  ASSERT_TRUE(new_node->Insert(0, "apple", 5, 106, pool).IsOk());
  ASSERT_EQ(new_node->Read("bdef", 4), 101);
  ASSERT_EQ(new_node->Read("apple", 5), 106);

  pool->GetEpoch()->Unprotect();
}

TEST_F(LeafNodeFixtures, DuplicateInsert) {
  pool->GetEpoch()->Protect();
  InsertDummy();
  ASSERT_TRUE(node->Insert(0, "10", 2, 111, pool).IsKeyExists());
  ASSERT_TRUE(node->Insert(0, "11", 2, 1212, pool).IsOk());

  ASSERT_EQ(node->Read("10", 2), 10);
  ASSERT_EQ(node->Read("11", 2), 1212);

  auto *new_node = node->Consolidate(pool);

  ASSERT_TRUE(new_node->Insert(0, "11", 2, 1213, pool).IsKeyExists());
  ASSERT_EQ(new_node->Read("11", 2), 1212);

  ASSERT_TRUE(new_node->Insert(0, "201", 3, 201, pool).IsOk());
  ASSERT_EQ(new_node->Read("201", 3), 201);

  pool->GetEpoch()->Unprotect();
}

TEST_F(LeafNodeFixtures, Delete) {
  pool->GetEpoch()->Protect();
  InsertDummy();
  ASSERT_EQ(node->Read("40", 2), 40);
  ASSERT_TRUE(node->Delete("40", 2, pool));
  ASSERT_EQ(node->Read("40", 2), 0);

  auto new_node = node->Consolidate(pool);

  ASSERT_EQ(new_node->Read("200", 3), 200);
  ASSERT_TRUE(new_node->Delete("200", 3, pool));
  ASSERT_EQ(new_node->Read("200", 3), 0);

  pool->GetEpoch()->Unprotect();
}

TEST_F(LeafNodeFixtures, SplitPrep) {
  pool->GetEpoch()->Protect();
  InsertDummy();

  ASSERT_TRUE(node->Insert(0, "abc", 3, 100, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "bdef", 4, 101, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "abcd", 4, 102, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "deadbeef", 8, 103, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "parker", 6, 104, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "deadpork", 8, 105, pool).IsOk());
  ASSERT_TRUE(node->Insert(0, "toronto", 7, 106, pool).IsOk());

  node->Dump();

  bztree::Stack stack;
  bztree::InternalNode *parent = nullptr;
  bztree::LeafNode *left = nullptr;
  bztree::LeafNode *right = nullptr;
  ASSERT_TRUE(node->PrepareForSplit(0, stack, &parent, &left, &right, pool));

  left->Dump();
  right->Dump();
  parent->Dump();
  pool->GetEpoch()->Unprotect();
}
TEST_F(LeafNodeFixtures, Update) {
  pool->GetEpoch()->Protect();
  InsertDummy();
  ASSERT_EQ(node->Read("10", 2), 10);
  ASSERT_TRUE(node->Update(0, "10", 2, 11, pool));
  ASSERT_EQ(node->Read("10", 2), 11);

  ASSERT_EQ(node->Read("200", 3), 200);
  ASSERT_TRUE(node->Update(0, "200", 3, 201, pool));
  ASSERT_EQ(node->Read("200", 3), 201);
  pool->GetEpoch()->Unprotect();
}

TEST_F(LeafNodeFixtures, Upsert) {
  pool->GetEpoch()->Protect();
  InsertDummy();
  ASSERT_EQ(node->Read("20", 2), 20);
  ASSERT_TRUE(node->Upsert(0, "20", 2, 21, pool));
  ASSERT_EQ(node->Read("20", 2), 21);

  ASSERT_EQ(node->Read("210", 3), 210);
  ASSERT_TRUE(node->Upsert(0, "210", 3, 211, pool));
  ASSERT_EQ(node->Read("210", 3), 211);

//  No-exsiting upsert
  ASSERT_EQ(node->Read("21", 2), 0);
  ASSERT_TRUE(node->Upsert(0, "21", 2, 21, pool));
  ASSERT_EQ(node->Read("21", 2), 21);

  ASSERT_EQ(node->Read("211", 3), 0);
  ASSERT_TRUE(node->Upsert(0, "211", 3, 211, pool));
  ASSERT_EQ(node->Read("211", 3), 211);

  pool->GetEpoch()->Unprotect();
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
