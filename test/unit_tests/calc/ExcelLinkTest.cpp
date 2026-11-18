/*
 * Xournal++ Calc
 *
 * Tests for the link with Excel: syntax in the engine (with a fake link), references, and .xlsx files
 *
 * @license GNU GPLv2 or later
 */

#include <filesystem>
#include <map>
#include <string>

#include <gtest/gtest.h>

#include "calc/CalcEngine.h"
#include "calc/ExcelLink.h"
#include "calc/XlsxFile.h"

#include "config-test.h"

using namespace xoj::calc;
using Kind = LineResult::Kind;
namespace fs = std::filesystem;

namespace {

/// Cells in memory
class FakeLink: public CellLink {
public:
    std::string useWorkbook(const std::string& path) override {
        book = path;
        return "\"" + path + "\" (archivo)";
    }
    double read(const std::string& ref) override {
        auto it = cells.find(ref);
        if (it == cells.end()) {
            throw CalcError("la celda " + ref + " está vacía");
        }
        return it->second;
    }
    std::string write(const std::string& ref, double value) override {
        cells[ref] = value;
        return ref;
    }
    std::string book;
    std::map<std::string, double> cells;
};

}  // namespace

TEST(ExcelLink, engineSyntax) {
    FakeLink link;
    link.cells["Cargas!C5"] = 300;
    link.cells["B2"] = 2.5;
    Environment env;
    env.cells = &link;

    auto r = evaluateLine("excel(\"cargas.xlsx\")", env);
    EXPECT_EQ(r.kind, Kind::Evaluation);
    EXPECT_EQ(link.book, "cargas.xlsx");
    EXPECT_EQ(r.result, "→ \"cargas.xlsx\" (archivo)");

    r = evaluateLine("P := xl(\"Cargas!C5\")·kN", env);
    EXPECT_EQ(r.kind, Kind::Definition) << r.error;
    EXPECT_DOUBLE_EQ(env.vars["P"].value, 300e3);

    // A plain cell name does not need quotes
    r = evaluateLine("L := xl(B2) m", env);
    EXPECT_EQ(r.kind, Kind::Definition) << r.error;
    EXPECT_DOUBLE_EQ(env.vars["L"].value, 2.5);

    r = evaluateLine("M := P·L =", env);
    EXPECT_EQ(r.result, "750 kN·m");

    r = evaluateLine("enviar(M, \"Resumen!D4\", kN·m)", env);
    EXPECT_EQ(r.kind, Kind::Evaluation) << r.error;
    EXPECT_DOUBLE_EQ(link.cells["Resumen!D4"], 750);
    EXPECT_EQ(r.result, "→ Resumen!D4: 750 kN·m");

    // Arguments with commas inside, units given as a product
    r = evaluateLine("enviar(max(M, 2 kN·m)/2, \"E4\", kN*m)", env);
    EXPECT_EQ(r.kind, Kind::Evaluation) << r.error;
    EXPECT_DOUBLE_EQ(link.cells["E4"], 375);

    // Dimensionless values do not need a unit
    r = evaluateLine("enviar(3/4, \"F1\")", env);
    EXPECT_EQ(r.kind, Kind::Evaluation) << r.error;
    EXPECT_DOUBLE_EQ(link.cells["F1"], 0.75);
}

TEST(ExcelLink, engineErrors) {
    FakeLink link;
    Environment env;
    env.cells = &link;

    auto r = evaluateLine("enviar(3 kN, \"D4\")", env);
    EXPECT_EQ(r.kind, Kind::Error);
    EXPECT_NE(r.error.find("unidad"), std::string::npos);

    r = evaluateLine("enviar(3 kN, \"D4\", m)", env);
    EXPECT_EQ(r.kind, Kind::Error);

    r = evaluateLine("x := xl(\"Z9\") =", env);
    EXPECT_EQ(r.kind, Kind::Error);
    EXPECT_NE(r.error.find("vacía"), std::string::npos);

    // Without a link
    Environment plain;
    r = evaluateLine("x := xl(\"A1\")", plain);
    EXPECT_EQ(r.kind, Kind::Error);

    // Plain text that starts with the same words is still text
    r = evaluateLine("enviar a revisión: lunes", env);
    EXPECT_EQ(r.kind, Kind::Text);
    r = evaluateLine("excelente", env);
    EXPECT_EQ(r.kind, Kind::Text);
}

TEST(ExcelLink, references) {
    auto r = parseCellRef("C5");
    EXPECT_EQ(r.sheet, "");
    EXPECT_EQ(r.row, 5);
    EXPECT_EQ(r.col, 3);

    r = parseCellRef("Hoja1!$ab$12");
    EXPECT_EQ(r.sheet, "Hoja1");
    EXPECT_EQ(r.cell, "AB12");
    EXPECT_EQ(r.col, 28);

    r = parseCellRef("'Mi hoja'!C5");
    EXPECT_EQ(r.sheet, "Mi hoja");
    EXPECT_EQ(r.toString(), "Mi hoja!C5");

    r = parseCellRef("[otro.xlsx]Datos!A1");
    EXPECT_EQ(r.book, "otro.xlsx");
    EXPECT_EQ(r.sheet, "Datos");

    r = parseCellRef("'[otro.xlsx]Mis datos'!A1");
    EXPECT_EQ(r.book, "otro.xlsx");
    EXPECT_EQ(r.sheet, "Mis datos");

    EXPECT_THROW(parseCellRef("Hoja1!"), CalcError);
    EXPECT_THROW(parseCellRef("12"), CalcError);
    EXPECT_EQ(columnName(1), "A");
    EXPECT_EQ(columnName(26), "Z");
    EXPECT_EQ(columnName(27), "AA");
}

