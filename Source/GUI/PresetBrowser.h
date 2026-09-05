#pragma once
#include <JuceHeader.h>
#include "AmbienceUI.h"
#include "../PresetManager.h"
#include "../AlgorithmPresets.h"

// ============================================================================
//  PresetBrowser  -  3カラム・オーバーレイ・プリセットブラウザ
//   ・第1階層: カテゴリ (Factory / User / Favorite)
//   ・第2階層: Room Type (All / Room1 / Room2 / Hall1 / Hall2 / Plate / Spring / GoldFoil / InchinDown)
//   ・第3階層: プリセット一覧 (★お気に入り / 検索 / シングルクリック即時読込)
// ============================================================================
class PresetBrowser : public juce::Component
{
public:
    struct FactoryPresetDef {
        juce::String name;
        int algorithmIndex; // 0..7
        float roomSize;
        float decayTime;
        float diffusion;
        float modAmount;
        float modRate;
        float stereoWidth;
        float preDelayMs;
        float erLevel;
        float hfDamp;
        float lfAbsorb;
        float saturation;
        int   satType;
    };

    std::function<void(const FactoryPresetDef&)> onLoadFactory;
    std::function<void(const juce::String& name)> onLoadUser;
    std::function<void()> onClose;

    PresetBrowser(PresetManager& pm, AmbienceLookAndFeel& laf);
    ~PresetBrowser() override;

    void refresh();
    void setCurrentPreset(const juce::String& name);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PresetManager& presetManager;
    AmbienceLookAndFeel& laf;

    // UI Components
    juce::ListBox catList{ "cat", nullptr };
    juce::ListBox subList{ "sub", nullptr };
    juce::ListBox fileList{ "file", nullptr };
    juce::TextEditor searchBox;
    juce::TextButton closeButton{ "CLOSE X" };

    juce::StringArray categories;
    juce::StringArray subCategories;
    juce::StringArray favorites;

    int selCat{ 0 };
    int selSub{ 0 };
    juce::String currentPresetName;

    // Items
    struct DisplayItem {
        bool isFactory{ true };
        FactoryPresetDef factoryDef;
        juce::String userName;
        juce::String subCategory;
        juce::String displayName;
    };
    juce::Array<DisplayItem> filteredItems;

    void initFactoryPresets();
    std::vector<FactoryPresetDef> factoryPresets;

    juce::File getFavoritesFile() const;
    void loadFavorites();
    void saveFavorites();
    bool isFavorite(const juce::String& name) const;
    void toggleFavorite(const juce::String& name);

    void updateSubCategories();
    void updateFiles();

    // ListBox Models
    struct CatModel : juce::ListBoxModel {
        PresetBrowser* owner{ nullptr };
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    } catModel;

    struct SubModel : juce::ListBoxModel {
        PresetBrowser* owner{ nullptr };
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    } subModel;

    struct FileModel : juce::ListBoxModel {
        PresetBrowser* owner{ nullptr };
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    } fileModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
