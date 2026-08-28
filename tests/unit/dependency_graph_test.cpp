#include <gtest/gtest.h>

#include <nexus/dependency_graph.hpp>

TEST(DependencyGraphTest, StoresDependency) {
    nexus::DependencyGraph graph;

    graph.addDependency("kde", "wayland");

    const auto& dependencies = graph.dependencies("kde");

    ASSERT_EQ(dependencies.size(), 1);
    EXPECT_EQ(dependencies[0], "wayland");
}

TEST(DependencyGraphTest, DetectsNoCycleInValidGraph) {
    nexus::DependencyGraph graph;

    graph.addDependency("kde", "wayland");
    graph.addDependency("wayland", "drm");
    graph.addDependency("drm", "kernel");

    EXPECT_FALSE(graph.hasCycle());
}

TEST(DependencyGraphTest, DetectsCycle) {
    nexus::DependencyGraph graph;

    graph.addDependency("A", "B");
    graph.addDependency("B", "C");
    graph.addDependency("C", "A");

    EXPECT_TRUE(graph.hasCycle());
}

TEST(DependencyGraphTest, DetectsSelfDependency) {
    nexus::DependencyGraph graph;

    graph.addDependency("A", "A");

    EXPECT_TRUE(graph.hasCycle());
}
