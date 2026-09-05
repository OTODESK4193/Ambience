#include "PresetBrowser.h"

PresetBrowser::PresetBrowser(PresetManager& pm, AmbienceLookAndFeel& l)
    : presetManager(pm), laf(l)
{
    catModel.owner = this;
    subModel.owner = this;
    fileModel.owner = this;

    categories = { "Factory", "User", "Favorite" };
    subCategories = { "All", "Room1", "Room2", "Hall1", "Hall2", "Plate", "Spring", "GoldFoil", "InchinDown" };

    catList.setModel(&catModel);
    subList.setModel(&subModel);
    fileList.setModel(&fileModel);

    catList.setRowHeight(32);
    subList.setRowHeight(28);
    fileList.setRowHeight(28);

    for (auto* list : { &catList, &subList, &fileList }) {
        list->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list->setOutlineThickness(1);
        addAndMakeVisible(*list);
    }

    searchBox.setTextToShowWhenEmpty("Search presets...", juce::Colours::grey);
    searchBox.setFont(juce::Font(juce::FontOptions(11.0f)));
    searchBox.onTextChange = [this] { updateFiles(); };
    addAndMakeVisible(searchBox);

    closeButton.onClick = [this] {
        if (onClose) onClose();
        else setVisible(false);
    };
    addAndMakeVisible(closeButton);

    initFactoryPresets();
    refresh();
}

PresetBrowser::~PresetBrowser() {}

