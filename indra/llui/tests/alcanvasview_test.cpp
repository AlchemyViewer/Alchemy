/**
 * @file alcanvasview_test.cpp
 * @brief How big a surface is, where what is on it sits, and when it scrolls.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#include "linden_common.h"

#include "alcanvasview.h"

#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../llscrollbar.h"
#include "../llscrollcontainer.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    // Something on a surface that says where the pointer and a drop reached
    // it, in its own coordinates.
    struct Target : public LLView
    {
        std::vector<std::pair<S32, S32>> clicks;
        std::vector<std::pair<S32, S32>> drops;

        explicit Target(const LLView::Params& p) : LLView(p) {}

        bool handleMouseDown(S32 x, S32 y, MASK) override
        {
            clicks.emplace_back(x, y);
            return true;
        }
        bool handleDragAndDrop(S32 x, S32 y, MASK, bool, EDragAndDropType, void*, EAcceptance* accept, std::string&) override
        {
            drops.emplace_back(x, y);
            *accept = ACCEPT_YES_SINGLE;
            return true;
        }
    };

    Target* target(LLView* on, const LLRect& rect)
    {
        LLView::Params p;
        p.name         = "target";
        p.rect         = rect;
        p.mouse_opaque = true;
        p.visible      = true;
        Target* made   = new Target(p);
        on->addChild(made);
        return made;
    }
}

namespace
{
    struct alcanvasview_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        // The room a surface is shown in, in these tests.
        static constexpr S32 ROOM_W = 700;
        static constexpr S32 ROOM_H = 400;

        static ALCanvasView* surface(bool sizable = true)
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "canvas";
            p.rect = LLRect(0, ROOM_H, ROOM_W, 0);
            p.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
            p.background_visible = false;
            ALCanvasView* canvas = new ALCanvasView(p);
            canvas->initFromParams(p);
            canvas->setSizable(sizable);
            return canvas;
        }

        // Something built on a surface, placed the way a builder places one:
        // at a margin from the surface's top left corner.
        static LLPanel* place(ALCanvasView* canvas, S32 width, S32 height, S32 margin = 4)
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "root";
            p.rect = LLRect(0, height, width, 0);
            p.follows.flags = FOLLOWS_ALL;
            LLPanel* root = LLUICtrlFactory::create<LLPanel>(p);
            canvas->addChild(root);
            canvas->fitContent(width + 2 * margin, height + 2 * margin);
            root->setOrigin(margin, canvas->surfaceHeight() - height - margin);
            canvas->setRoot(root);
            return root;
        }

        static std::string where(const LLRect& r)
        {
            return std::to_string(r.mLeft) + "," + std::to_string(r.mBottom)
                 + " to " + std::to_string(r.mRight) + "," + std::to_string(r.mTop);
        }

        static std::string size(const LLView* v)
        {
            return std::to_string(v->getRect().getWidth()) + " x "
                 + std::to_string(v->getRect().getHeight());
        }
    };

    // A surface is never smaller than the room it is shown in: one that
    // stopped at the edge of what is on it has nowhere to draw a rule along
    // and nothing to drop onto.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a surface is never smaller than the room it is shown in", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 200, 100);

        CHECK(canvas->getRect().getWidth() == ROOM_W);
        CHECK(canvas->getRect().getHeight() == ROOM_H);
        canvas->die();
    }

    // And never smaller than what is on it, which is the whole of why a
    // canvas scrolls.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a surface is never smaller than what is on it", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 900, 800);

        UNSCOPED_INFO("wider than the room " + size(canvas));
        CHECK(canvas->getRect().getWidth() > ROOM_W);
        UNSCOPED_INFO("and taller " + size(canvas));
        CHECK(canvas->getRect().getHeight() > ROOM_H);
        canvas->die();
    }

    // A surface has two sizes. What is on it keeps its own numbers; what it
    // covers is those times the zoom, and that is what has to be scrolled.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a zoomed surface covers its own size times the zoom", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 400, 300);

        canvas->setZoom(2.f);
        UNSCOPED_INFO("the surface is still measured in the file's numbers");
        CHECK(canvas->surfaceWidth() == 408);
        UNSCOPED_INFO("and it covers twice that");
        CHECK(canvas->getRect().getWidth() == 816);
        UNSCOPED_INFO("which is more than the room " + size(canvas));
        CHECK(canvas->getRect().getWidth() > ROOM_W);

        // Drawn smaller, the surface grows in its own coordinates instead,
        // so the region is still filled and there is still nothing to scroll.
        canvas->setZoom(0.5f);
        UNSCOPED_INFO("half size covers the room exactly");
        CHECK(canvas->getRect().getWidth() == ROOM_W);
        UNSCOPED_INFO("and no more of it");
        CHECK(canvas->getRect().getHeight() == ROOM_H);
        UNSCOPED_INFO("so the surface itself is twice the room");
        CHECK(canvas->surfaceWidth() >= ROOM_W * 2);
        canvas->die();
    }

    // The pointer is taken out of the zoom at one door, so everything that
    // finds or moves anything works in the file's own numbers.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: toContent takes the zoom out of a pointer position", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setZoom(2.f);
        S32 x = 100;
        S32 y = 50;
        canvas->toContent(x, y);
        CHECK(x == 50);
        CHECK(y == 25);
        canvas->die();
    }

    // The surface changes size; what is on it does not. Carrying it by its
    // follows flags would stretch a previewed window to the size of the
    // thing previewing it.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: what is on a surface keeps its size when the surface grows", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);
        UNSCOPED_INFO("it was built at its own size");
        CHECK(size(root) == std::string("200 x 100"));

        canvas->setLeastSurface(ROOM_W, ROOM_H);
        UNSCOPED_INFO("the surface grew " + size(canvas));
        CHECK(canvas->getRect().getWidth() >= ROOM_W);
        UNSCOPED_INFO("what is on it did not");
        CHECK(size(root) == std::string("200 x 100"));
        canvas->die();
    }

    // What is on it keeps its distance from the surface's top left corner
    // when the surface changes size, because a view otherwise keeps its
    // distance from the bottom.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: what is on a surface keeps its distance from the top left corner", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100, 4);
        UNSCOPED_INFO("placed at the margin");
        CHECK(root->getRect().mLeft == 4);
        UNSCOPED_INFO("under the top");
        CHECK(canvas->surfaceHeight() - root->getRect().mTop == 4);

        canvas->setLeastSurface(ROOM_W, ROOM_H);
        UNSCOPED_INFO("still at the margin");
        CHECK(root->getRect().mLeft == 4);
        UNSCOPED_INFO("still under the top");
        CHECK(canvas->surfaceHeight() - root->getRect().mTop == 4);
        canvas->die();
    }

    // A child placed past its parent's edge is drawn past it, and a surface
    // cut to the rect would leave that clipped with nowhere to scroll to.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: the surface grows to reach a child placed past its parent's edge", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);
        const S32 was = canvas->surfaceWidth();

        LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
        p.name = "spills";
        p.rect = LLRect(150, 60, 500, 10);       // out past the root's right edge
        root->addChild(LLUICtrlFactory::create<LLPanel>(p));
        canvas->rememberRoot();

        UNSCOPED_INFO("the surface reaches what is drawn " + size(canvas));
        CHECK(canvas->surfaceWidth() > was);
        UNSCOPED_INFO("as far as it goes");
        CHECK(canvas->surfaceWidth() >= 4 + 500);
        canvas->die();
    }

    // A preview moved towards an edge takes the surface with it: where it
    // sits is remembered nowhere else, so the surface has to grow to keep it.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a preview moved towards an edge takes the surface with it", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);

        root->translate(400, -300);
        canvas->rememberRoot();

        UNSCOPED_INFO("the surface followed it across " + size(canvas));
        CHECK(canvas->surfaceWidth() >= 604);
        UNSCOPED_INFO("and down " + size(canvas));
        CHECK(canvas->surfaceHeight() >= 404);
        UNSCOPED_INFO("and it stayed where it was put");
        CHECK(root->getRect().mLeft == 404);
        canvas->die();
    }

    // There is no growing off the top left of a canvas, so what is dragged
    // that way stops at the corner rather than disappearing over it.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: what is moved off the top left stops at the corner", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        LLPanel* root = place(canvas, 200, 100);

        root->translate(-80, 200);
        canvas->rememberRoot();

        UNSCOPED_INFO("it did not go off the left " + where(root->getRect()));
        CHECK(root->getRect().mLeft >= 0);
        UNSCOPED_INFO("nor off the top " + where(root->getRect()));
        CHECK(root->getRect().mTop <= canvas->surfaceHeight());
        canvas->die();
    }

    // A surface that is not sizable is whatever it was given: a canvas in a
    // window of its own is sized by the window.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a surface that is not sizable keeps the rect it was given", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface(false);
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 900, 800);
        UNSCOPED_INFO("it kept the rect it was made with");
        CHECK(canvas->getRect().getWidth() == ROOM_W);
        canvas->die();
    }

    // Two variants side by side are one row as wide as both of them, and
    // each gets half the room to fill.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: two variants in a row sit side by side and each fill half the room", "[alcanvasview]")
    {
        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row";
        rp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        ALCanvasRow* row = new ALCanvasRow(rp);
        row->initFromParams(rp);

        ALCanvasView* first = surface();
        ALCanvasView* second = surface();
        row->addCanvas(first);
        row->addCanvas(second);
        row->show(first, true);
        row->show(second, true);
        place(first, 100, 60);
        place(second, 100, 60);
        row->layout();

        const S32 half = (ROOM_W - ALCanvasRow::GUTTER) / 2;
        UNSCOPED_INFO("each fills its share of the room");
        CHECK(first->getRect().getWidth() == half);
        CHECK(second->getRect().getWidth() == half);
        UNSCOPED_INFO("and the row is the two of them and the gutter");
        CHECK(row->getRect().getWidth() == half * 2 + ALCanvasRow::GUTTER);
        UNSCOPED_INFO("the second is to the right of the first");
        CHECK(second->getRect().mLeft == half + ALCanvasRow::GUTTER);
        UNSCOPED_INFO("and their tops line up");
        CHECK(first->getRect().mTop == second->getRect().mTop);
        row->die();
    }

    // And the whole of it: a container, a row, a surface, and something on
    // it too big for the region. This is the question the canvas exists to
    // answer -- what does not fit is scrolled to rather than cut off.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a preview too big for its region in a row scrolls rather than clips", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row";
        rp.rect = area->getLocalRect();
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        ALCanvasRow* row = new ALCanvasRow(rp);
        row->initFromParams(rp);
        area->addChild(row);

        ALCanvasView* canvas = surface();
        row->addCanvas(canvas);
        row->show(canvas, true);

        LLScrollbar* down = area->getChild<LLScrollbar>("scrollable vertical", true);
        LLScrollbar* across = area->getChild<LLScrollbar>("scrollable horizontal", true);

        // Something that fits: nothing to scroll to, and the row still
        // covers the region, so the whole of it belongs to the canvas.
        place(canvas, 200, 100);
        row->shareRoom();
        row->layout();
        {
            INFO("what fits needs no scrollbar");
            CHECK_FALSE(down->getVisible());
            CHECK_FALSE(across->getVisible());
        }
        UNSCOPED_INFO("the row covers the region across");
        CHECK(row->getRect().getWidth() == ROOM_W);
        UNSCOPED_INFO("and down");
        CHECK(row->getRect().getHeight() == ROOM_H);

        // Something taller than the region.
        canvas->clear();
        place(canvas, 200, ROOM_H * 2);
        row->layout();
        UNSCOPED_INFO("a preview taller than the region scrolls");
        CHECK(down->getVisible());

        // And the same by zooming rather than by building something bigger:
        // a preview drawn past the edge of the region is clipped, and being
        // clipped is what a scrollbar is for.
        canvas->clear();
        place(canvas, 200, 100);
        row->layout();
        UNSCOPED_INFO("small again, and quiet");
        CHECK_FALSE(down->getVisible());
        canvas->setZoom(4.f);
        row->layout();
        UNSCOPED_INFO("zoomed past the region, it scrolls");
        CHECK((down->getVisible() || across->getVisible()));
        area->die();
    }

    // A region with nothing built on it yet is still the canvas's: the row
    // covers it, so a wheel or a drop there reaches a canvas rather than
    // falling through to whatever is behind.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a row with nothing built on it yet still covers the region", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row";
        rp.rect = LLRect(0, 1, 1, 0);
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        ALCanvasRow* row = new ALCanvasRow(rp);
        row->initFromParams(rp);
        area->addChild(row);

        ALCanvasView* canvas = surface();
        row->addCanvas(canvas);
        row->show(canvas, false);
        row->layout();

        const LLRect region = area->getContentWindowRect();
        UNSCOPED_INFO("the row covers the region across");
        CHECK(row->getRect().getWidth() == region.getWidth());
        UNSCOPED_INFO("and down");
        CHECK(row->getRect().getHeight() == region.getHeight());
        area->die();
    }

    // Moved twice, which is what anybody moving anything does. The second
    // move is measured against the surface the first one grew, so a surface
    // that forgets what it needs before reading where the root is puts the
    // root back higher than it was and grows under it again -- a preview
    // that jumps on every drag and walks off the end of what can be
    // scrolled to.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a root moved again and again stays where each move put it", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);

        for (S32 again = 0; again < 3; ++again)
        {
            root->translate(60, -50);
            // Where it was put, from the corner the surface grows out of.
            // Its own y is not that: a surface that grows does so downwards,
            // so everything on it counts from a new bottom.
            const S32 want_left = root->getRect().mLeft;
            const S32 want_down = canvas->surfaceHeight() - root->getRect().mTop;
            canvas->rememberRoot();

            CAPTURE(again);
            UNSCOPED_INFO("it stayed where it was put, across");
            REQUIRE(root->getRect().mLeft == want_left);
            UNSCOPED_INFO("and down from the top");
            REQUIRE(canvas->surfaceHeight() - root->getRect().mTop == want_down);
            {
                INFO("and the surface reaches under it " + where(root->getRect()));
                REQUIRE(root->getRect().mBottom >= 0);
                REQUIRE(root->getRect().mRight <= canvas->surfaceWidth());
            }
        }
        canvas->die();
    }

    // Asking a row to lay itself out twice gives the same answer twice.
    // A row that says something different every time it is asked moves what
    // is under the pointer on every frame, and then nothing on it can be
    // dragged: the hand goes one way and the thing being dragged goes
    // another, because the coordinates it is being dragged in have moved.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a row laid out twice gives the same answer twice", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row";
        rp.rect = area->getLocalRect();
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        ALCanvasRow* row = new ALCanvasRow(rp);
        row->initFromParams(rp);
        area->addChild(row);

        ALCanvasView* canvas = surface();
        row->addCanvas(canvas);
        row->show(canvas, true);

        // Big enough to want scrollbars, which is the case where the room
        // the row is told about depends on what the row did last time.
        place(canvas, ROOM_W * 2, ROOM_H * 2);

        LLRect settled;
        for (S32 pass = 0; pass < 6; ++pass)
        {
            row->shareRoom();
            row->layout();
            if (pass == 1)
            {
                settled = row->getRect();
            }
            else if (pass > 1)
            {
                CAPTURE(pass);
                UNSCOPED_INFO("the row is where it was, across");
                REQUIRE(row->getRect().getWidth() == settled.getWidth());
                UNSCOPED_INFO("and down");
                REQUIRE(row->getRect().getHeight() == settled.getHeight());
                UNSCOPED_INFO("and so is what is on it");
                REQUIRE(canvas->getRect().mLeft == 0);
            }
        }
        area->die();
    }

    // What is on a surface can move itself. A previewed window dragged by
    // its own title bar does: nothing tells the surface, so the surface asks
    // -- and without that the next change of size puts the window back where
    // the surface last put it, which is a window that cannot be dragged by
    // the handle it is meant to be dragged by.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a root moved by something else stays where it was moved", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);

        // Moved by something that is not the surface.
        root->translate(70, -40);
        const LLRect moved = root->getRect();
        canvas->refresh();
        UNSCOPED_INFO("it is still where it was moved to, across");
        CHECK(root->getRect().mLeft == moved.mLeft);

        // And it stays there through the change of size that used to undo it.
        const S32 from_top = canvas->surfaceHeight() - root->getRect().mTop;
        canvas->setLeastSurface(600, 500);
        UNSCOPED_INFO("across a change of size, across");
        CHECK(root->getRect().mLeft == moved.mLeft);
        UNSCOPED_INFO("and down from the top");
        CHECK(canvas->surfaceHeight() - root->getRect().mTop == from_top);

        // Asking again when nothing has moved changes nothing.
        const LLRect settled = root->getRect();
        canvas->refresh();
        UNSCOPED_INFO("asking twice is asking once");
        CHECK(where(root->getRect()) == where(settled));
        canvas->die();
    }

    // A surface reaches to a margin past what is on it, so whatever was last
    // dragged towards a far edge is sitting right against that edge -- well
    // inside the distance a drag snaps from. Line the window up with the edge
    // and the surface grows to keep its margin, which puts the edge back
    // against the window, which lines it up again. A drag is offered the
    // pointer every frame whether it moved or not, so this runs on its own: a
    // window that shakes where it is held, over a surface that grows without
    // end under it.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a root held near the surface's far edge does not snap or drift", "[alcanvasview]")
    {
        // How far a drag snaps from: LLDragHandle::sSnapMargin.
        constexpr S32 SNAP_FROM = 5;

        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);

        // Far enough down that the surface has had to grow to hold it, which
        // is what brings its bottom edge up against the root's.
        root->translate(40, -160);
        canvas->rememberRoot();
        {
            INFO("the surface reaches just under it " + where(root->getRect()));
            CHECK(root->getRect().mBottom > 0);
            CHECK(root->getRect().mBottom <= SNAP_FROM);
        }

        const LLRect held = root->getRect();
        const S32 was = canvas->surfaceHeight();
        for (S32 frame = 0; frame < 8; ++frame)
        {
            CAPTURE(frame);
            LLRect snapped;
            LLView* to = root->findSnapRect(snapped, LLCoordGL(0, -1),
                                            LLView::SNAP_PARENT_AND_SIBLINGS, SNAP_FROM);
            UNSCOPED_INFO("nothing on a surface has an edge of it to line up with");
            REQUIRE(to == nullptr);
            root->setShape(snapped, true);
            canvas->refresh();
        }
        UNSCOPED_INFO("it is still where it was held");
        CHECK(where(root->getRect()) == where(held));
        UNSCOPED_INFO("and the surface did not grow under it");
        CHECK(canvas->surfaceHeight() == was);
        canvas->die();
    }

    // The tables under a canvas fold away and come back, which is the region
    // growing and shrinking under a surface that is sized to what is on it.
    // A surface pins its own bottom when it resizes itself, so a surface that
    // has grown reaches past the top of the row until the row places it
    // again. What hangs over the row is drawn nowhere and clicked on by
    // nobody: a preview whose window is up there cannot be picked up by its
    // title bar, however many times it is dragged at.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: tables folding under a row keep the surface and window inside it", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row";
        rp.rect = area->getLocalRect();
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        ALCanvasRow* row = new ALCanvasRow(rp);
        row->initFromParams(rp);
        area->addChild(row);

        ALCanvasView* canvas = surface();
        row->addCanvas(canvas);
        row->show(canvas, true);
        LLPanel* root = place(canvas, 300, 200);
        row->settle();

        for (S32 turn = 0; turn < 6; ++turn)
        {
            CAPTURE(turn);

            // The band folding away and coming back.
            area->reshape(ROOM_W, (turn % 2) ? ROOM_H : ROOM_H / 2);
            row->settle();

            // And the window nudged across the surface, which is a drag.
            root->translate(0, -12);
            canvas->refresh();
            row->settle();

            UNSCOPED_INFO("the surface is inside the row, " + where(canvas->getRect())
                   + " in " + where(row->getLocalRect()));
            REQUIRE(row->getLocalRect().contains(canvas->getRect()));
            UNSCOPED_INFO("and the window is on the surface, " + where(root->getRect())
                   + " on " + where(canvas->getLocalRect()));
            REQUIRE(canvas->getLocalRect().contains(root->getRect()));
        }
        area->die();
    }

    // The one that is reported: a window on the canvas, the tables under it
    // folded away, and the window can no longer be picked up by its title
    // bar. Folding the tables is the region growing taller under a preview
    // that was bigger than it, so what was scrolled to becomes what fits --
    // and wherever that leaves the window, it has to be somewhere the
    // pointer can reach it.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a window's title bar stays reachable with the tables folded away", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row";
        rp.rect = area->getLocalRect();
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        ALCanvasRow* row = new ALCanvasRow(rp);
        row->initFromParams(rp);
        area->addChild(row);

        ALCanvasView* canvas = surface();
        row->addCanvas(canvas);
        row->show(canvas, true);

        // Taller than the region, which is what the tables being there means.
        LLPanel* root = place(canvas, 600, ROOM_H + 120);
        row->settle();

        // The title bar: the top of the window, which is what is grabbed.
        const auto title = [root]()
        {
            LLRect bar(root->calcScreenRect());
            bar.mBottom = bar.mTop - 16;
            return bar;
        };
        UNSCOPED_INFO("with the tables there, the title bar can be reached");
        CHECK(area->calcScreenRect().overlaps(title()));

        // The tables folded away: the region grows and the preview fits.
        area->reshape(ROOM_W, ROOM_H * 2);
        row->settle();

        LLRect window;
        area->localRectToScreen(area->getContentWindowRect(), &window);
        UNSCOPED_INFO("with them folded away it still can, " + where(title()) + " in " + where(window));
        CHECK(window.overlaps(title()));
        area->die();
    }

    // A window on the canvas picked up by its own title bar. That is not a
    // translate: a drag handle hands the floater a whole new rect and says a
    // person put it there, and a floater answers that with more than a panel
    // does. It has to land the same whether the surface is bigger than the
    // region it is shown in or smaller, because folding the tables away
    // underneath changes which of those it is and nothing else.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a floater dragged by its title bar moves the same at any region size", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row";
        rp.rect = area->getLocalRect();
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        ALCanvasRow* row = new ALCanvasRow(rp);
        row->initFromParams(rp);
        area->addChild(row);

        ALCanvasView* canvas = surface();
        row->addCanvas(canvas);
        row->show(canvas, true);

        // Built the way the studio builds a floater preview: parented first,
        // so nothing else claims it, then placed at the surface's corner.
        LLFloater::Params fp(LLFloater::getDefaultParams());
        fp.name = "preview";
        fp.rect = LLRect(0, 300, 400, 0);
        LLFloater* floater = new LLFloater(LLSD(), fp);
        canvas->addChild(floater);
        floater->setVisible(true);
        canvas->fitContent(408, 308);
        floater->setOrigin(4, canvas->surfaceHeight() - 300 - 4);
        canvas->setRoot(floater);
        row->settle();

        // What a drag handle does with the pointer, and what the surface is
        // given the chance to do about it.
        const auto drag_down = [&](S32 by)
        {
            LLRect moved(floater->getRect());
            moved.translate(0, -by);
            floater->setShape(moved, true);
            canvas->refresh();
            row->settle();
        };
        // Where it sits, counted from the corner a surface grows out of: its
        // own y is not that, because a surface that grows does so downwards.
        const auto down_from_top = [&]()
        {
            return canvas->surfaceHeight() - floater->getRect().mTop;
        };

        // Whether a press on the title bar reaches the window at all, which
        // is what has to be true before any of the rest of it matters.
        const auto title_reached = [&]()
        {
            const LLRect& r = floater->getRect();
            const S32 on_the_bar_x = r.mLeft + r.getWidth() / 2;
            const S32 on_the_bar_y = r.mTop - 8;
            return canvas->childFromPoint(on_the_bar_x, on_the_bar_y, false) == floater
                && floater->pointInView(on_the_bar_x - r.mLeft, on_the_bar_y - r.mBottom);
        };
        UNSCOPED_INFO("a press on the title bar reaches the window");
        CHECK(title_reached());

        // The tables there: the surface is taller than the region, and what
        // does not fit is scrolled to.
        area->reshape(ROOM_W, 200);
        row->settle();
        UNSCOPED_INFO("with the tables there it still reaches it");
        CHECK(title_reached());
        S32 was = down_from_top();
        drag_down(20);
        UNSCOPED_INFO("with the tables there it moves");
        CHECK(down_from_top() == was + 20);

        // Folded away: the region is taller than the surface.
        area->reshape(ROOM_W, 800);
        row->settle();
        UNSCOPED_INFO("with them folded away it still reaches it");
        CHECK(title_reached());
        was = down_from_top();
        drag_down(20);
        UNSCOPED_INFO("with them folded away it moves too");
        CHECK(down_from_top() == was + 20);
        area->die();
    }

    // An edit rebuilds what is on the surface, and a rebuild starts by
    // clearing it. Where the last root was left has to outlive that, or
    // writing one field sends the window it is about back to the corner.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: where the root was left outlives the surface being cleared", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        S32 left = -1;
        S32 down = -1;
        UNSCOPED_INFO("a surface with nothing on it has no place to keep");
        CHECK_FALSE(canvas->keptPlace(left, down));

        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);
        root->translate(40, -60);
        canvas->rememberRoot();
        UNSCOPED_INFO("once something has been put on it, it has");
        CHECK(canvas->keptPlace(left, down));
        CHECK(left == root->getRect().mLeft);
        UNSCOPED_INFO("and down from the top");
        CHECK(down == canvas->surfaceHeight() - root->getRect().mTop);

        // The rebuild: everything on it goes, and what is built next asks.
        const S32 was_left = left;
        const S32 was_down = down;
        canvas->clear();
        left = -1;
        down = -1;
        UNSCOPED_INFO("and still has it once the surface is cleared");
        CHECK(canvas->keptPlace(left, down));
        CHECK(left == was_left);
        CHECK(down == was_down);
        canvas->die();
    }

    // The keyboard given to a surface stays on the surface. A panel handed
    // the keyboard passes it to the first thing in it that will take it,
    // which here is a widget in the picture -- and a line editor in the
    // picture keeps the arrows, so the element they were meant to move
    // stayed put while the caret in a previewed box went left and right.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: the keyboard given to a surface stays on the surface", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALCanvasView* canvas = surface();
        gFloaterView->addChild(canvas);
        LLPanel* root = place(canvas, 200, 100);

        LLLineEditor::Params ep(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
        ep.name = "box";
        ep.rect = LLRect(10, 60, 150, 40);
        LLLineEditor* box = LLUICtrlFactory::create<LLLineEditor>(ep);
        root->addChild(box);

        canvas->setFocus(true);
        UNSCOPED_INFO("the surface has the keyboard");
        CHECK(canvas->hasFocus());
        UNSCOPED_INFO("and the box in the picture does not");
        CHECK_FALSE(box->hasFocus());

        // A box in the picture can still be given the keyboard on purpose:
        // a plain click on a previewed widget is the preview working.
        box->setFocus(true);
        UNSCOPED_INFO("asked for directly, the box takes it");
        CHECK(box->hasFocus());
        box->setFocus(false);
        canvas->die();
    }

    // The wheel climbs a ladder that doubles and halves rather than a fixed
    // amount, and stops at the ends of the zoom's range.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: steppedZoom climbs a ladder of rungs and stops at its ends", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();

        UNSCOPED_INFO("life size to start");
        CHECK(canvas->zoom() == 1.f);
        const F32 up[] = { 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f };
        for (const F32 step : up)
        {
            canvas->setZoom(canvas->steppedZoom(1));
            UNSCOPED_INFO("up a rung");
            REQUIRE(canvas->zoom() == step);
        }
        UNSCOPED_INFO("and no further");
        CHECK(canvas->steppedZoom(1) == ALCanvasView::MAX_ZOOM);

        canvas->setZoom(1.f);
        const F32 down[] = { 0.75f, 0.5f, 0.25f };
        for (const F32 step : down)
        {
            canvas->setZoom(canvas->steppedZoom(-1));
            UNSCOPED_INFO("down a rung");
            REQUIRE(canvas->zoom() == step);
        }
        UNSCOPED_INFO("to the floor");
        CHECK(canvas->steppedZoom(-1) == ALCanvasView::MIN_ZOOM);

        // A zoom between rungs goes to the next rung, not past it.
        canvas->setZoom(2.4f);
        UNSCOPED_INFO("between rungs, up");
        CHECK(canvas->steppedZoom(1) == 3.f);
        UNSCOPED_INFO("between rungs, down");
        CHECK(canvas->steppedZoom(-1) == 2.f);
        canvas->setZoom(100.f);
        UNSCOPED_INFO("asked for too much, it is the ceiling");
        CHECK(canvas->zoom() == ALCanvasView::MAX_ZOOM);
        canvas->die();
    }

    // The zoom that fits is the largest rung at which what is on the
    // surface still fits the room the surface is shown in.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: fittingZoom is the largest rung at which the content fits the room", "[alcanvasview]")
    {
        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "room";
        rp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLPanel* room = LLUICtrlFactory::create<LLPanel>(rp);

        ALCanvasView* canvas = surface();
        room->addChild(canvas);
        place(canvas, 200, 100);

        // 208 by 108 with its margin: three times over fits 700 by 400,
        // four times over does not.
        UNSCOPED_INFO("three times over");
        CHECK(canvas->fittingZoom() == 3.f);
        place(canvas, 1000, 100);
        UNSCOPED_INFO("too wide even at life size, so half");
        CHECK(canvas->fittingZoom() == 0.5f);
        // A surface is never narrower than 120 of its own units, so a small
        // thing fits at four times over and not the eight its own size
        // would allow.
        place(canvas, 20, 20);
        UNSCOPED_INFO("a small thing stops where the surface's floor does");
        CHECK(canvas->fittingZoom() == 4.f);
        room->die();
    }

    // A surface that is the one thing in its container fills the container,
    // so what is on it sits in the middle of the room until it outgrows it.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a surface alone in its container fills it until it outgrows it", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        ALCanvasView* canvas = surface();
        area->addChild(canvas);
        canvas->fitContent(200, 100);
        canvas->refresh();

        const LLRect room = area->getContentWindowRect();
        CHECK(canvas->getRect().getWidth() == room.getWidth());
        CHECK(canvas->getRect().getHeight() == room.getHeight());

        canvas->setZoom(4.f);
        canvas->refresh();
        UNSCOPED_INFO("zoomed past the room, the surface is bigger than it " + size(canvas));
        CHECK(canvas->getRect().getWidth() > room.getWidth());
        area->die();
    }

    // What the canvas says about itself: empty until something is on it,
    // and the zoom as a percentage once there is.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: empty and legend describe the canvas and backdrops go by name", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();

        CHECK(canvas->empty());
        UNSCOPED_INFO("life size reads as a hundred");
        CHECK(canvas->legend() == std::string("100%"));
        place(canvas, 200, 100);
        CHECK_FALSE(canvas->empty());
        canvas->setZoom(2.f);
        UNSCOPED_INFO("twice life size");
        CHECK(canvas->legend() == std::string("200%"));
        canvas->die();

        UNSCOPED_INFO("a backdrop by name");
        CHECK(ALCanvasView::backdropNamed("checker") == ALCanvasView::Backdrop::Checker);
        UNSCOPED_INFO("and back");
        CHECK(std::string(ALCanvasView::backdropName(ALCanvasView::Backdrop::Light)) == std::string("light"));
        UNSCOPED_INFO("an unknown name is none");
        CHECK(ALCanvasView::backdropNamed("velvet") == ALCanvasView::Backdrop::None);
    }

    // A surface told to fill a room never draws past it, whatever the zoom
    // makes of the division: a pixel over brings out a scrollbar, which
    // shrinks the room, which rounds the other way, and the canvas shakes.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a surface told to fill a room never draws past it at any zoom", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->fitContent(120, 40);
        // Zooms at which what is on it still fits the room: past that a
        // scrollbar is right.
        for (const F32 zoom : { 4.f, 3.f, 1.5f, 5.f })
        {
            canvas->setZoom(zoom);
            for (S32 room = 640; room < 652; ++room)
            {
                CAPTURE(zoom, room);
                canvas->setLeastSurface(room, room - 96);
                UNSCOPED_INFO("no wider than the room");
                REQUIRE(canvas->getRect().getWidth() <= room);
                UNSCOPED_INFO("nor taller");
                REQUIRE(canvas->getRect().getHeight() <= room - 96);
                UNSCOPED_INFO("and not far short of it either");
                REQUIRE(canvas->getRect().getWidth() > room - zoom - 1);
            }
        }
        canvas->die();
    }

    // Zooming about a point keeps that point of the surface where it was in
    // the window: the container is scrolled by however much the zoom moved
    // it. Where the pointer is in the window, read the way the canvas reads
    // it.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: zoomAbout keeps the point it zooms about where it was in the window", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        ALCanvasView* canvas = surface();
        area->addChild(canvas);
        canvas->fitContent(1000, 800);
        canvas->refresh();
        area->getContentWindowRect();

        // A point of the surface, and where it is in the window before.
        const auto inWindow = [&](S32 x, S32 y, S32& wx, S32& wy)
        {
            canvas->localPointToOtherView(x, y, &wx, &wy, area);
            const LLRect window = area->getContentWindowRect();
            wx -= window.mLeft;
            wy -= window.mBottom;
        };
        S32 before_x = 0;
        S32 before_y = 0;
        inWindow(300, 500, before_x, before_y);

        canvas->zoomAbout(2.f, 300, 500);
        CHECK(canvas->zoom() == 2.f);
        UNSCOPED_INFO("scrolled across to keep the point");
        CHECK(area->getDocPosHorizontal() == 300);

        // The same point of the surface is drawn at twice the distance from
        // the corner, and the container has been scrolled so that it is
        // still where it was in the window.
        S32 after_x = 0;
        S32 after_y = 0;
        inWindow(600, 1000, after_x, after_y);
        CHECK(after_x == before_x);
        CHECK(after_y == before_y);
        area->die();
    }

    // A pan is the container scrolled by how far the pointer went, and no
    // further than there is to scroll.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a pan scrolls the container by the drag and no further than it can", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollContainer::Params cp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        cp.name = "area";
        cp.rect = LLRect(0, ROOM_H, ROOM_W, 0);
        LLScrollContainer* area = LLUICtrlFactory::create<LLScrollContainer>(cp);

        ALCanvasView* canvas = surface();
        area->addChild(canvas);
        canvas->fitContent(1000, 800);
        canvas->refresh();
        area->getContentWindowRect();

        CHECK_FALSE(canvas->panning());
        canvas->beginPan(100, 100);
        CHECK(canvas->panning());
        // Dragged left and down: what is seen moves that way, so the
        // container scrolls right and up.
        canvas->panTo(50, 80);
        UNSCOPED_INFO("scrolled right by the drag");
        CHECK(area->getDocPosHorizontal() == 50);
        UNSCOPED_INFO("and not above the top");
        CHECK(area->getDocPosVertical() == 0);
        canvas->panTo(150, 130);
        UNSCOPED_INFO("not past the left edge");
        CHECK(area->getDocPosHorizontal() == 0);
        UNSCOPED_INFO("scrolled down by the drag");
        CHECK(area->getDocPosVertical() == 30);
        canvas->endPan();
        CHECK_FALSE(canvas->panning());

        canvas->panBy(-20, 10);
        UNSCOPED_INFO("panned by amount across");
        CHECK(area->getDocPosHorizontal() == 20);
        UNSCOPED_INFO("and down");
        CHECK(area->getDocPosVertical() == 40);

        // panGesture() is whether a left press pans, which the space bar
        // held makes it too: asked before the surface says it drags and
        // after, it shows the saying is what changed it.
        UNSCOPED_INFO("a plain surface does not drag by the left button");
        CHECK_FALSE(canvas->panGesture());
        canvas->setDragPans(true);
        UNSCOPED_INFO("until it says so");
        CHECK(canvas->panGesture());
        area->die();
    }

    // A viewport keeps the size it is given, whatever is on it, and what is
    // on it may be left of and below nought: the pointer reaches it through
    // the origin and the zoom, and the way back out lands where it went in.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a viewport keeps its size and maps the pointer through its origin", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setViewport(true);
        canvas->fitContent(3000, 2000);
        UNSCOPED_INFO("its place's width, not its content's");
        CHECK(canvas->getRect().getWidth() == ROOM_W);
        UNSCOPED_INFO("and height");
        CHECK(canvas->getRect().getHeight() == ROOM_H);

        canvas->setContentOrigin(300.f, 200.f);
        Target* node = target(canvas, LLRect(-100, -40, -60, -60));
        S32     x = -80, y = -50;
        canvas->toDrawn(x, y);
        UNSCOPED_INFO("drawn across from the origin");
        CHECK(x == 220);
        UNSCOPED_INFO("and down");
        CHECK(y == 150);
        canvas->toContent(x, y);
        {
            INFO("and back where it was");
            CHECK(x == -80);
            CHECK(y == -50);
        }
        UNSCOPED_INFO("clicked where it is drawn");
        CHECK(canvas->handleMouseDown(220, 150, MASK_NONE));
        {
            INFO("reached, in its own coordinates");
            REQUIRE(node->clicks.size() == 1);
            CHECK(node->clicks[0] == std::make_pair(20, 10));
        }

        canvas->setZoom(2.f);
        x = -80;
        y = -50;
        canvas->toDrawn(x, y);
        canvas->handleMouseDown(x, y, MASK_NONE);
        {
            INFO("reached at twice the size too");
            REQUIRE(node->clicks.size() == 2);
            CHECK(node->clicks[1] == std::make_pair(20, 10));
        }
        const LLRect seen = canvas->contentInView();
        {
            INFO("what is seen, in the surface's own terms, holds it");
            CHECK(seen.mLeft <= -100);
            CHECK(seen.mRight >= -60);
            CHECK(seen.mBottom <= -60);
            CHECK(seen.mTop >= -40);
        }
        canvas->die();
    }

    // A viewport pans by its origin, with no container to scroll; zooms
    // about the pointer or the middle keeping that point where it was; and
    // keeps its top left where it was as its place changes height.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a viewport pans and zooms by its origin with no container to scroll", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCanvasView* canvas = surface(false);
        gFloaterView->addChild(canvas);
        canvas->setViewport(true);
        canvas->beginPan(100, 100);
        canvas->panTo(160, 70);
        {
            INFO("the origin follows the hand");
            CHECK(canvas->contentOriginX() == 60.f);
            CHECK(canvas->contentOriginY() == -30.f);
        }
        canvas->endPan();
        canvas->panBy(-10, 5);
        {
            INFO("and moves by an amount");
            CHECK(canvas->contentOriginX() == 50.f);
            CHECK(canvas->contentOriginY() == -25.f);
        }

        const auto contentAt = [canvas](S32 x, S32 y) {
            canvas->toContent(x, y);
            return std::make_pair(x, y);
        };
        const std::pair<S32, S32> under = contentAt(300, 200);
        canvas->zoomAbout(2.f, 300, 200);
        CHECK(canvas->zoom() == 2.f);
        UNSCOPED_INFO("what was under the pointer is under it still");
        CHECK(contentAt(300, 200) == under);
        const std::pair<S32, S32> middle = contentAt(ROOM_W / 2, ROOM_H / 2);
        canvas->setZoom(4.f);
        UNSCOPED_INFO("a zoom asked for keeps the middle");
        CHECK(contentAt(ROOM_W / 2, ROOM_H / 2) == middle);

        const std::pair<S32, S32> corner = contentAt(0, ROOM_H);
        canvas->reshape(ROOM_W, ROOM_H + 100);
        UNSCOPED_INFO("the top left stays put as the place grows");
        CHECK(contentAt(0, ROOM_H + 100) == corner);
        canvas->die();
    }

    // A drop reaches what is on a surface where it is drawn, zoomed or
    // panned, bounded or not.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a drop reaches what is on a surface where it is drawn", "[alcanvasview]")
    {
        std::string tip;
        EAcceptance accept = ACCEPT_NO;
        {
            ALCanvasView* canvas = surface();
            Target* node = target(canvas, LLRect(40, 80, 100, 20));
            canvas->setZoom(2.f);
            UNSCOPED_INFO("taken");
            CHECK(canvas->handleDragAndDrop(100, 60, MASK_NONE, true, DAD_NOTECARD, nullptr, &accept, tip));
            {
                INFO("on a zoomed surface, in its own coordinates");
                REQUIRE(node->drops.size() == 1);
                CHECK(node->drops[0] == std::make_pair(10, 10));
            }
            canvas->die();
        }
        {
            ALCanvasView* canvas = surface();
            canvas->setViewport(true);
            canvas->setContentOrigin(200.f, 100.f);
            Target* node = target(canvas, LLRect(-50, 0, 0, -50));
            accept = ACCEPT_NO;
            canvas->handleDragAndDrop(160, 70, MASK_NONE, false, DAD_NOTECARD, nullptr, &accept, tip);
            {
                INFO("in a viewport, left of and below nought");
                REQUIRE(node->drops.size() == 1);
                CHECK(node->drops[0] == std::make_pair(10, 20));
            }
            UNSCOPED_INFO("and what it says of it answered");
            CHECK(accept == ACCEPT_YES_SINGLE);
            canvas->die();
        }
    }

    // What is on a zoomed or panned canvas is on the screen where it is
    // drawn, which is what a popover beside it is placed by -- not where
    // its own screen rect, at a hundred per cent from the corner, says.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: screenRectOf places a child where a zoomed and panned canvas draws it", "[alcanvasview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCanvasView* canvas = surface(false);
        gFloaterView->addChild(canvas);
        canvas->setViewport(true);
        canvas->setContentOrigin(250.f, 150.f);
        canvas->setZoom(2.f);
        Target* node = target(canvas, LLRect(-20, 30, 20, 10));
        S32 sx = 0, sy = 0;
        canvas->localPointToScreen(0, 0, &sx, &sy);
        const F32    ox     = canvas->contentOriginX();
        const F32    oy     = canvas->contentOriginY();
        const LLRect screen = canvas->screenRectOf(node);
        CHECK(screen.mLeft == sx + ll_round(ox - 40.f));
        CHECK(screen.mRight == sx + ll_round(ox + 40.f));
        CHECK(screen.mTop == sy + ll_round(oy + 60.f));
        CHECK(screen.mBottom == sy + ll_round(oy + 20.f));
        UNSCOPED_INFO("not where its own rect says");
        CHECK(node->calcScreenRect() != screen);
        S32 x = -20, y = 10;
        canvas->toScreen(x, y);
        {
            INFO("a point out the same way");
            CHECK(x == screen.mLeft);
            CHECK(y == screen.mBottom);
        }
        canvas->die();
    }

    // A bounded surface's origin is its corner: moved by nothing, and
    // back at the corner when a viewport stops being one.
    TEST_CASE_METHOD(alcanvasview_data, "alcanvasview: a bounded surface's origin is its corner", "[alcanvasview]")
    {
        ALCanvasView* canvas = surface();
        canvas->setContentOrigin(40.f, 40.f);
        {
            INFO("not a viewport's to move");
            CHECK(canvas->contentOriginX() == 0.f);
            CHECK(canvas->contentOriginY() == 0.f);
        }
        canvas->setViewport(true);
        canvas->setContentOrigin(40.f, 40.f);
        canvas->setViewport(false);
        {
            INFO("a corner again");
            CHECK(canvas->contentOriginX() == 0.f);
            CHECK(canvas->contentOriginY() == 0.f);
        }
        S32 x = 30, y = 30;
        canvas->setZoom(2.f);
        canvas->toContent(x, y);
        {
            INFO("and the zoom alone taken out");
            CHECK(x == 15);
            CHECK(y == 15);
        }
        canvas->die();
    }
}