TEST(ExcelLink, cache) {
    CellCache c{{"cargas.xlsx|Cargas!B2", 350.5}, {"a;b|c%.xlsx|C5", -1e-7}};
    EXPECT_EQ(parseCellCache(serializeCellCache(c)), c);
    EXPECT_TRUE(parseCellCache("").empty());
}

class XlsxFileTest: public ::testing::Test {
protected:
    void SetUp() override {
        dir = fs::temp_directory_path() /
              (std::string("jackmath-xlsx-") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::create_directories(dir);
        file = dir / "cargas.xlsx";
        fs::copy_file(fs::path(GET_TESTFILE(u8"calc/cargas.xlsx")), file, fs::copy_options::overwrite_existing);
    }
    void TearDown() override { fs::remove_all(dir); }
    fs::path dir;
    fs::path file;
};

TEST_F(XlsxFileTest, read) {
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("B2")), 350.5);         // first sheet
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("cargas!B2")), 350.5);  // sheet names ignore case
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("B3")), 12.5);          // number written as text with a comma
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("'Mi hoja'!C5")), -0.25);
    EXPECT_THROW(xlsx::read(file, parseCellRef("B4")), CalcError);       // text
    EXPECT_THROW(xlsx::read(file, parseCellRef("Z99")), CalcError);      // empty
    EXPECT_THROW(xlsx::read(file, parseCellRef("Otra!A1")), CalcError);  // no such sheet
    EXPECT_THROW(xlsx::read(dir / "no.xlsx", parseCellRef("A1")), CalcError);
}

TEST_F(XlsxFileTest, write) {
    // Existing cell, new cell in an existing row, new cell in a new row (before and after other rows)
    xlsx::write(file, parseCellRef("B2"), 400);
    xlsx::write(file, parseCellRef("C2"), 1.25);
    xlsx::write(file, parseCellRef("A2"), 3);
    xlsx::write(file, parseCellRef("E7"), -2);
    xlsx::write(file, parseCellRef("A20"), 0.1);
    xlsx::write(file, parseCellRef("'Mi hoja'!C5"), 9);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("B2")), 400);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("C2")), 1.25);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("A2")), 3);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("E7")), -2);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("A20")), 0.1);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("D10")), 7);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("'Mi hoja'!C5")), 9);

    // Text cells can be replaced by numbers, formulas are kept
    xlsx::write(file, parseCellRef("B4"), 5);
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("B4")), 5);
    EXPECT_THROW(xlsx::write(file, parseCellRef("B5"), 1), CalcError);

    // Writing the same value does not touch the file
    auto before = fs::last_write_time(file);
    xlsx::write(file, parseCellRef("B2"), 400);
    EXPECT_EQ(fs::last_write_time(file), before);
}

TEST_F(XlsxFileTest, link) {
    ExcelLink link(dir, true);
    CellCache saved, fresh;
    link.setRegionCache(&saved, &fresh);
    Environment env;
    env.cells = &link;

    auto r = evaluateLine("excel(\"cargas\")", env);  // ".xlsx" may be left out
    EXPECT_EQ(r.kind, Kind::Evaluation) << r.error;
    r = evaluateLine("P := xl(\"Cargas!B2\") kN", env);
    EXPECT_EQ(r.kind, Kind::Definition) << r.error;
    EXPECT_DOUBLE_EQ(env.vars["P"].value, 350.5e3);
    EXPECT_DOUBLE_EQ(fresh["cargas.xlsx|Cargas!B2"], 350.5);

    r = evaluateLine("enviar(2·P, \"Cargas!C2\", kN)", env);
    EXPECT_EQ(r.kind, Kind::Evaluation) << r.error;
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("C2")), 701);

    // The workbook is gone: the saved value is used, with a note
    fs::remove(file);
    saved = fresh;
    fresh.clear();
    r = evaluateLine("Q := xl(\"Cargas!B2\") kN", env);
    EXPECT_EQ(r.kind, Kind::Definition) << r.error;
    EXPECT_DOUBLE_EQ(env.vars["Q"].value, 350.5e3);
    EXPECT_NE(r.note.find("valor guardado"), std::string::npos);
    EXPECT_DOUBLE_EQ(fresh["cargas.xlsx|Cargas!B2"], 350.5);
    r = evaluateLine("R := xl(\"Cargas!B3\") kN", env);
    EXPECT_EQ(r.kind, Kind::Error);

    // Exports never write
    fs::copy_file(fs::path(GET_TESTFILE(u8"calc/cargas.xlsx")), file);
    ExcelLink readOnly(dir, false);
    readOnly.useWorkbook("cargas.xlsx");
    EXPECT_EQ(readOnly.write("B2", 1), "B2 (no enviado)");
    EXPECT_DOUBLE_EQ(xlsx::read(file, parseCellRef("B2")), 350.5);
}