void PresetBrowser::initFactoryPresets()
{
    factoryPresets = {
        // ── Room1 ──
        { "Acoustic Guitar Booth", 0, 0.80f, 0.25f, 0.55f, 0.15f, 0.40f, 0.75f,  5.0f, 0.70f, 0.10f, 0.00f, 0.10f, 0 },
        { "Tight Vocal Space",     0, 0.70f, 0.20f, 0.60f, 0.10f, 0.35f, 0.70f,  4.0f, 0.80f, 0.00f, 0.00f, 0.05f, 0 },
        { "Percussion Air",        0, 0.90f, 0.30f, 0.50f, 0.20f, 0.45f, 0.80f,  8.0f, 0.65f, 0.20f, 0.10f, 0.15f, 1 },
        { "Small Drum Room",       0, 0.85f, 0.22f, 0.65f, 0.12f, 0.40f, 0.85f,  6.0f, 0.75f, 0.00f, 0.00f, 0.20f, 1 },

        // ── Room2 ──
        { "Live Studio B",         1, 1.00f, 1.20f, 0.65f, 0.22f, 0.45f, 0.85f, 10.0f, 0.60f, 0.10f, 0.00f, 0.15f, 1 },
        { "Warm Wooden Room",      1, 1.10f, 1.40f, 0.70f, 0.25f, 0.50f, 0.80f, 12.0f, 0.55f, 0.20f, 0.15f, 0.20f, 0 },
        { "Vocal Chamber Lush",    1, 0.95f, 1.10f, 0.75f, 0.20f, 0.40f, 0.90f, 10.0f, 0.65f, 0.00f, 0.00f, 0.10f, 1 },
        { "Snare Fat Room",        1, 1.05f, 1.30f, 0.60f, 0.18f, 0.45f, 0.85f,  8.0f, 0.70f, 0.15f, 0.00f, 0.25f, 2 },

        // ── Hall1 ──
        { "Concert Hall Warm",     2, 1.30f, 1.90f, 0.75f, 0.28f, 0.50f, 0.90f, 20.0f, 0.50f, 0.10f, 0.00f, 0.12f, 0 },
        { "Classical Strings",     2, 1.35f, 2.10f, 0.80f, 0.30f, 0.45f, 0.95f, 25.0f, 0.45f, 0.20f, 0.10f, 0.08f, 0 },
        { "Piano Recital Stage",   2, 1.25f, 1.80f, 0.70f, 0.25f, 0.55f, 0.90f, 18.0f, 0.55f, 0.05f, 0.00f, 0.15f, 0 },
        { "Scoring Stage Lush",    2, 1.40f, 2.30f, 0.85f, 0.35f, 0.40f, 1.00f, 22.0f, 0.40f, 0.15f, 0.05f, 0.10f, 0 },

        // ── Hall2 ──
        { "Symphony Hall Grand",   3, 1.60f, 2.20f, 0.85f, 0.32f, 0.45f, 1.00f, 30.0f, 0.45f, 0.10f, 0.00f, 0.10f, 0 },
        { "Cathedral Vast",        3, 1.80f, 3.50f, 0.90f, 0.38f, 0.35f, 1.00f, 40.0f, 0.35f, 0.30f, 0.10f, 0.05f, 0 },
        { "Ambient Cloud",         3, 1.70f, 4.00f, 0.95f, 0.50f, 0.30f, 1.00f, 35.0f, 0.40f, 0.15f, 0.00f, 0.15f, 1 },
        { "Deep Space Arena",      3, 2.00f, 5.00f, 0.90f, 0.60f, 0.25f, 1.00f, 45.0f, 0.30f, 0.25f, 0.15f, 0.20f, 0 },

        // ── Plate ──
        { "EMT140 Vocal",          4, 0.75f, 1.20f, 0.90f, 0.20f, 0.60f, 0.85f,  0.0f, 0.65f, 0.00f, 0.00f, 0.25f, 1 },
        { "EMT140 Snare Crisp",    4, 0.70f, 0.90f, 0.85f, 0.18f, 0.65f, 0.80f,  0.0f, 0.70f, 0.00f, 0.00f, 0.30f, 2 },
        { "Smooth Silk Plate",     4, 0.80f, 1.50f, 0.95f, 0.25f, 0.55f, 0.90f,  5.0f, 0.60f, 0.10f, 0.05f, 0.20f, 0 },
        { "Bright Gold Shimmer",   4, 0.85f, 1.80f, 0.92f, 0.30f, 0.70f, 0.95f,  0.0f, 0.55f, 0.00f, 0.00f, 0.25f, 1 },

        // ── Spring ──
        { "Vintage Twin Tank",     5, 0.50f, 2.80f, 0.60f, 0.35f, 0.70f, 0.70f,  0.0f, 0.55f, 0.00f, 0.00f, 0.40f, 3 },
        { "Dub Reggae Spring",     5, 0.60f, 3.50f, 0.65f, 0.50f, 0.60f, 0.75f,  0.0f, 0.50f, 0.10f, 0.00f, 0.45f, 3 },
        { "Surf Rock Drip",        5, 0.45f, 2.20f, 0.55f, 0.30f, 0.75f, 0.65f,  0.0f, 0.60f, 0.00f, 0.00f, 0.35f, 3 },
        { "Metallic Splash",       5, 0.55f, 3.00f, 0.70f, 0.40f, 0.65f, 0.70f,  0.0f, 0.50f, 0.05f, 0.00f, 0.50f, 3 },

        // ── GoldFoil ──
        { "EMT240 Gold Foil Clean",6, 0.95f, 2.00f, 0.85f, 0.25f, 0.55f, 0.90f,  5.0f, 0.60f, 0.00f, 0.00f, 0.20f, 1 },
        { "Delicate Vocal Shimmer",6, 1.00f, 2.20f, 0.90f, 0.30f, 0.50f, 0.95f,  6.0f, 0.55f, 0.05f, 0.00f, 0.15f, 1 },
        { "Acoustic Warm Foil",    6, 0.90f, 1.80f, 0.80f, 0.20f, 0.60f, 0.85f,  4.0f, 0.65f, 0.15f, 0.05f, 0.22f, 0 },
        { "Airy Top End Foil",     6, 1.05f, 2.40f, 0.88f, 0.35f, 0.45f, 1.00f,  8.0f, 0.50f, 0.00f, 0.00f, 0.18f, 1 },

        // ── InchinDown ──
        { "Guinness Tunnel Infinite", 7, 2.00f, 35.0f, 0.95f, 0.45f, 0.20f, 1.00f, 60.0f, 0.40f, 0.00f, 0.00f, 0.15f, 0 },
        { "Dark Subterranean Abyss",  7, 2.00f, 45.0f, 0.95f, 0.50f, 0.18f, 1.00f, 70.0f, 0.35f, 0.20f, 0.10f, 0.10f, 0 },
        { "Frozen Time Echo",         7, 2.00f, 60.0f, 0.98f, 0.40f, 0.15f, 1.00f, 80.0f, 0.30f, 0.00f, 0.00f, 0.20f, 1 },
        { "Endless Drone Cave",       7, 2.00f, 50.0f, 0.92f, 0.55f, 0.22f, 1.00f, 65.0f, 0.45f, 0.10f, 0.00f, 0.25f, 2 }
    };
}

juce::File PresetBrowser::getFavoritesFile() const
{
    return presetManager.getPresetsFolder().getChildFile("_favorites.txt");
}

