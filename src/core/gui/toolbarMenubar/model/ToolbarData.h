/*
 * Xournal++
 *
 * Toolbar definitions model
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string
#include <vector>  // for vector

#include <glib.h>  // for GKeyFile

class ToolbarEntry;
struct Palette;

class ToolbarData {
public:
    ToolbarData(bool predefined);
    ToolbarData(const ToolbarData& data);
    virtual ~ToolbarData();

    void operator=(const ToolbarData& other);

public:
    const std::string& getName() const;
    void setName(std::string name);
    const std::string& getId() const;
    void setId(std::string id);
    bool isPredefined() const;

    void load(GKeyFile* config, const char* group, const Palette& colorPalette);
    void saveToKeyFile(GKeyFile* config) const;

    // Editing API
    int insertItem(const std::string& toolbar, const std::string& item, int position);
    bool removeItemByID(const std::string& toolbar, int id);

    /// xournalpp-calc: names of the items of one bar (e.g. "toolbarTop1"), in order
    std::vector<std::string> getItemNames(const std::string& toolbar) const;
    /// xournalpp-calc: names of all the bars/entries stored (also those that are not shown, like hidden groups)
    std::vector<std::string> getEntryNames() const;
    /// xournalpp-calc: replace the items of one bar
    void setItemNames(const std::string& toolbar, const std::vector<std::string>& items);
    /// xournalpp-calc: replace all the bars with those of another toolbar configuration (keeps id, name)
    void copyContentsFrom(const ToolbarData& other);

private:
    std::string id;
    std::string name;
    std::vector<ToolbarEntry> contents;

    bool predefined = false;

    friend class ToolbarModel;
    friend class ToolMenuHandler;
};
