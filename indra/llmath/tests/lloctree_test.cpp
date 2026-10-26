/**
 * @file lloctree_test.cpp
 * @author Rye
 * @brief The octree's near-first traversal
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

// The viewer's cull walks its octrees with traverseNearFirst, and the groups reach the draw pools in the order it
// visits them. What it promises is the order a ray from the eye would meet the children in, whatever order the
// children were added in, at every level of the tree.

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include "../lloctree.h"

#include <algorithm>
#include <random>
#include <vector>

namespace
{
    struct Entry
    {
        LLVector4a mPosition;
        S32 mBinIndex = -1;
        const LLVector4a& getPositionGroup() const { return mPosition; }
        F32 getBinRadius() const { return 0.5f; }
        S32 getBinIndex() const { return mBinIndex; }
        void setBinIndex(S32 index) { mBinIndex = index; }
    };

    using Node = LLOctreeNode<Entry, Entry*>;

    class NearFirst : public LLOctreeTraveler<Entry, Entry*>
    {
    public:
        explicit NearFirst(const LLVector4a& eye) : mEye(eye) { }

        void traverse(const Node* node) override { traverseNearFirst(node, mEye); }
        void visit(const Node* node) override { mVisited.push_back(node); }

        // Where in the walk `node` was visited, or -1.
        S32 indexOf(const Node* node) const
        {
            auto it = std::find(mVisited.begin(), mVisited.end(), node);
            return it == mVisited.end() ? -1 : (S32)(it - mVisited.begin());
        }

        LLVector4a mEye;
        std::vector<const Node*> mVisited;
    };

    LLVector4a vec(F32 x, F32 y, F32 z)
    {
        return LLVector4a(x, y, z, 0.f);
    }

    // The child of `parent` in `octant`: x is bit 0, y bit 1, z bit 2, set when the child is on the high side.
    Node* make_child(Node* parent, U8 octant)
    {
        LLVector4a size = parent->getSize();
        size.mul(0.5f);
        F32 c[3];
        for (U32 axis = 0; axis < 3; ++axis)
        {
            c[axis] = parent->getCenter()[axis] + ((octant & (1 << axis)) ? size[axis] : -size[axis]);
        }
        Node* child = new Node(vec(c[0], c[1], c[2]), size, parent);
        parent->addChild(child, true);
        return child;
    }

    // A root 16 m across at the origin with all eight children, added out of order, and all eight grandchildren under
    // the child in octant 6.
    struct Tree
    {
        Tree()
        {
            mRoot = new Node(vec(0.f, 0.f, 0.f), vec(8.f, 8.f, 8.f), nullptr);
            for (U8 octant : { 5, 2, 7, 0, 3, 6, 1, 4 })
            {
                mChildren[octant] = make_child(mRoot, octant);
            }
            for (U8 octant : { 7, 1, 4, 2, 0, 6, 3, 5 })
            {
                mGrandchildren[octant] = make_child(mChildren[6], octant);
            }
        }
        ~Tree() { delete mRoot; }

        Node* mRoot;
        Node* mChildren[8];
        Node* mGrandchildren[8];
    };

    // The children of `parent` a segment from the eye to `target` passes through, in the order it meets them.
    std::vector<const Node*> met_along(const Node* parent, const Node* const* children, const LLVector4a& eye, const LLVector4a& target)
    {
        std::vector<const Node*> met;
        constexpr int STEPS = 2000;
        for (int i = 0; i <= STEPS; ++i)
        {
            LLVector4a p;
            p.setLerp(eye, target, (F32)i / STEPS);
            if (!parent->isInside(p))
            {
                continue;
            }
            const Node* child = children[parent->getOctant(p)];
            if (met.empty() || met.back() != child)
            {
                met.push_back(child);
            }
        }
        return met;
    }

    TEST_CASE("LLOctreeTraveler: traverseNearFirst visits every node once, the eye's own child first", "[LLOctree]")
    {
        Tree tree;
        for (U8 eye_octant = 0; eye_octant < 8; ++eye_octant)
        {
            // inside the eye's child, off its centre
            LLVector4a eye = tree.mChildren[eye_octant]->getCenter();
            eye.add(vec(0.7f, -1.3f, 0.4f));

            NearFirst walk(eye);
            walk.traverse(tree.mRoot);

            REQUIRE(walk.mVisited.size() == 17);
            CHECK(walk.mVisited[0] == tree.mRoot);
            CHECK(walk.mVisited[1] == tree.mChildren[eye_octant]);

            std::vector<const Node*> sorted = walk.mVisited;
            std::sort(sorted.begin(), sorted.end());
            CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
        }
    }

    TEST_CASE("LLOctreeTraveler: no child is visited before one a ray from the eye meets first", "[LLOctree]")
    {
        Tree tree;
        std::mt19937 rng(7);
        std::uniform_real_distribution<F32> anywhere(-40.f, 40.f);
        std::uniform_real_distribution<F32> unit(0.f, 1.f);

        int crossings = 0;
        for (int trial = 0; trial < 300; ++trial)
        {
            // half the eyes inside the root, half anywhere around it
            const F32 scale = (trial & 1) ? 1.f : 0.19f;
            const LLVector4a eye = vec(anywhere(rng) * scale, anywhere(rng) * scale, anywhere(rng) * scale);

            NearFirst walk(eye);
            walk.traverse(tree.mRoot);

            for (U8 octant = 0; octant < 8; ++octant)
            {
                const Node* child = tree.mChildren[octant];
                LLVector4a target = child->getCenter();
                target.add(vec((unit(rng) - 0.5f) * 7.f, (unit(rng) - 0.5f) * 7.f, (unit(rng) - 0.5f) * 7.f));

                const std::vector<const Node*> met = met_along(tree.mRoot, tree.mChildren, eye, target);
                for (size_t i = 1; i < met.size(); ++i)
                {
                    ++crossings;
                    CHECK(walk.indexOf(met[i - 1]) < walk.indexOf(met[i]));
                }
            }
        }
        // the trials must actually have crossed from child to child
        CHECK(crossings > 500);
    }

    TEST_CASE("LLOctreeTraveler: a node's children are ordered by the eye at every level", "[LLOctree]")
    {
        Tree tree;
        std::mt19937 rng(11);
        std::uniform_real_distribution<F32> around(-12.f, 12.f);
        std::uniform_real_distribution<F32> unit(0.f, 1.f);

        const Node* parent = tree.mChildren[6];
        int crossings = 0;
        for (int trial = 0; trial < 200; ++trial)
        {
            const LLVector4a eye = vec(around(rng), around(rng), around(rng));

            NearFirst walk(eye);
            walk.traverse(tree.mRoot);

            for (U8 octant = 0; octant < 8; ++octant)
            {
                LLVector4a target = tree.mGrandchildren[octant]->getCenter();
                target.add(vec((unit(rng) - 0.5f) * 3.5f, (unit(rng) - 0.5f) * 3.5f, (unit(rng) - 0.5f) * 3.5f));

                const std::vector<const Node*> met = met_along(parent, tree.mGrandchildren, eye, target);
                for (size_t i = 1; i < met.size(); ++i)
                {
                    ++crossings;
                    CHECK(walk.indexOf(met[i - 1]) < walk.indexOf(met[i]));
                }
            }
        }
        CHECK(crossings > 300);
    }

    TEST_CASE("LLOctreeTraveler: traverseNearFirst walks a lone child, wherever the eye is", "[LLOctree]")
    {
        Node* root = new Node(vec(0.f, 0.f, 0.f), vec(8.f, 8.f, 8.f), nullptr);
        const Node* child = make_child(root, 5);

        NearFirst walk(vec(-3.f, -3.f, -3.f));
        walk.traverse(root);

        REQUIRE(walk.mVisited.size() == 2);
        CHECK(walk.indexOf(root) == 0);
        CHECK(walk.indexOf(child) == 1);

        delete root;
    }

    // An element of a size, as the viewer's static prims are: the octree files it by its bin radius.
    struct SizedEntry
    {
        LLVector4a mPosition;
        F32 mRadius = 1.f;
        S32 mBinIndex = -1;
        const LLVector4a& getPositionGroup() const { return mPosition; }
        F32 getBinRadius() const { return mRadius; }
        S32 getBinIndex() const { return mBinIndex; }
        void setBinIndex(S32 index) { mBinIndex = index; }
    };

    using SizedNode = LLOctreeNode<SizedEntry, SizedEntry*>;
    using SizedRoot = LLOctreeRoot<SizedEntry, SizedEntry*>;

    // The octree's capacity and minimum size are the process's: each case sets its own and puts them back.
    struct OctreeLimits
    {
        OctreeLimits(U32 capacity, F32 min_size) : mCapacity(gOctreeMaxCapacity), mMinSize(gOctreeMinSize)
        {
            gOctreeMaxCapacity = capacity;
            gOctreeMinSize     = min_size;
        }
        ~OctreeLimits()
        {
            gOctreeMaxCapacity = mCapacity;
            gOctreeMinSize     = mMinSize;
        }
        U32 mCapacity;
        F32 mMinSize;
    };

    // Every node under `node`, and it.
    void collect_nodes(SizedNode* node, std::vector<SizedNode*>& nodes)
    {
        nodes.push_back(node);
        for (U32 i = 0; i < node->getChildCount(); ++i)
        {
            collect_nodes(node->getChild(i), nodes);
        }
    }

    // Whether `node` can still split: a child of one under twice the minimum size would sit at its own centre, so that
    // insert keeps whatever comes there (LLOctreeNode::insert).
    bool can_split(SizedNode* node)
    {
        return node->getSize()[0] >= 2.f * gOctreeMinSize;
    }

    // Many elements of one size crowded into a space smaller than they are, as small prims are filed by a bin radius
    // of 3 m (LLVOVolume::getBinRadius) and a dense build packs hundreds of them into a few metres: once the node of
    // their size is full, each node below it takes no more than the capacity either, and gives the rest to its
    // children in turn.
    TEST_CASE("LLOctreeNode: no node that can still split holds more than the capacity", "[LLOctree]")
    {
        OctreeLimits limits(8, 0.01f);
        constexpr U32 COUNT = 2000;
        std::vector<SizedEntry> entries(COUNT);
        std::mt19937 rng(7);
        std::uniform_real_distribution<F32> place(0.1f, 3.9f);
        for (SizedEntry& entry : entries)
        {
            entry.mPosition = vec(place(rng), place(rng), place(rng));
            entry.mRadius   = 3.f;
        }

        SizedRoot* root = new SizedRoot(vec(0.f, 0.f, 0.f), vec(1.f, 1.f, 1.f), nullptr);
        for (SizedEntry& entry : entries)
        {
            root->insert(&entry);
        }

        std::vector<SizedNode*> nodes;
        collect_nodes(root, nodes);
        U32 held = 0;
        for (SizedNode* node : nodes)
        {
            held += node->getElementCount();
            if (can_split(node))
            {
                CAPTURE(node->getSize()[0]);
                CHECK(node->getElementCount() <= gOctreeMaxCapacity);
            }
        }
        {
            INFO("every element is in the tree, once");
            CHECK(held == COUNT);
        }
        for (const SizedEntry& entry : entries)
        {
            CHECK(entry.getBinIndex() >= 0);
        }
        delete root;
    }

    // Crowded into a few centimetres, past what the nodes can split to: those that can still split take no more than
    // the capacity, and those under twice the minimum size, which cannot, take the rest, so that the descent ends.
    TEST_CASE("LLOctreeNode: a node too small to split takes what comes and every larger one the capacity", "[LLOctree]")
    {
        OctreeLimits limits(8, 0.01f);
        constexpr U32 COUNT = 2000;
        std::vector<SizedEntry> entries(COUNT);
        std::mt19937 rng(13);
        std::uniform_real_distribution<F32> place(0.f, 0.04f);
        for (SizedEntry& entry : entries)
        {
            entry.mPosition = vec(place(rng), place(rng), place(rng));
            entry.mRadius   = 3.f;
        }

        SizedRoot* root = new SizedRoot(vec(0.f, 0.f, 0.f), vec(1.f, 1.f, 1.f), nullptr);
        for (SizedEntry& entry : entries)
        {
            root->insert(&entry);
        }

        std::vector<SizedNode*> nodes;
        collect_nodes(root, nodes);
        U32 held = 0;
        U32 crowded = 0;
        for (SizedNode* node : nodes)
        {
            held += node->getElementCount();
            if (can_split(node))
            {
                CAPTURE(node->getSize()[0]);
                CHECK(node->getElementCount() <= gOctreeMaxCapacity);
            }
            else if (node->getElementCount() > gOctreeMaxCapacity)
            {
                ++crowded;
            }
        }
        {
            INFO("every element is in the tree, once");
            CHECK(held == COUNT);
        }
        {
            INFO("the crowd reached the nodes that cannot split, and they took it");
            CHECK(crowded > 0);
        }
        delete root;
    }

    // Taken out through the nodes that hold them, as the viewer takes a drawable out of its group, the elements leave
    // nothing behind: every node they made goes as it empties, back to the root.
    TEST_CASE("LLOctreeNode: removing every element leaves an empty root", "[LLOctree]")
    {
        OctreeLimits limits(8, 0.01f);
        constexpr U32 COUNT = 500;
        std::vector<SizedEntry> entries(COUNT);
        std::mt19937 rng(11);
        std::uniform_real_distribution<F32> place(-20.f, 20.f);
        std::uniform_real_distribution<F32> size(0.5f, 6.f);
        for (U32 i = 0; i < COUNT; ++i)
        {
            // Half crowded into a few metres, half spread out.
            const F32 spread = i % 2 ? 1.f : 0.1f;
            entries[i].mPosition = vec(place(rng) * spread, place(rng) * spread, place(rng) * spread);
            entries[i].mRadius   = i % 2 ? size(rng) : 3.f;
        }

        SizedRoot* root = new SizedRoot(vec(0.f, 0.f, 0.f), vec(1.f, 1.f, 1.f), nullptr);
        for (SizedEntry& entry : entries)
        {
            root->insert(&entry);
        }

        std::vector<std::pair<SizedEntry*, SizedNode*>> placed;
        std::vector<SizedNode*> nodes;
        collect_nodes(root, nodes);
        for (SizedNode* node : nodes)
        {
            for (auto it = node->getDataBegin(); it != node->getDataEnd(); ++it)
            {
                placed.emplace_back(*it, node);
            }
        }
        REQUIRE(placed.size() == COUNT);

        for (auto& [entry, node] : placed)
        {
            CHECK(node->remove(entry));
            CHECK(entry->getBinIndex() == -1);
        }
        CHECK(root->getElementCount() == 0);
        CHECK(root->getChildCount() == 0);
        delete root;
    }
}