void PresetBrowser::loadFavorites()
{
    favorites.clear();
    auto f = getFavoritesFile();
    if (f.existsAsFile()) {
        favorites.addLines(f.loadFileAsString());
        favorites.removeEmptyStrings();
    }
}

void PresetBrowser::saveFavorites()
{
    auto f = getFavoritesFile();
    f.replaceWithText(favorites.joinIntoString("\n"), false, false, "\n");
}

bool PresetBrowser::isFavorite(const juce::String& name) const
{
    return favorites.contains(name);
}

void PresetBrowser::toggleFavorite(const juce::String& name)
{
    if (favorites.contains(name))
        favorites.removeString(name);
    else
        favorites.add(name);

    saveFavorites();
    updateFiles();
}

void PresetBrowser::refresh()
{
    loadFavorites();
    updateSubCategories();
}

void PresetBrowser::setCurrentPreset(const juce::String& name)
{
    currentPresetName = name;
    fileList.repaint();
}

void PresetBrowser::updateSubCategories()
{
    subList.updateContent();
    subList.repaint();
    updateFiles();
}

void PresetBrowser::updateFiles()
{
    filteredItems.clear();

    const juce::String currentCategory = categories[selCat];
    const juce::String currentSub = (selSub >= 0 && selSub < subCategories.size()) ? subCategories[selSub] : "All";
    const juce::String query = searchBox.getText().trim().toLowerCase();

    static const char* algoNames[] = {
        "Room1","Room2","Hall1","Hall2","Plate","Spring","GoldFoil","InchinDown"
    };

    auto matchQuery = [&](const juce::String& text) {
        return query.isEmpty() || text.toLowerCase().contains(query);
    };

    // ── 1. Factory プリセット ──
    if (currentCategory == "Factory" || currentCategory == "Favorite") {
        for (const auto& fp : factoryPresets) {
            juce::String roomName = (fp.algorithmIndex >= 0 && fp.algorithmIndex < 8) ? algoNames[fp.algorithmIndex] : "Other";
            
            if (currentCategory == "Favorite" && !isFavorite(fp.name))
                continue;

            if (currentSub != "All" && roomName != currentSub)
                continue;

            if (!matchQuery(fp.name))
                continue;

            DisplayItem item;
            item.isFactory = true;
            item.factoryDef = fp;
            item.subCategory = roomName;
            item.displayName = fp.name;
            filteredItems.add(item);
        }
    }

    // ── 2. User プリセット ──
    if (currentCategory == "User" || currentCategory == "Favorite") {
        auto userNames = presetManager.getPresetNames();
        for (const auto& un : userNames) {
            if (currentCategory == "Favorite" && !isFavorite(un))
                continue;

            if (!matchQuery(un))
                continue;

            DisplayItem item;
            item.isFactory = false;
            item.userName = un;
            item.subCategory = "User";
            item.displayName = un;
            filteredItems.add(item);
        }
    }

    fileList.updateContent();
    fileList.repaint();
}

void PresetBrowser::paint(juce::Graphics& g)
{
    const auto& theme = laf.getTheme();

    // ソリッドなパネル背景（背後のグラフが透けない完全不透明）
    g.setColour(theme.panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    g.setColour(theme.border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);

    // カラムヘッダー見出し
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 10.0f, juce::Font::bold)));
    g.setColour(theme.textSecondary);

    const int col1W = 130;
    const int col2W = 160;

    g.drawText("CATEGORY", 12, 6, col1W, 16, juce::Justification::centredLeft);
    g.drawText("ROOM TYPE", col1W + 16, 6, col2W, 16, juce::Justification::centredLeft);
    g.drawText("PRESETS", col1W + col2W + 20, 6, 200, 16, juce::Justification::centredLeft);

    // カラム境界線
    g.setColour(theme.separator.withAlpha(0.4f));
    g.drawVerticalLine(col1W + 8, 4.0f, (float)getHeight() - 4.0f);
    g.drawVerticalLine(col1W + col2W + 12, 4.0f, (float)getHeight() - 4.0f);
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced(6);
    area.removeFromTop(20); // ヘッダー見出し領域

    const int col1W = 120;
    const int col2W = 150;

    catList.setBounds(area.removeFromLeft(col1W).reduced(2));
    area.removeFromLeft(8);

    subList.setBounds(area.removeFromLeft(col2W).reduced(2));
    area.removeFromLeft(8);

    // 右カラム (検索バー + プリセットリスト + 閉じるボタン)
    auto topBar = area.removeFromTop(24);
    closeButton.setBounds(topBar.removeFromRight(64).reduced(1));
    searchBox.setBounds(topBar.reduced(1));

    area.removeFromTop(4);
    fileList.setBounds(area.reduced(1));
}

