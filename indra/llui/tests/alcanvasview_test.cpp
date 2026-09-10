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

#include "../alcanvasview.h"

#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../llscrollbar.h"
#include "../llscrollcontainer.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gCanvasTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gCanvasTestAnonName;
}

namespace tut
{
    struct alcanvasview_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

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

    typedef test_group<alcanvasview_data> alcanvasview_test;
    typedef alcanvasview_test::object     alcanvasview_object;
    tut::alcanvasview_test alcanvasview_testgroup("alcanvasview");

    // A surface is never smaller than the room it is shown in: one that
    // stopped at the edge of what is on it has nowhere to draw a rule along
    // and nothing to drop onto.
    template<> template<>
    void alcanvasview_object::test<1>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 200, 100);

        ensure_equals("as wide as the room", canvas->getRect().getWidth(), ROOM_W);
        ensure_equals("and as tall", canvas->getRect().getHeight(), ROOM_H);
        canvas->die();
    }

    // And never smaller than what is on it, which is the whole of why a
    // canvas scrolls.
    template<> template<>
    void alcanvasview_object::test<2>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 900, 800);

        ensure("wider than the room " + size(canvas), canvas->getRect().getWidth() > ROOM_W);
        ensure("and taller " + size(canvas), canvas->getRect().getHeight() > ROOM_H);
        canvas->die();
    }

    // A surface has two sizes. What is on it keeps its own numbers; what it
    // covers is those times the zoom, and that is what has to be scrolled.
    template<> template<>
    void alcanvasview_object::test<3>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 400, 300);

        canvas->setZoom(2.f);
        ensure_equals("the surface is still measured in the file's numbers",
                      canvas->surfaceWidth(), 408);
        ensure_equals("and it covers twice that", canvas->getRect().getWidth(), 816);
        ensure("which is more than the room " + size(canvas),
               canvas->getRect().getWidth() > ROOM_W);

        // Drawn smaller, the surface grows in its own coordinates instead,
        // so the region is still filled and there is still nothing to scroll.
        canvas->setZoom(0.5f);
        ensure_equals("half size covers the room exactly", canvas->getRect().getWidth(), ROOM_W);
        ensure_equals("and no more of it", canvas->getRect().getHeight(), ROOM_H);
        ensure("so the surface itself is twice the room", canvas->surfaceWidth() >= ROOM_W * 2);
        canvas->die();
    }

    // The pointer is taken out of the zoom at one door, so everything that
    // finds or moves anything works in the file's own numbers.
    template<> template<>
    void alcanvasview_object::test<4>()
    {
        ALCanvasView* canvas = surface();
        canvas->setZoom(2.f);
        S32 x = 100;
        S32 y = 50;
        canvas->toContent(x, y);
        ensure_equals("across", x, 50);
        ensure_equals("down", y, 25);
        canvas->die();
    }

    // The surface changes size; what is on it does not. Carrying it by its
    // follows flags would stretch a previewed window to the size of the
    // thing previewing it.
    template<> template<>
    void alcanvasview_object::test<5>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);
        ensure_equals("it was built at its own size", size(root), std::string("200 x 100"));

        canvas->setLeastSurface(ROOM_W, ROOM_H);
        ensure("the surface grew " + size(canvas), canvas->getRect().getWidth() >= ROOM_W);
        ensure_equals("what is on it did not", size(root), std::string("200 x 100"));
        canvas->die();
    }

    // What is on it keeps its distance from the surface's top left corner
    // when the surface changes size, because a view otherwise keeps its
    // distance from the bottom.
    template<> template<>
    void alcanvasview_object::test<6>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100, 4);
        ensure_equals("placed at the margin", root->getRect().mLeft, 4);
        ensure_equals("under the top", canvas->surfaceHeight() - root->getRect().mTop, 4);

        canvas->setLeastSurface(ROOM_W, ROOM_H);
        ensure_equals("still at the margin", root->getRect().mLeft, 4);
        ensure_equals("still under the top", canvas->surfaceHeight() - root->getRect().mTop, 4);
        canvas->die();
    }

    // A child placed past its parent's edge is drawn past it, and a surface
    // cut to the rect would leave that clipped with nowhere to scroll to.
    template<> template<>
    void alcanvasview_object::test<7>()
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

        ensure("the surface reaches what is drawn " + size(canvas),
               canvas->surfaceWidth() > was);
        ensure("as far as it goes", canvas->surfaceWidth() >= 4 + 500);
        canvas->die();
    }

    // A preview moved towards an edge takes the surface with it: where it
    // sits is remembered nowhere else, so the surface has to grow to keep it.
    template<> template<>
    void alcanvasview_object::test<8>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);

        root->translate(400, -300);
        canvas->rememberRoot();

        ensure("the surface followed it across " + size(canvas), canvas->surfaceWidth() >= 604);
        ensure("and down " + size(canvas), canvas->surfaceHeight() >= 404);
        ensure_equals("and it stayed where it was put", root->getRect().mLeft, 404);
        canvas->die();
    }

    // Moved twice, which is what anybody moving anything does. The second
    // move is measured against the surface the first one grew, so a surface
    // that forgets what it needs before reading where the root is puts the
    // root back higher than it was and grows under it again -- a preview
    // that jumps on every drag and walks off the end of what can be
    // scrolled to.
    template<> template<>
    void alcanvasview_object::test<14>()
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

            const std::string which = "move " + std::to_string(again) + ": ";
            ensure_equals(which + "it stayed where it was put, across", root->getRect().mLeft, want_left);
            ensure_equals(which + "and down from the top",
                          canvas->surfaceHeight() - root->getRect().mTop, want_down);
            ensure(which + "and the surface reaches under it " + where(root->getRect()),
                   root->getRect().mBottom >= 0 && root->getRect().mRight <= canvas->surfaceWidth());
        }
        canvas->die();
    }

    // What is on a surface can move itself. A previewed window dragged by
    // its own title bar does: nothing tells the surface, so the surface asks
    // -- and without that the next change of size puts the window back where
    // the surface last put it, which is a window that cannot be dragged by
    // the handle it is meant to be dragged by.
    template<> template<>
    void alcanvasview_object::test<16>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);

        // Moved by something that is not the surface.
        root->translate(70, -40);
        const LLRect moved = root->getRect();
        canvas->refresh();
        ensure_equals("it is still where it was moved to, across",
                      root->getRect().mLeft, moved.mLeft);

        // And it stays there through the change of size that used to undo it.
        const S32 from_top = canvas->surfaceHeight() - root->getRect().mTop;
        canvas->setLeastSurface(600, 500);
        ensure_equals("across a change of size, across", root->getRect().mLeft, moved.mLeft);
        ensure_equals("and down from the top",
                      canvas->surfaceHeight() - root->getRect().mTop, from_top);

        // Asking again when nothing has moved changes nothing.
        const LLRect settled = root->getRect();
        canvas->refresh();
        ensure_equals("asking twice is asking once", where(root->getRect()), where(settled));
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
    template<> template<>
    void alcanvasview_object::test<17>()
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
        ensure("the surface reaches just under it " + where(root->getRect()),
               root->getRect().mBottom > 0 && root->getRect().mBottom <= SNAP_FROM);

        const LLRect held = root->getRect();
        const S32 was = canvas->surfaceHeight();
        for (S32 frame = 0; frame < 8; ++frame)
        {
            LLRect snapped;
            LLView* to = root->findSnapRect(snapped, LLCoordGL(0, -1),
                                            LLView::SNAP_PARENT_AND_SIBLINGS, SNAP_FROM);
            ensure("frame " + std::to_string(frame)
                   + ": nothing on a surface has an edge of it to line up with", to == nullptr);
            root->setShape(snapped, true);
            canvas->refresh();
        }
        ensure_equals("it is still where it was held", where(root->getRect()), where(held));
        ensure_equals("and the surface did not grow under it", canvas->surfaceHeight(), was);
        canvas->die();
    }

    // There is no growing off the top left of a canvas, so what is dragged
    // that way stops at the corner rather than disappearing over it.
    template<> template<>
    void alcanvasview_object::test<9>()
    {
        ALCanvasView* canvas = surface();
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        LLPanel* root = place(canvas, 200, 100);

        root->translate(-80, 200);
        canvas->rememberRoot();

        ensure("it did not go off the left " + where(root->getRect()), root->getRect().mLeft >= 0);
        ensure("nor off the top " + where(root->getRect()),
               root->getRect().mTop <= canvas->surfaceHeight());
        canvas->die();
    }

    // A surface that is not sizable is whatever it was given: a canvas in a
    // window of its own is sized by the window.
    template<> template<>
    void alcanvasview_object::test<10>()
    {
        ALCanvasView* canvas = surface(false);
        canvas->setLeastSurface(ROOM_W, ROOM_H);
        place(canvas, 900, 800);
        ensure_equals("it kept the rect it was made with", canvas->getRect().getWidth(), ROOM_W);
        canvas->die();
    }

    // Two variants side by side are one row as wide as both of them, and
    // each gets half the room to fill.
    template<> template<>
    void alcanvasview_object::test<11>()
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
        ensure_equals("each fills its share of the room", first->getRect().getWidth(), half);
        ensure_equals("both of them", second->getRect().getWidth(), half);
        ensure_equals("and the row is the two of them and the gutter",
                      row->getRect().getWidth(), half * 2 + ALCanvasRow::GUTTER);
        ensure_equals("the second is to the right of the first",
                      second->getRect().mLeft, half + ALCanvasRow::GUTTER);
        ensure_equals("and their tops line up", first->getRect().mTop, second->getRect().mTop);
        row->die();
    }

    // And the whole of it: a container, a row, a surface, and something on
    // it too big for the region. This is the question the canvas exists to
    // answer -- what does not fit is scrolled to rather than cut off.
    template<> template<>
    void alcanvasview_object::test<12>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("what fits needs no scrollbar", !down->getVisible() && !across->getVisible());
        ensure_equals("the row covers the region across", row->getRect().getWidth(), ROOM_W);
        ensure_equals("and down", row->getRect().getHeight(), ROOM_H);

        // Something taller than the region.
        canvas->clear();
        place(canvas, 200, ROOM_H * 2);
        row->layout();
        ensure("a preview taller than the region scrolls", down->getVisible());

        // And the same by zooming rather than by building something bigger:
        // a preview drawn past the edge of the region is clipped, and being
        // clipped is what a scrollbar is for.
        canvas->clear();
        place(canvas, 200, 100);
        row->layout();
        ensure("small again, and quiet", !down->getVisible());
        canvas->setZoom(4.f);
        row->layout();
        ensure("zoomed past the region, it scrolls", down->getVisible() || across->getVisible());
        area->die();
    }

    // Asking a row to lay itself out twice gives the same answer twice.
    // A row that says something different every time it is asked moves what
    // is under the pointer on every frame, and then nothing on it can be
    // dragged: the hand goes one way and the thing being dragged goes
    // another, because the coordinates it is being dragged in have moved.
    template<> template<>
    void alcanvasview_object::test<15>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
                ensure_equals("pass " + std::to_string(pass) + ": the row is where it was, across",
                              row->getRect().getWidth(), settled.getWidth());
                ensure_equals("pass " + std::to_string(pass) + ": and down",
                              row->getRect().getHeight(), settled.getHeight());
                ensure_equals("pass " + std::to_string(pass) + ": and so is what is on it",
                              canvas->getRect().mLeft, 0);
            }
        }
        area->die();
    }

    // The tables under a canvas fold away and come back, which is the region
    // growing and shrinking under a surface that is sized to what is on it.
    // A surface pins its own bottom when it resizes itself, so a surface that
    // has grown reaches past the top of the row until the row places it
    // again. What hangs over the row is drawn nowhere and clicked on by
    // nobody: a preview whose window is up there cannot be picked up by its
    // title bar, however many times it is dragged at.
    template<> template<>
    void alcanvasview_object::test<18>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            const std::string which = "turn " + std::to_string(turn) + ": ";

            // The band folding away and coming back.
            area->reshape(ROOM_W, (turn % 2) ? ROOM_H : ROOM_H / 2);
            row->settle();

            // And the window nudged across the surface, which is a drag.
            root->translate(0, -12);
            canvas->refresh();
            row->settle();

            ensure(which + "the surface is inside the row, " + where(canvas->getRect())
                   + " in " + where(row->getLocalRect()),
                   row->getLocalRect().contains(canvas->getRect()));
            ensure(which + "and the window is on the surface, " + where(root->getRect())
                   + " on " + where(canvas->getLocalRect()),
                   canvas->getLocalRect().contains(root->getRect()));
        }
        area->die();
    }

    // The one that is reported: a window on the canvas, the tables under it
    // folded away, and the window can no longer be picked up by its title
    // bar. Folding the tables is the region growing taller under a preview
    // that was bigger than it, so what was scrolled to becomes what fits --
    // and wherever that leaves the window, it has to be somewhere the
    // pointer can reach it.
    template<> template<>
    void alcanvasview_object::test<19>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("with the tables there, the title bar can be reached",
               area->calcScreenRect().overlaps(title()));

        // The tables folded away: the region grows and the preview fits.
        area->reshape(ROOM_W, ROOM_H * 2);
        row->settle();

        LLRect window;
        area->localRectToScreen(area->getContentWindowRect(), &window);
        ensure("with them folded away it still can, " + where(title()) + " in " + where(window),
               window.overlaps(title()));
        area->die();
    }

    // A window on the canvas picked up by its own title bar. That is not a
    // translate: a drag handle hands the floater a whole new rect and says a
    // person put it there, and a floater answers that with more than a panel
    // does. It has to land the same whether the surface is bigger than the
    // region it is shown in or smaller, because folding the tables away
    // underneath changes which of those it is and nothing else.
    template<> template<>
    void alcanvasview_object::test<20>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("a press on the title bar reaches the window", title_reached());

        // The tables there: the surface is taller than the region, and what
        // does not fit is scrolled to.
        area->reshape(ROOM_W, 200);
        row->settle();
        ensure("with the tables there it still reaches it", title_reached());
        S32 was = down_from_top();
        drag_down(20);
        ensure_equals("with the tables there it moves", down_from_top(), was + 20);

        // Folded away: the region is taller than the surface.
        area->reshape(ROOM_W, 800);
        row->settle();
        ensure("with them folded away it still reaches it", title_reached());
        was = down_from_top();
        drag_down(20);
        ensure_equals("with them folded away it moves too", down_from_top(), was + 20);
        area->die();
    }

    // An edit rebuilds what is on the surface, and a rebuild starts by
    // clearing it. Where the last root was left has to outlive that, or
    // writing one field sends the window it is about back to the corner.
    template<> template<>
    void alcanvasview_object::test<21>()
    {
        ALCanvasView* canvas = surface();
        S32 left = -1;
        S32 down = -1;
        ensure("a surface with nothing on it has no place to keep",
               !canvas->keptPlace(left, down));

        canvas->setLeastSurface(300, 200);
        LLPanel* root = place(canvas, 200, 100);
        root->translate(40, -60);
        canvas->rememberRoot();
        ensure("once something has been put on it, it has", canvas->keptPlace(left, down));
        ensure_equals("across", left, root->getRect().mLeft);
        ensure_equals("and down from the top", down,
                      canvas->surfaceHeight() - root->getRect().mTop);

        // The rebuild: everything on it goes, and what is built next asks.
        const S32 was_left = left;
        const S32 was_down = down;
        canvas->clear();
        left = -1;
        down = -1;
        ensure("and still has it once the surface is cleared", canvas->keptPlace(left, down));
        ensure_equals("the same place, across", left, was_left);
        ensure_equals("and down", down, was_down);
        canvas->die();
    }

    // A region with nothing built on it yet is still the canvas's: the row
    // covers it, so a wheel or a drop there reaches a canvas rather than
    // falling through to whatever is behind.
    template<> template<>
    void alcanvasview_object::test<13>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the row covers the region across", row->getRect().getWidth(), region.getWidth());
        ensure_equals("and down", row->getRect().getHeight(), region.getHeight());
        area->die();
    }

    // The keyboard given to a surface stays on the surface. A panel handed
    // the keyboard passes it to the first thing in it that will take it,
    // which here is a widget in the picture -- and a line editor in the
    // picture keeps the arrows, so the element they were meant to move
    // stayed put while the caret in a previewed box went left and right.
    template<> template<>
    void alcanvasview_object::test<22>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the surface has the keyboard", canvas->hasFocus());
        ensure("and the box in the picture does not", !box->hasFocus());

        // A box in the picture can still be given the keyboard on purpose:
        // a plain click on a previewed widget is the preview working.
        box->setFocus(true);
        ensure("asked for directly, the box takes it", box->hasFocus());
        box->setFocus(false);
        canvas->die();
    }
}
