/*
 * Xournal++ Calc
 *
 * Letterhead for the graph paper: logos and texts of the user, read from the "membrete" configuration folder
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <vector>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "util/Color.h"

namespace xoj::view {

/**
 * The letterhead is configured in <config folder>/membrete/membrete.ini (the logos are in the same folder):
 *
 *   [membrete]
 *   nombre=Formato Unimag
 *   logo_izquierdo=escudo.png
 *   logo_derecho=sellos.png
 *   titulo=UNIVERSIDAD DEL MAGDALENA;Facultad de Ingeniería
 *   campos=Programa;Hoja de cálculo
 *
 * When the file exists, a page format with that name is added to the list (graph paper with "lh=1").
 * Logos and texts belong to the user and are never part of the program.
 */
class Letterhead {
public:
    ~Letterhead();

    /// The letterhead of the user, or nullptr if there is none. Loaded once
    static const Letterhead* get();

    /// Draw it in the box (page coordinates). s is the size of a grid square
    void draw(cairo_t* cr, double left, double top, double right, double bottom, double s, Color lineColor) const;

    std::string name;

private:
    GdkPixbuf* leftLogo = nullptr;
    GdkPixbuf* rightLogo = nullptr;
    std::vector<std::string> title;
    std::vector<std::string> fields;
};

}  // namespace xoj::view