// ─── ListBox Model 実装 ───
int PresetBrowser::CatModel::getNumRows()
{
    return owner ? owner->categories.size() : 0;
}

void PresetBrowser::CatModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (!owner) return;
    const auto& theme = owner->laf.getTheme();
    bool isSel = (row == owner->selCat);

    if (isSel) {
        g.fillAll(theme.primary.withAlpha(0.20f));
        g.setColour(theme.primary);
        g.fillRect(0, 0, 3, h);
    }

    g.setColour(isSel ? theme.textPrimary : theme.textSecondary);
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 12.0f, isSel ? juce::Font::bold : juce::Font::plain)));
    g.drawText(owner->categories[row], 10, 0, w - 16, h, juce::Justification::centredLeft);
}

void PresetBrowser::CatModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (!owner) return;
    owner->selCat = row;
    owner->updateSubCategories();
    owner->catList.repaint();
}

int PresetBrowser::SubModel::getNumRows()
{
    return owner ? owner->subCategories.size() : 0;
}

void PresetBrowser::SubModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (!owner) return;
    const auto& theme = owner->laf.getTheme();
    bool isSel = (row == owner->selSub);

    if (isSel) {
        g.fillAll(theme.primary.withAlpha(0.15f));
        g.setColour(theme.primary);
        g.fillRect(0, 0, 3, h);
    }

    g.setColour(isSel ? theme.textPrimary : theme.textSecondary);
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 11.5f, isSel ? juce::Font::bold : juce::Font::plain)));
    g.drawText(owner->subCategories[row], 10, 0, w - 16, h, juce::Justification::centredLeft);
}

void PresetBrowser::SubModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (!owner) return;
    owner->selSub = row;
    owner->updateFiles();
    owner->subList.repaint();
}

int PresetBrowser::FileModel::getNumRows()
{
    return owner ? owner->filteredItems.size() : 0;
}

void PresetBrowser::FileModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (!owner || row >= owner->filteredItems.size()) return;
    const auto& theme = owner->laf.getTheme();
    const auto& item = owner->filteredItems.getReference(row);

    bool isCurrent = (item.displayName == owner->currentPresetName);

    if (isCurrent) {
        g.fillAll(theme.primary.withAlpha(0.18f));
        g.setColour(theme.primary);
        g.fillRect(0, 0, 3, h);
    }

    // ── ★ お気に入りアイコン (左端) ──
    bool fav = owner->isFavorite(item.displayName);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    if (fav) {
        g.setColour(juce::Colour(0xFFFFB703)); // ゴールド
        g.drawText(juce::CharPointer_UTF8("\xe2\x98\x85"), 6, 0, 18, h, juce::Justification::centred);
    } else {
        g.setColour(theme.textSecondary.withAlpha(0.35f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x98\x86"), 6, 0, 18, h, juce::Justification::centred);
    }

    // ── プリセット名 ──
    g.setColour(isCurrent ? theme.primary : theme.textPrimary);
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 12.0f, isCurrent ? juce::Font::bold : juce::Font::plain)));
    g.drawText(item.displayName, 28, 0, w - 140, h, juce::Justification::centredLeft);

    // ── 右端: RoomType/Category タグ ──
    g.setColour(theme.textSecondary.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 10.0f, juce::Font::plain)));
    g.drawText(item.subCategory, w - 110, 0, 100, h, juce::Justification::centredRight);
}

void PresetBrowser::FileModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (!owner || row >= owner->filteredItems.size()) return;
    const auto& item = owner->filteredItems.getReference(row);

    // 左端の ★ アイコン部分 (x <= 26) をクリックしたときはお気に入りトグル！
    if (e.x <= 26) {
        owner->toggleFavorite(item.displayName);
        return;
    }

    // プリセットのロード
    owner->currentPresetName = item.displayName;
    owner->fileList.repaint();

    if (item.isFactory) {
        if (owner->onLoadFactory)
            owner->onLoadFactory(item.factoryDef);
    } else {
        if (owner->onLoadUser)
            owner->onLoadUser(item.userName);
    }
}
