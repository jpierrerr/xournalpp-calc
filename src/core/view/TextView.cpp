#include "TextView.h"

#include <algorithm>  // for max
#include <cstddef>    // for size_t

#include "calc/MathLayout.h"
#include "model/Text.h"           // for Text
#include "util/Color.h"           // for cairo_set_source_rgbi
#include "util/Matrix.h"          // for Matrix
#include "util/StringUtils.h"     // for StringUtils
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"
#include "view/View.h"            // for Context, OPACITY_NO_AUDIO, view

#include "filesystem.h"  // for path

using namespace xoj::view;

TextView::TextView(const Text* text): text(text) {}

TextView::~TextView() = default;

auto TextView::initPango(cairo_t* cr, const Text* t) -> xoj::util::GObjectSPtr<PangoLayout> {
    auto layout = t->createPangoLayout();
    pango_cairo_update_layout(cr, layout.get());
    pango_context_set_matrix(pango_layout_get_context(layout.get()), nullptr);

    return layout;
}

void TextView::draw(const Context& ctx) const {
    if (text->isInEditing()) {
        // The drawing is handled by gui/TextEditor
        return;
    }

    xoj::util::CairoSaveGuard saveGuard(ctx.cr);

    // make elements without audio translucent when highlighting elements with audio
    if (ctx.fadeOutNonAudio && text->getAudioFilename().empty()) {
        cairo_set_operator(ctx.cr, CAIRO_OPERATOR_OVER);
        Util::cairo_set_source_rgbi(ctx.cr, text->getColor(), OPACITY_NO_AUDIO);
    } else {
        cairo_set_operator(ctx.cr, CAIRO_OPERATOR_SOURCE);
        Util::cairo_set_source_rgbi(ctx.cr, text->getColor());
    }

    text->getTransformation().transformCairo(ctx.cr);

    if (text->isCalc()) {
        // xournalpp-calc: fractions, exponents and roots drawn in two dimensions
        text->calcFormula()->draw(ctx.cr);
        return;
    }

    auto layout = initPango(ctx.cr, text);
    text->applyDisplayContent(layout.get());

    if (text->getLineHeight() > 0) {
        // xournalpp-calc: fixed line pitch (alignment grid). Draw each line on its own baseline, so that a taller
        // line (e.g. with an exponent) does not move down
        const double firstBaseline = text->firstBaseline();
        PangoLayoutIter* iter = pango_layout_get_iter(layout.get());
        int i = 0;
        do {
            PangoRectangle logical;
            pango_layout_iter_get_line_extents(iter, nullptr, &logical);
            cairo_move_to(ctx.cr, static_cast<double>(logical.x) / PANGO_SCALE,
                          firstBaseline + i * text->getLineHeight());
            pango_cairo_show_layout_line(ctx.cr, pango_layout_iter_get_line_readonly(iter));
            i++;
        } while (pango_layout_iter_next_line(iter));
        pango_layout_iter_free(iter);
        // cairo_restore() does not reset the current point: clear it, or the next text is drawn here
        cairo_new_path(ctx.cr);
        return;
    }

    cairo_move_to(ctx.cr, 0, 0);
    pango_cairo_show_layout(ctx.cr, layout.get());
    cairo_new_path(ctx.cr);
}
