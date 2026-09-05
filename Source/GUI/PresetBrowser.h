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
        juce::String description; // 英文説明・タグ（例: "Vocal, Acoustic | Dry, Intimate | Natural room sound"）
        int algorithmIndex{ 0 };  // 0..7
        float roomSize{ 1.0f };
        float decayTime{ 1.5f };
        float diffusion{ 0.7f };
        float modAmount{ 0.25f };
        float modRate{ 0.5f };
        float stereoWidth{ 0.8f };
        float preDelayMs{ 10.0f };
        float erLevel{ 0.6f };
        float hfDamp{ 0.0f };
        float lfAbsorb{ 0.0f };
        float saturation{ 0.0f };
        int   satType{ 0 };       // 0: Warm, 1: Tape, 2: Tube, 3: Hard
        // Dry / Wet
        float dryDB{ 0.0f };
        float wetDB{ -12.0f };
        // Ducking
        float duckAmount{ 0.0f };
        float duckThresh{ -20.0f };
        float duckAttack{ 10.0f };
        float duckRelease{ 200.0f };
        // OutEQ
        int   loEQType{ 0 };      // 0: Off, 1: Cut, 2: Shelf
        float loCut{ 20.0f };
        float loGain{ 0.0f };
        int   hiEQType{ 0 };      // 0: Off, 1: Cut, 2: Shelf
        float hiCut{ 20000.0f };
        float hiGain{ 0.0f };
        // PRO ACOUSTIC 6ノブ
        float scattering{ 0.5f };
        float erCrossoverMs{ 40.0f };
        float lateDensity{ 0.7f };
        float asymmetry{ 0.3f };
        float clarityDB{ 0.0f };
        float airAbsorbScale{ 1.0f };
    };

    std::function<void(const FactoryPresetDef&)> onLoadFactory;
    std::function<void(const juce::String& name)> onLoadUser;
    std::function<void()> onClose;

    PresetBrowser(PresetManager& pm, AmbienceLookAndFeel& laf);
    ~PresetBrowser() override;

    void refresh();
    void setCurrentPreset(const juce::String& name);
    bool loadPresetByName(const juce::String& name);
    bool loadPreviousPreset();
    bool loadNextPreset();

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
    juce::StringArray tagCategories;
    juce::StringArray favorites;

    int selCat{ 0 };
    int selSub{ 0 };
    int selTag{ 0 };
    int subCategoryMode{ 0 }; // 0: Room Type, 1: Acoustic Tags
    juce::String currentPresetName;
    juce::Rectangle<int> infoPanelArea;
    juce::Rectangle<int> modeRoomBtnRect;
    juce::Rectangle<int> modeTagBtnRect;

    void mouseDown(const juce::MouseEvent& e) override;

    // Items
    struct DisplayItem {
        bool isFactory{ true };
        FactoryPresetDef factoryDef;
        juce::String userName;
        juce::String subCategory;
        juce::String displayName;
    };
    juce::Array<DisplayItem> filteredItems;
    const DisplayItem* getSelectedDisplayItem() const;

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
