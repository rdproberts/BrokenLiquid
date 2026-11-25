#include "core/UndoHistory.h"

#include <gtest/gtest.h>

using bl::UndoHistory;

TEST(UndoHistory, UndoRedoReturnTheRightStates) {
    UndoHistory<int> h;
    EXPECT_FALSE(h.canUndo());
    h.push("one", 0, 1);
    h.push("two", 1, 2);
    EXPECT_EQ(h.undoLabel(), "two");
    EXPECT_EQ(h.undo(), 1);
    EXPECT_EQ(h.redoLabel(), "two");
    EXPECT_EQ(h.undo(), 0);
    EXPECT_FALSE(h.canUndo());
    EXPECT_EQ(h.redo(), 1);
    EXPECT_EQ(h.redo(), 2);
    EXPECT_FALSE(h.canRedo());
}

TEST(UndoHistory, NewChangeDropsRedo) {
    UndoHistory<int> h;
    h.push("a", 0, 1);
    h.push("b", 1, 2);
    h.undo();
    h.push("c", 1, 5);
    EXPECT_FALSE(h.canRedo());
    EXPECT_EQ(h.undoLabel(), "c");
}

TEST(UndoHistory, TracksCleanState) {
    UndoHistory<int> h;
    EXPECT_TRUE(h.isClean());
    h.push("a", 0, 1);
    EXPECT_FALSE(h.isClean());
    h.setClean(); // saved after "a"
    EXPECT_TRUE(h.isClean());
    h.push("b", 1, 2);
    EXPECT_FALSE(h.isClean());
    h.undo();
    EXPECT_TRUE(h.isClean()); // back at the saved state
    h.undo();
    EXPECT_FALSE(h.isClean());
    h.redo();
    EXPECT_TRUE(h.isClean());
}

TEST(UndoHistory, CleanStateOnDiscardedBranchIsUnreachable) {
    UndoHistory<int> h;
    h.push("a", 0, 1);
    h.push("b", 1, 2);
    h.setClean(); // saved at 2
    h.undo();
    h.push("c", 1, 3); // the saved state can no longer be reached
    EXPECT_FALSE(h.isClean());
    h.undo();
    EXPECT_FALSE(h.isClean());
}

TEST(UndoHistory, RespectsLimit) {
    UndoHistory<int> h(3);
    for (int i = 0; i < 10; ++i) {
        h.push("step", i, i + 1);
    }
    EXPECT_EQ(h.undoCount(), 3u);
    EXPECT_EQ(h.undo(), 9);
    EXPECT_EQ(h.undo(), 8);
    EXPECT_EQ(h.undo(), 7);
    EXPECT_FALSE(h.canUndo());
}
