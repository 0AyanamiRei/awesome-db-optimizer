#pragma once

#include "volcano/types.hpp"
#include "volcano/stats.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace volcano::test {

// A self-contained test case: join graph + stats catalog.
// Constructed entirely in C++ code — no SQL or JSON input needed.
struct TestCase {
  std::string name;
  std::string description;
  JoinGraph graph;
  StatsCatalog stats;

  // List of all registered test cases.
  static const TestCase &Lookup(const std::string &name);
  static std::vector<std::string> AllNames();
};

// --- Factory functions for common join graph topologies ---

// Chain: a-b-c (3 tables, 2 join edges)
//   a.rows=1000, b.rows=100, c.rows=500
//   a.x=b.x (sel=0.01), b.y=c.y (sel=0.05)
TestCase MakeChain3();

// Chain: a-b-c-d (4 tables, 3 join edges)
//   a.rows=1000, b.rows=100, c.rows=500, d.rows=200
//   a.x=b.x (sel=0.01), b.y=c.y (sel=0.05), c.z=d.z (sel=0.02)
TestCase MakeChain4();

// Star: center + 3 leaves (4 tables, 3 join edges)
//   center.rows=10000, a.rows=100, b.rows=200, c.rows=300
//   center.x=a.x (sel=0.001), center.y=b.y (sel=0.002), center.z=c.z (sel=0.003)
TestCase MakeStar4();

// Cycle: a-b-c-d-a (4 tables, 4 join edges, cyclic)
//   All rows=100, all selectivities=0.1
TestCase MakeCycle4();

// Clique: fully connected 4 tables (6 join edges)
//   All rows=100, all selectivities=0.1
TestCase MakeClique4();

// Two tables (simplest non-trivial case)
TestCase MakeTwoTable();

} // namespace volcano::test
