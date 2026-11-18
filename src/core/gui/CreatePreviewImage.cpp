#include "CreatePreviewImage.h"

#include "model/PageType.h"  // for PageType
#include "util/Color.h"      // for Color
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"
#include "view/background/BackgroundView.h"  // for BackgroundView

namespace xoj::helper {
auto createPreviewImage(const PageType& pt) -> GtkWidget* {
    // Formats with a header (exam sheet, letterhead) are shown as a whole A4 page, so that the header fits
    const bool hasHeader = pt.config.find("ex=1") != std::string::npos || pt.config.find("lh=1") != std::string::npos;
    const double zoom = hasHeader ? PREVIEW_WIDTH / 595.28 : 0.5;

    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, PREVIEW_WIDTH, PREVIEW_HEIGHT),
                                        xoj::util::adopt);
    xoj::util::CairoSPtr crSPtr(cairo_create(surface.get()), xoj::util::adopt);
    auto cr = crSPtr.get();

    cairo_scale(cr, zoom, zoom);

    auto bgView = xoj::view::BackgroundView::createRuled(PREVIEW_WIDTH / zoom, PREVIEW_HEIGHT / zoom, Colors::white, pt,
                                                         hasHeader ? 2.0 : 1. / zoom);
    bgView->draw(cr);

    cairo_identity_matrix(cr);

    cairo_set_line_width(cr, 2);
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, PREVIEW_WIDTH, 0);
    cairo_line_to(cr, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    cairo_line_to(cr, 0, PREVIEW_HEIGHT);
    cairo_line_to(cr, 0, 0);
    cairo_stroke(cr);

    xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
            gdk_pixbuf_get_from_surface(surface.get(), 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT), xoj::util::adopt);
    return gtk_image_new_from_pixbuf(pixbuf.get());
}
};  // namespace xoj::helper
