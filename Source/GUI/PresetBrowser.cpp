#include "PresetBrowser.h"

PresetBrowser::PresetBrowser(PresetManager& pm, AmbienceLookAndFeel& l)
    : presetManager(pm), laf(l)
{
    catModel.owner = this;
    subModel.owner = this;
    fileModel.owner = this;

    categories = { "Factory", "User", "Favorite" };
    subCategories = { "All", "Room1", "Room2", "Hall1", "Hall2", "Plate", "Spring", "GoldFoil", "InchinDown" };
    tagCategories = { "All", "Vocal & Speech", "Drums & Snare", "Acoustic & Guitar", "Piano & Keys", "Strings & Orch", "Brass & Horns", "Bass & LowEnd", "Ambient & Pad", "Creative & FX" };

    catList.setModel(&catModel);
    subList.setModel(&subModel);
    fileList.setModel(&fileModel);

    catList.setRowHeight(32);
    subList.setRowHeight(28);
    fileList.setRowHeight(28);

    for (auto* list : { &catList, &subList, &fileList }) {
        list->setLookAndFeel(&laf);
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

PresetBrowser::~PresetBrowser()
{
    for (auto* list : { &catList, &subList, &fileList })
        list->setLookAndFeel(nullptr);
}

// ============================================================================
//  Factory Presets List (104 Presets = 8 Algorithms x 13 Presets)
// ============================================================================
void PresetBrowser::initFactoryPresets()
{
    factoryPresets.reserve(104);
    factoryPresets = {

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 0: ROOM1 (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "Tight Vocal Isolation Booth", "Vocal, Speech | Intimate, Dry | High-end acoustic isolation booth with zero flutter", 0, 0.65f, 0.18f, 0.70f, 0.08f, 0.30f, 0.72f, 12.0f, 0.78f, 0.25f, 0.30f, 0.06f, 0, 0.0f, -15.0f, 4.5f, -22.0f, 5.0f, 180.0f, 1, 120.0f, 0.0f, 2, 10000.0f, -1.5f, 0.65f, 14.0f, 0.55f, 0.50f, 3.8f, 1.25f },
        { "Modern Snare Ambient Pocket", "Drums, Snare | Punchy, Wide | Adds fat stereo depth and tail pocket without wash", 0, 0.85f, 0.28f, 0.60f, 0.05f, 0.40f, 0.90f, 0.0f, 0.82f, 0.15f, 0.40f, 0.18f, 1, 0.0f, -13.5f, 0.0f, -20.0f, 10.0f, 100.0f, 1, 160.0f, 0.0f, 2, 8000.0f, 1.5f, 0.55f, 16.0f, 0.70f, 0.40f, 3.0f, 0.90f },
        { "Studio Drum Room Tight", "Drums, Percussion | Cohesive, Wooden | Natural tracking room acoustics for glue and weight", 0, 1.10f, 0.42f, 0.72f, 0.10f, 0.35f, 0.95f, 8.0f, 0.74f, 0.30f, 0.15f, 0.12f, 0, 0.0f, -14.0f, 2.0f, -18.0f, 2.0f, 120.0f, 1, 90.0f, 0.0f, 1, 14000.0f, 0.0f, 0.60f, 22.0f, 0.68f, 0.60f, 1.5f, 1.10f },
        { "Intimate Acoustic Guitar", "Acoustic, Guitar | Warm, Pristine | Rich wood reflections with transparent high-end air", 0, 0.80f, 0.32f, 0.65f, 0.15f, 0.45f, 0.85f, 6.0f, 0.70f, 0.10f, 0.25f, 0.08f, 0, 0.0f, -14.5f, 1.5f, -24.0f, 8.0f, 200.0f, 2, 150.0f, -2.0f, 2, 12000.0f, 1.0f, 0.50f, 18.0f, 0.62f, 0.55f, 2.5f, 0.95f },
        { "Close Percussion Studio", "Percussion, Shakers | Crisp, Diffuse | High-diffusion studio space preventing harsh flutter", 0, 0.75f, 0.24f, 0.82f, 0.12f, 0.55f, 0.88f, 4.0f, 0.68f, 0.18f, 0.35f, 0.05f, 1, 0.0f, -16.0f, 0.0f, -20.0f, 10.0f, 100.0f, 1, 180.0f, 0.0f, 2, 9000.0f, -1.0f, 0.75f, 13.0f, 0.75f, 0.45f, 3.2f, 1.05f },
        { "Punchy Brass & Horn Section", "Brass, Horns | Bold, Dynamic | Live studio floor with tube warmth and dynamic ducking", 0, 1.20f, 0.48f, 0.68f, 0.18f, 0.40f, 0.92f, 14.0f, 0.72f, 0.22f, 0.10f, 0.16f, 2, 0.0f, -13.0f, 3.5f, -16.0f, 3.0f, 250.0f, 1, 100.0f, 0.0f, 2, 6500.0f, -1.5f, 0.58f, 24.0f, 0.70f, 0.65f, 1.8f, 1.15f },
        { "Broadcast & Voiceover Booth", "Voiceover, Podcast | Dead, In-Your-Face | Ultra-dry voice room ensuring externalized presence", 0, 0.45f, 0.12f, 0.75f, 0.02f, 0.20f, 0.65f, 2.0f, 0.85f, 0.35f, 0.45f, 0.04f, 0, 0.0f, -17.0f, 5.0f, -26.0f, 4.0f, 150.0f, 1, 110.0f, 0.0f, 1, 11000.0f, 0.0f, 0.60f, 11.0f, 0.45f, 0.35f, 5.2f, 1.40f },
        { "Slapback Electric Guitar Cab", "Electric Guitar, Amp | Slap, Direct | Discrete boundary reflections for amp-in-room punch", 0, 0.70f, 0.22f, 0.38f, 0.06f, 0.50f, 0.80f, 18.0f, 0.85f, 0.12f, 0.30f, 0.22f, 2, 0.0f, -13.0f, 0.0f, -20.0f, 10.0f, 100.0f, 1, 130.0f, 0.0f, 2, 5500.0f, -2.0f, 0.25f, 15.0f, 0.50f, 0.30f, 4.0f, 0.85f },
        { "Warm Electric Piano Space", "Keys, Electric Piano | Mellow, Shimmer | Gentle chorus modulation and rich analog warmth", 0, 0.95f, 0.55f, 0.75f, 0.35f, 0.45f, 0.92f, 10.0f, 0.65f, 0.20f, 0.05f, 0.15f, 0, 0.0f, -13.5f, 1.0f, -22.0f, 15.0f, 300.0f, 2, 200.0f, 1.0f, 2, 7500.0f, -1.0f, 0.52f, 20.0f, 0.72f, 0.50f, 0.5f, 1.00f },
        { "Plucked Strings & Harp Intimacy", "Acoustic, Harp, Strings | Delicate, Airy | Transparent micro-acoustic room with sparkling highs", 0, 0.85f, 0.38f, 0.78f, 0.14f, 0.60f, 0.94f, 8.0f, 0.72f, 0.05f, 0.20f, 0.05f, 0, 0.0f, -14.0f, 1.0f, -24.0f, 6.0f, 180.0f, 1, 100.0f, 0.0f, 2, 11000.0f, 1.5f, 0.62f, 16.0f, 0.65f, 0.60f, 2.8f, 0.80f },
        { "Tape Crushed Slap Chamber", "Creative, Lo-Fi | Saturated, Slap | Heavy tape overdrive combined with vintage slap reflections", 0, 0.60f, 0.40f, 0.35f, 0.28f, 0.75f, 0.75f, 34.0f, 0.85f, 0.45f, 0.35f, 0.65f, 1, 0.0f, -11.0f, 4.0f, -18.0f, 2.0f, 160.0f, 1, 220.0f, 0.0f, 1, 6000.0f, 0.0f, 0.30f, 15.0f, 0.45f, 0.40f, 3.0f, 1.50f },
        { "Micro Concrete Closet", "Creative, FX | Boxy, Resonant | Ultra-cramped concrete booth with sharp metallic boundary nodes", 0, 0.35f, 0.12f, 0.20f, 0.00f, 0.10f, 0.50f, 0.0f, 0.95f, 0.05f, 0.00f, 0.25f, 3, 0.0f, -12.0f, 0.0f, -20.0f, 5.0f, 80.0f, 2, 350.0f, 3.5f, 2, 8500.0f, 2.0f, 0.15f, 10.0f, 0.35f, 0.10f, 6.0f, 0.60f },
        { "Asymmetrical Chaos Room", "Creative, Experimental | Disoriented, Psychoacoustic | Maximum asymmetric boundary diffusion with warping modulation", 0, 1.40f, 0.65f, 0.45f, 0.55f, 1.10f, 1.00f, 25.0f, 0.80f, 0.15f, 0.20f, 0.35f, 2, 0.0f, -12.0f, 6.0f, -20.0f, 1.0f, 220.0f, 1, 75.0f, 0.0f, 2, 10000.0f, 2.5f, 0.85f, 35.0f, 0.80f, 0.95f, -2.0f, 0.85f },

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 1: ROOM2 (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "Natural Oak Drum Room", "Drums, Percussion | Warm, Punchy | Medium oak-floored live room with controlled early reflections and tape warmth", 1, 1.15f, 1.20f, 0.68f, 0.15f, 0.40f, 0.88f, 10.0f, 0.72f, 0.22f, 0.10f, 0.18f, 1, 0.0f, -11.0f, 1.5f, -18.0f, 8.0f, 80.0f, 1, 45.0f, 0.0f, 2, 12000.0f, -1.0f, 0.72f, 24.0f, 0.75f, 0.35f, 1.8f, 1.05f },
        { "Vintage Maple Live Studio", "Band, Rhythm Section | Organic, Cohesive | 70s-style maple tracking room delivering warm acoustic glue and wood resonance", 1, 1.20f, 1.45f, 0.70f, 0.22f, 0.50f, 0.85f, 14.0f, 0.62f, 0.25f, 0.08f, 0.22f, 0, 0.0f, -12.5f, 0.0f, -20.0f, 15.0f, 150.0f, 1, 35.0f, 0.0f, 2, 10000.0f, -0.5f, 0.75f, 28.0f, 0.78f, 0.42f, 1.2f, 1.10f },
        { "Warm Studio Grand Piano", "Acoustic Piano, Keys | Rich, Elegant | Spacious wooden recital studio capturing rich soundboard resonance and pedal blooms", 1, 1.35f, 1.85f, 0.78f, 0.20f, 0.35f, 0.95f, 24.0f, 0.55f, 0.18f, 0.12f, 0.08f, 0, 0.0f, -14.0f, 2.0f, -24.0f, 12.0f, 250.0f, 2, 120.0f, -1.5f, 2, 14000.0f, 0.5f, 0.68f, 35.0f, 0.82f, 0.25f, 1.5f, 0.95f },
        { "Acoustic Ensemble Wood Hall", "Strings, Acoustic Guitars | Open, Natural | Balanced wooden room providing intimate depth and breath for unplugged ensembles", 1, 1.25f, 1.60f, 0.82f, 0.18f, 0.45f, 0.92f, 18.0f, 0.58f, 0.15f, 0.15f, 0.10f, 0, 0.0f, -13.0f, 0.0f, -20.0f, 20.0f, 180.0f, 1, 60.0f, 0.0f, 0, 16000.0f, 0.0f, 0.80f, 30.0f, 0.80f, 0.30f, 2.2f, 1.00f },
        { "Vocal Wood Chamber Lush", "Lead Vocals, Ballads | Intimate, Silky | Deep cedar vocal chamber with transparent sidechain ducking to preserve lyrical articulation", 1, 1.08f, 1.50f, 0.76f, 0.25f, 0.55f, 0.90f, 32.0f, 0.65f, 0.12f, 0.18f, 0.14f, 2, 0.0f, -12.0f, 4.0f, -22.0f, 5.0f, 180.0f, 1, 100.0f, 0.0f, 2, 11000.0f, 1.2f, 0.70f, 26.0f, 0.76f, 0.38f, 2.4f, 0.90f },
        { "Electric Guitar Cab Ambience", "Guitars, Amps | Focused, In-The-Room | Close-to-mid reflections converting direct cabinet captures into dynamic 3D air", 1, 0.82f, 0.85f, 0.62f, 0.10f, 0.60f, 0.80f, 6.0f, 0.80f, 0.30f, 0.05f, 0.25f, 1, 0.0f, -13.5f, 0.0f, -20.0f, 10.0f, 100.0f, 1, 85.0f, 0.0f, 1, 7500.0f, 0.0f, 0.65f, 18.0f, 0.68f, 0.48f, 3.5f, 1.25f },
        { "Dynamic Brass & Horn Room", "Horns, Brass Section | Explosive, Detailed | Highly scattered room absorbing abrasive high transients while augmenting brass body", 1, 1.12f, 1.35f, 0.85f, 0.16f, 0.40f, 0.88f, 16.0f, 0.68f, 0.28f, 0.08f, 0.15f, 2, 0.0f, -11.5f, 2.5f, -16.0f, 6.0f, 120.0f, 1, 70.0f, 0.0f, 2, 8500.0f, -1.5f, 0.85f, 25.0f, 0.84f, 0.40f, 0.8f, 1.20f },
        { "Upright Bass & Percussion Booth", "Acoustic Bass, Congas | Tight, Woody | Controlled low-frequency decay preventing mud and exaggerating rhythmic string slapping", 1, 0.88f, 0.95f, 0.60f, 0.12f, 0.35f, 0.72f, 8.0f, 0.70f, 0.20f, 0.30f, 0.20f, 0, 0.0f, -14.0f, 0.0f, -20.0f, 10.0f, 100.0f, 2, 110.0f, 1.2f, 1, 11000.0f, 0.0f, 0.60f, 20.0f, 0.65f, 0.50f, 3.2f, 1.00f },
        { "Snare Punch Live Chamber", "Snare, Claps | Fat, Explosive | Dense early cluster and saturated tube harmonics dialed for maximum rimshot impact and crack", 1, 1.02f, 1.10f, 0.72f, 0.14f, 0.45f, 0.85f, 12.0f, 0.78f, 0.18f, 0.12f, 0.32f, 2, 0.0f, -10.5f, 1.0f, -14.0f, 3.0f, 70.0f, 1, 90.0f, 0.0f, 2, 10000.0f, 0.8f, 0.76f, 22.0f, 0.82f, 0.32f, 2.0f, 0.95f },
        { "Modern Indie Vocal & Synth Space", "Synths, Indie Vocals | Dimensional, Shimmering | Asymmetric timber room with gentle chorusing modulation for indie and dream-pop productions", 1, 1.22f, 1.70f, 0.80f, 0.38f, 0.70f, 0.94f, 20.0f, 0.60f, 0.15f, 0.10f, 0.16f, 1, 0.0f, -12.0f, 3.0f, -24.0f, 10.0f, 200.0f, 1, 55.0f, 0.0f, 2, 13500.0f, 1.0f, 0.74f, 32.0f, 0.75f, 0.58f, 0.5f, 0.85f },
        { "Overdriven Tube Cellar", "Lo-Fi, Distorted FX | Gritty, Raw | Hot tube-driven basement space with nonlinear harmonic compression and dark wooden reflections", 1, 0.95f, 1.95f, 0.65f, 0.30f, 0.50f, 0.82f, 8.0f, 0.82f, 0.35f, 0.05f, 0.68f, 2, 0.0f, -9.5f, 0.0f, -20.0f, 10.0f, 100.0f, 1, 80.0f, 0.0f, 1, 6500.0f, 0.0f, 0.88f, 22.0f, 0.85f, 0.65f, -2.0f, 1.40f },
        { "Fluttering Reverse Prism", "Experimental, Soundscapes | Swelling, Ethereal | Ghostly backward illusion created by delayed tail buildup, extreme modulation, and acoustic asymmetry", 1, 1.45f, 2.50f, 0.88f, 0.75f, 1.45f, 1.00f, 62.0f, 0.25f, 0.10f, 0.20f, 0.12f, 1, 0.0f, -11.0f, 5.0f, -28.0f, 25.0f, 450.0f, 1, 120.0f, 0.0f, 2, 15000.0f, -2.0f, 0.90f, 55.0f, 0.92f, 0.85f, -4.5f, 0.70f },
        { "Nonlinear Stone Wall Slam", "Industrial Drums, Hard Hits | Massive, Gated | Aggressive hard-clipped wall slap combining high ER reflection density with hyper-fast envelope cutoff", 1, 1.38f, 1.15f, 0.50f, 0.08f, 0.30f, 0.90f, 14.0f, 0.94f, 0.40f, 0.00f, 0.42f, 3, 0.0f, -8.0f, 9.5f, -16.0f, 0.8f, 110.0f, 1, 50.0f, 0.0f, 1, 9000.0f, 0.0f, 0.80f, 68.0f, 0.90f, 0.20f, 4.5f, 1.60f },

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 2: HALL1 (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "Viennese String Hall", "Strings, Orchestral | Rich, Lush, Elegant | Traditional European recital hall tuned for string ensembles with warm wooden resonance", 2, 1.35f, 2.30f, 0.88f, 0.22f, 0.45f, 0.95f, 28.0f, 0.45f, 0.42f, 0.25f, 0.12f, 0, 0.0f, -14.0f, 0.0f, -30.0f, 20.0f, 200.0f, 1, 35.0f, 0.0f, 2, 8500.0f, -1.5f, 0.78f, 34.0f, 0.85f, 0.15f, 1.2f, 1.15f },
        { "Steinway Recital Stage", "Piano, Acoustic | Transparent, Articulate | Intimate recital hall capturing hammer transient clarity and singing sustain", 2, 1.20f, 2.05f, 0.82f, 0.10f, 0.30f, 0.90f, 32.0f, 0.52f, 0.38f, 0.35f, 0.05f, 0, 0.0f, -15.5f, 0.0f, -25.0f, 15.0f, 180.0f, 1, 42.0f, 0.0f, 2, 12000.0f, 0.5f, 0.72f, 30.0f, 0.82f, 0.20f, 2.8f, 1.05f },
        { "Symphonic Concert Hall", "Orchestra, Symphonic | Grand, Deep, Cohesive | 1,500-seat classical concert hall with deep stage perspective and bloom", 2, 1.65f, 2.70f, 0.92f, 0.25f, 0.55f, 1.00f, 40.0f, 0.40f, 0.48f, 0.28f, 0.08f, 1, 0.0f, -16.0f, 0.0f, -28.0f, 25.0f, 300.0f, 1, 30.0f, 0.0f, 2, 7500.0f, -2.0f, 0.85f, 42.0f, 0.92f, 0.25f, 0.5f, 1.35f },
        { "Solo Vocal Grand Hall", "Vocal, Lead | Silky, Focused, Modern | Polished hall space with ducked tail preserving upfront vocal intimacy and air", 2, 1.25f, 2.10f, 0.86f, 0.35f, 0.70f, 0.92f, 48.0f, 0.36f, 0.32f, 0.45f, 0.15f, 2, 0.0f, -13.0f, 3.8f, -24.0f, 8.0f, 280.0f, 1, 120.0f, 0.0f, 2, 10500.0f, 1.8f, 0.75f, 28.0f, 0.80f, 0.12f, 2.2f, 0.85f },
        { "Cedar Acoustic Guitar", "Acoustic Guitar, Folk | Warm, Woody, Intimate | Medium wood-paneled hall giving acoustic guitars sparkling high end and body", 2, 1.05f, 1.75f, 0.80f, 0.18f, 0.60f, 0.85f, 22.0f, 0.58f, 0.40f, 0.42f, 0.10f, 1, 0.0f, -15.0f, 0.0f, -30.0f, 10.0f, 150.0f, 1, 95.0f, 0.0f, 2, 9000.0f, 0.8f, 0.70f, 24.0f, 0.78f, 0.18f, 3.0f, 1.00f },
        { "Majestic Brass Stage", "Brass, Horns | Powerful, Expansive, Noble | Robust acoustic space capable of handling extreme brass spl without harshness", 2, 1.50f, 2.45f, 0.95f, 0.15f, 0.35f, 0.96f, 36.0f, 0.42f, 0.55f, 0.22f, 0.18f, 1, 0.0f, -14.5f, 0.0f, -26.0f, 20.0f, 250.0f, 1, 40.0f, 0.0f, 2, 6500.0f, -2.5f, 0.88f, 38.0f, 0.88f, 0.30f, 0.8f, 1.40f },
        { "Hollywood Scoring Stage", "Film Score, Cinematic | Wide, Controlled, Epic | Premium film scoring stage with pristine localization and monumental depth", 2, 1.40f, 1.85f, 0.85f, 0.20f, 0.40f, 1.00f, 26.0f, 0.50f, 0.36f, 0.32f, 0.08f, 0, 0.0f, -13.5f, 1.5f, -22.0f, 12.0f, 220.0f, 1, 32.0f, 0.0f, 2, 11000.0f, 0.2f, 0.80f, 32.0f, 0.90f, 0.22f, 2.5f, 1.10f },
        { "Baroque Chamber Hall", "Chamber, Early Music | Crisp, Agile, Intimate | Historic stone & wood chamber hall with bright early reflections and fast bloom", 2, 0.95f, 1.60f, 0.78f, 0.12f, 0.50f, 0.88f, 18.0f, 0.62f, 0.28f, 0.38f, 0.04f, 0, 0.0f, -15.0f, 0.0f, -30.0f, 15.0f, 160.0f, 1, 48.0f, 0.0f, 2, 12500.0f, 0.5f, 0.68f, 22.0f, 0.75f, 0.16f, 3.4f, 0.90f },
        { "Cathedral Choral Hall", "Choir, Vocal Ensemble | Heavenly, Expansive, Ethereal | Vaulted ceiling hall designed for sacred choral polyphony and soaring voices", 2, 1.80f, 3.40f, 0.96f, 0.30f, 0.25f, 0.98f, 55.0f, 0.32f, 0.46f, 0.20f, 0.06f, 2, 0.0f, -12.5f, 0.0f, -32.0f, 30.0f, 450.0f, 1, 65.0f, 0.0f, 2, 8000.0f, -1.0f, 0.90f, 48.0f, 0.95f, 0.20f, -1.0f, 1.50f },
        { "Acoustic Jazz Club Hall", "Jazz, Combo | Organic, Snappy, Dimensional | Intimate auditorium acoustics for upright bass, brushed snare, and dry piano", 2, 0.88f, 1.35f, 0.74f, 0.08f, 0.40f, 0.82f, 15.0f, 0.65f, 0.44f, 0.50f, 0.14f, 1, 0.0f, -16.5f, 0.0f, -25.0f, 10.0f, 120.0f, 1, 55.0f, 0.0f, 2, 10000.0f, -1.2f, 0.65f, 18.0f, 0.70f, 0.28f, 4.2f, 1.05f },
        { "Neon Dusk 1984", "Synthwave, Electronic | Pumping, Retro, Huge | Massive gated & ducked 80s hall tailored for synth leads, arps, and gated snare", 2, 1.70f, 3.80f, 0.90f, 0.45f, 1.20f, 1.00f, 12.0f, 0.48f, 0.25f, 0.40f, 0.32f, 1, 0.0f, -10.0f, 12.0f, -18.0f, 1.5f, 180.0f, 1, 140.0f, 0.0f, 2, 14000.0f, 2.0f, 0.82f, 35.0f, 0.94f, 0.08f, -2.0f, 0.60f },
        { "Abyssal Infinite Drone", "Ambient, Drone | Endless, Dark, Shimmering | Colossal subterranean hall with dark absorption and eternal slow-evolving tail", 2, 2.00f, 4.50f, 0.98f, 0.60f, 0.12f, 1.00f, 65.0f, 0.20f, 0.68f, 0.10f, 0.22f, 2, 0.0f, -11.0f, 2.0f, -28.0f, 40.0f, 800.0f, 2, 80.0f, 2.5f, 1, 5500.0f, 0.0f, 0.95f, 60.0f, 1.00f, 0.45f, -5.5f, 2.20f },
        { "Liquid Chorus Hall", "Experimental, Guitar, Keys | Shimmering, Swirling, Detuned | Hyper-modulated medium hall creating lush pitch-dispersed soundscapes", 2, 1.30f, 2.90f, 0.92f, 0.75f, 1.65f, 1.00f, 30.0f, 0.38f, 0.30f, 0.30f, 0.18f, 0, 0.0f, -12.0f, 0.0f, -30.0f, 20.0f, 200.0f, 1, 70.0f, 0.0f, 2, 11000.0f, 1.5f, 0.86f, 36.0f, 0.88f, 0.55f, -1.5f, 0.75f },

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 3: HALL2 (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "Concert Symphony Hall", "Orchestra, Acoustic | Warm, Golden Ratio | Shoebox concert hall with rich midrange and organic bloom", 3, 1.55f, 2.40f, 0.82f, 0.18f, 0.65f, 1.00f, 24.0f, 0.72f, 0.66f, 0.22f, 0.08f, 3, 0.0f, -14.0f, 0.0f, -24.0f, 20.0f, 180.0f, 1, 45.0f, 0.8f, 1, 12500.0f, -1.5f, 0.78f, 45.0f, 0.88f, 0.12f, 0.3f, 0.42f },
        { "Monumental Stone Cathedral", "Cathedral, Choir, Organ | Vast, Majestic | Massive gothic stone sanctuary with towering reverberation", 3, 1.95f, 7.80f, 0.94f, 0.28f, 0.42f, 1.00f, 65.0f, 0.55f, 0.79f, 0.12f, 0.12f, 2, 0.0f, -11.0f, 0.1f, -20.0f, 35.0f, 350.0f, 0, 60.0f, -2.0f, 1, 8500.0f, -4.0f, 0.92f, 35.0f, 0.96f, 0.25f, -0.2f, 0.75f },
        { "Sacred Sanctuary Choir", "Choir, Vocal Ensemble | Heavenly, Pure | Reverent sacred hall tuned to preserve vocal articulation and lush bloom", 3, 1.70f, 4.80f, 0.89f, 0.24f, 0.85f, 1.00f, 42.0f, 0.62f, 0.72f, 0.28f, 0.05f, 1, 0.0f, -13.0f, 0.2f, -22.0f, 15.0f, 240.0f, 0, 90.0f, -1.0f, 1, 11000.0f, -0.5f, 0.85f, 50.0f, 0.90f, 0.15f, 0.1f, 0.50f },
        { "Epic Cinema Stage", "Film Score, Cinematic | Massive, Dynamic | Deep and punchy scoring stage tailored for modern blockbuster orchestral mixes", 3, 1.65f, 3.20f, 0.85f, 0.20f, 0.55f, 1.00f, 32.0f, 0.78f, 0.62f, 0.35f, 0.15f, 1, 0.0f, -12.5f, 0.2f, -18.0f, 10.0f, 160.0f, 0, 40.0f, 0.0f, 1, 14000.0f, 1.0f, 0.82f, 40.0f, 0.92f, 0.18f, 0.4f, 0.38f },
        { "Lush Ballad Vocal", "Lead Vocal, Ballad | Silky, Intimate-to-Wide | Smooth vocal hall with duck-compression for clear front-and-center presence", 3, 1.35f, 2.60f, 0.78f, 0.32f, 1.10f, 1.00f, 48.0f, 0.58f, 0.57f, 0.40f, 0.10f, 2, 0.0f, -15.0f, 0.5f, -24.0f, 8.0f, 200.0f, 0, 140.0f, -2.5f, 1, 12000.0f, 1.5f, 0.75f, 65.0f, 0.84f, 0.08f, 0.5f, 0.30f },
        { "Stadium Arena Rock", "Drums, Guitars, Rock | Cavernous, Punchy | Colossal indoor arena with hard wall reflections and thunderous low-end weight", 3, 1.85f, 4.20f, 0.70f, 0.22f, 0.35f, 1.00f, 75.0f, 0.82f, 0.75f, 0.18f, 0.25f, 1, 0.0f, -13.5f, 0.3f, -16.0f, 12.0f, 220.0f, 0, 55.0f, 1.5f, 1, 7500.0f, -3.0f, 0.68f, 30.0f, 0.78f, 0.28f, 0.2f, 0.65f },
        { "Gothic Cathedral Organ", "Pipe Organ, Sacred | Monumental, Divine | Vast stone vault designed to support sub-bass pedals and shimmering treble pipes", 3, 2.00f, 8.50f, 0.95f, 0.15f, 0.30f, 1.00f, 50.0f, 0.50f, 0.76f, 0.10f, 0.18f, 2, 0.0f, -10.0f, 0.0f, -30.0f, 50.0f, 400.0f, 1, 35.0f, 2.0f, 1, 9000.0f, -2.5f, 0.94f, 28.0f, 0.98f, 0.20f, -0.1f, 0.68f },
        { "Majestic Brass Fanfare", "Brass Section, Horns | Noble, Resonant | Expansive acoustic space engineered to handle brass transients without harsh build-up", 3, 1.50f, 2.80f, 0.86f, 0.16f, 0.50f, 1.00f, 28.0f, 0.68f, 0.60f, 0.30f, 0.12f, 3, 0.0f, -14.0f, 0.2f, -18.0f, 10.0f, 150.0f, 0, 75.0f, 0.0f, 1, 13000.0f, 0.5f, 0.84f, 55.0f, 0.89f, 0.10f, 0.5f, 0.35f },
        { "Opera House Chorus", "Opera, Theatrical Chorus | Articulate, Stately | Horseshoe-tier opera hall balancing lyrical clarity with enveloping acoustic warmth", 3, 1.45f, 2.20f, 0.80f, 0.20f, 0.70f, 1.00f, 22.0f, 0.75f, 0.55f, 0.32f, 0.06f, 3, 0.0f, -13.5f, 0.2f, -22.0f, 14.0f, 180.0f, 0, 80.0f, -0.5f, 1, 13500.0f, 0.0f, 0.80f, 48.0f, 0.86f, 0.14f, 0.6f, 0.32f },
        { "Hollywood Scoring Strings", "Strings, Orchestral | Silky, Cinematic | Broad and opulent hall providing string ensembles with lush, cohesive three-dimensional depth", 3, 1.60f, 3.50f, 0.88f, 0.26f, 0.75f, 1.00f, 36.0f, 0.65f, 0.64f, 0.25f, 0.10f, 1, 0.0f, -13.0f, 0.1f, -24.0f, 25.0f, 260.0f, 0, 65.0f, 0.5f, 1, 12000.0f, -1.0f, 0.86f, 42.0f, 0.94f, 0.16f, 0.3f, 0.40f },
        { "Deep Space Cloud", "Ambient, Soundscape | Nebular, Ethereal | Shimmering multi-voice cloud with deep slow pitch movement and infinite expanse", 3, 1.90f, 9.50f, 0.98f, 0.55f, 1.40f, 1.00f, 85.0f, 0.40f, 0.70f, 0.15f, 0.20f, 2, 0.0f, -9.0f, 0.3f, -26.0f, 30.0f, 450.0f, 0, 100.0f, 0.0f, 1, 10500.0f, 2.0f, 0.95f, 60.0f, 0.99f, 0.35f, -0.4f, 0.25f },
        { "Ethereal Infinite Sustains", "Synth Pad, Drone, Experimental | Endless, Immersive | Limitless sustained tail transforming input signals into timeless ambient drones", 3, 2.00f, 10.00f, 0.92f, 0.38f, 0.90f, 1.00f, 60.0f, 0.45f, 0.74f, 0.10f, 0.15f, 1, 0.0f, -8.5f, 0.4f, -22.0f, 20.0f, 400.0f, 0, 70.0f, -1.0f, 1, 8000.0f, -2.0f, 0.90f, 50.0f, 0.97f, 0.22f, -0.3f, 0.55f },
        { "Abyssal Dark Cinematic", "Dark Ambient, Horror, FX | Ominous, Subterranean | Gloomy cavernous resonance with heavy low-end saturation and muted top end", 3, 1.80f, 6.50f, 0.75f, 0.42f, 0.25f, 1.00f, 95.0f, 0.65f, 0.88f, 0.05f, 0.35f, 1, 0.0f, -11.0f, 0.2f, -18.0f, 15.0f, 300.0f, 1, 30.0f, 3.5f, 0, 4500.0f, -6.0f, 0.70f, 25.0f, 0.85f, 0.40f, -0.1f, 0.85f },

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 4: PLATE (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "EMT 140 Vintage Vocal", "Vocal, Ballad | Warm, Silky, Vintage | Classic German V84 tube plate with smooth airy top and lush decay", 4, 0.85f, 2.20f, 0.92f, 0.18f, 0.45f, 0.88f, 18.0f, 0.45f, 0.15f, 0.10f, 0.18f, 2, 0.0f, -10.0f, 3.5f, -22.0f, 12.0f, 220.0f, 1, 120.0f, 0.0f, 2, 8500.0f, -1.5f, 0.75f, 24.0f, 0.94f, 0.35f, 0.5f, 1.05f },
        { "Tight Snare Plate", "Snare, Drums | Punchy, Dense, Crisp | High-density 0.8s damping plate designed to add explosive body and crack", 4, 0.70f, 0.85f, 0.95f, 0.10f, 0.60f, 0.80f, 0.0f, 0.65f, 0.08f, 0.15f, 0.25f, 2, 0.0f, -8.5f, 0.0f, -20.0f, 10.0f, 200.0f, 1, 160.0f, 0.0f, 2, 12000.0f, 1.2f, 0.85f, 15.0f, 0.96f, 0.28f, 1.8f, 0.90f },
        { "Silky Pop Vocal", "Vocal, Pop, R&B | Bright, Modern, Polished | Air-lifted plate with dynamic ducking for modern radio-ready vocal space", 4, 0.95f, 2.60f, 0.90f, 0.25f, 0.50f, 0.95f, 25.0f, 0.35f, 0.02f, 0.20f, 0.08f, 0, 0.0f, -11.0f, 6.0f, -24.0f, 5.0f, 180.0f, 1, 220.0f, 0.0f, 2, 10000.0f, 2.5f, 0.70f, 22.0f, 0.92f, 0.40f, 0.8f, 0.75f },
        { "Acoustic Guitar Shimmer", "Acoustic, Fingerstyle | Sparkling, Harmonic, Resonant | Resonant steel sheen that blends string transients into organic tail", 4, 0.88f, 1.90f, 0.88f, 0.32f, 0.40f, 0.90f, 14.0f, 0.48f, 0.10f, 0.08f, 0.14f, 1, 0.0f, -12.5f, 1.5f, -26.0f, 15.0f, 250.0f, 1, 150.0f, 0.0f, 2, 6500.0f, 1.5f, 0.80f, 28.0f, 0.89f, 0.32f, 1.2f, 0.95f },
        { "60s Motown Plate", "Vocal, Soul, Snare | Warm, Mid-Forward, Historic | Classic 60s hits style with slap pre-delay and analog tape drive", 4, 0.92f, 2.80f, 0.86f, 0.20f, 0.35f, 0.82f, 55.0f, 0.52f, 0.22f, 0.12f, 0.28f, 1, 0.0f, -9.0f, 2.0f, -20.0f, 10.0f, 240.0f, 1, 130.0f, 0.0f, 1, 9500.0f, 0.0f, 0.72f, 32.0f, 0.88f, 0.45f, -0.5f, 1.20f },
        { "70s Classic Rock Drum Plate", "Drums, Rock, Toms | Massive, Gritty, Explosive | Driven tube plate with punchy dynamics for arena drum overheads", 4, 1.05f, 1.60f, 0.94f, 0.15f, 0.55f, 1.00f, 12.0f, 0.60f, 0.12f, 0.18f, 0.32f, 2, 0.0f, -7.5f, 0.0f, -18.0f, 10.0f, 150.0f, 1, 90.0f, 0.0f, 2, 7000.0f, -1.0f, 0.88f, 20.0f, 0.95f, 0.38f, 0.2f, 1.10f },
        { "Grand Piano Plate", "Piano, Keys | Deep, Expansive, Harmonious | Broad stereophonic plate enveloping grand piano chords with rich sustain", 4, 1.15f, 3.20f, 0.94f, 0.22f, 0.30f, 0.98f, 32.0f, 0.42f, 0.14f, 0.10f, 0.10f, 0, 0.0f, -11.5f, 2.0f, -25.0f, 20.0f, 350.0f, 1, 100.0f, 0.0f, 2, 11000.0f, 0.8f, 0.82f, 35.0f, 0.95f, 0.25f, 1.0f, 0.90f },
        { "Brass & Horns Plate", "Brass, Horns, Sax | Crisp, Punchy, Dynamic | Fast transient response with open top end that maintains brass bite", 4, 0.78f, 1.30f, 0.89f, 0.14f, 0.65f, 0.85f, 10.0f, 0.55f, 0.06f, 0.16f, 0.16f, 1, 0.0f, -9.5f, 1.8f, -19.0f, 8.0f, 160.0f, 1, 180.0f, 0.0f, 2, 13000.0f, 1.0f, 0.84f, 18.0f, 0.91f, 0.30f, 2.0f, 0.85f },
        { "Intimate Vocal Plate", "Vocal, Intimate, Dark | Close, Warm, Textured | Short dark plate placing vocalists right in front of speakers with rich depth", 4, 0.68f, 1.10f, 0.91f, 0.12f, 0.40f, 0.78f, 8.0f, 0.58f, 0.30f, 0.14f, 0.12f, 0, 0.0f, -10.5f, 2.5f, -24.0f, 15.0f, 200.0f, 1, 140.0f, 0.0f, 1, 8000.0f, 0.0f, 0.78f, 20.0f, 0.93f, 0.22f, 1.5f, 1.40f },
        { "Lush String Plate", "Strings, Orchestral, Pad | Lush, Continuous, Ethereal | Wide and fluid plate with slow modulation for seamless string ensembles", 4, 1.20f, 3.80f, 0.96f, 0.38f, 0.28f, 1.00f, 28.0f, 0.38f, 0.12f, 0.06f, 0.10f, 0, 0.0f, -10.0f, 1.0f, -28.0f, 25.0f, 400.0f, 1, 90.0f, 0.0f, 2, 9000.0f, 1.0f, 0.86f, 30.0f, 0.97f, 0.42f, -0.5f, 0.85f },
        { "Overdriven Industrial Plate", "Creative, Industrial, FX | Raw, Distorted, Metallic | Aggressively driven plate with hard saturation and resonant steel clangs", 4, 0.95f, 2.40f, 0.76f, 0.28f, 0.85f, 0.92f, 5.0f, 0.62f, 0.05f, 0.10f, 0.65f, 3, 0.0f, -8.0f, 0.0f, -15.0f, 10.0f, 100.0f, 1, 100.0f, 0.0f, 1, 7500.0f, 0.0f, 0.92f, 16.0f, 0.85f, 0.50f, 2.5f, 0.60f },
        { "Tape Slapback Space Plate", "Creative, Slapback, Dub | Lo-Fi, Flutter, Space | Vintage tape slapback fed into expansive plate with tape flutter modulation", 4, 1.25f, 4.20f, 0.85f, 0.55f, 0.75f, 0.96f, 85.0f, 0.50f, 0.18f, 0.08f, 0.45f, 1, 0.0f, -8.5f, 4.0f, -22.0f, 12.0f, 300.0f, 1, 110.0f, 0.0f, 2, 6000.0f, -2.0f, 0.80f, 40.0f, 0.88f, 0.60f, 0.0f, 1.30f },
        { "Hyper-Metallic Bright Plate", "Creative, Ambient, Synth | Ultra-Bright, Crystalline, Dense | Pure steel plate with zero HF damping and hyper-density shimmering tail", 4, 1.10f, 4.80f, 0.98f, 0.20f, 0.45f, 1.00f, 0.0f, 0.40f, 0.00f, 0.05f, 0.15f, 2, 0.0f, -9.0f, 2.0f, -24.0f, 10.0f, 250.0f, 1, 180.0f, 0.0f, 2, 14000.0f, 3.0f, 0.95f, 12.0f, 0.98f, 0.35f, 1.5f, 0.20f },

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 5: SPRING (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "63 Surf Twin Drip", "Guitar, Surf | Twangy, Boing | Classic 6G15 dual-spring tank with maximum drip, splash, and tube warmth", 5, 0.85f, 2.80f, 0.38f, 0.18f, 0.85f, 0.65f, 12.0f, 0.82f, 0.45f, 0.35f, 0.38f, 2, 0.0f, -8.5f, 0.0f, -24.0f, 20.0f, 150.0f, 0, 140.0f, 0.0f, 1, 5800.0f, 2.5f, 0.85f, 65.0f, 0.42f, 0.30f, 0.8f, 0.20f },
        { "Texas Blues Stage", "Guitar, Blues | Gritty, Woody | Dynamic amp spring tuned for stinging lead guitars and touch sensitivity", 5, 0.70f, 2.10f, 0.52f, 0.12f, 0.60f, 0.50f, 18.0f, 0.65f, 0.55f, 0.48f, 0.32f, 2, 0.0f, -11.0f, 0.1f, -22.0f, 15.0f, 120.0f, 0, 160.0f, 0.0f, 0, 4800.0f, 0.0f, 0.62f, 50.0f, 0.55f, 0.25f, 0.8f, 0.30f },
        { "Kingston Dub Splash", "Drums, Dub | Resonant, Long | High-feedback dub spring with heavy tape drive and resonant high-frequency splash", 5, 1.40f, 5.50f, 0.45f, 0.42f, 0.40f, 0.90f, 35.0f, 0.90f, 0.30f, 0.55f, 0.65f, 1, 0.0f, -4.0f, 0.0f, -20.0f, 10.0f, 200.0f, 0, 180.0f, 0.0f, 2, 3200.0f, 4.5f, 0.92f, 80.0f, 0.48f, 0.45f, 0.7f, 0.20f },
        { "Studio Golden BX20", "Studio, Plate-like | Lush, Silky | Luxurious recreation of the flagship AKG BX20 dual-channel mechanical reverb", 5, 1.25f, 3.80f, 0.88f, 0.22f, 0.30f, 1.00f, 25.0f, 0.50f, 0.38f, 0.25f, 0.15f, 0, 0.0f, -9.0f, 0.0f, -24.0f, 20.0f, 200.0f, 1, 80.0f, 1.5f, 0, 9500.0f, -1.0f, 0.35f, 40.0f, 0.92f, 0.10f, 0.8f, 0.22f },
        { "Warm Vintage Vocal", "Vocal, 70s | Smooth, Intimate | Ducked studio spring tailored for vintage ballads and soul vocals", 5, 1.05f, 3.20f, 0.78f, 0.15f, 0.50f, 1.00f, 30.0f, 0.40f, 0.50f, 0.40f, 0.20f, 2, 0.0f, -10.5f, 0.5f, -26.0f, 25.0f, 350.0f, 0, 220.0f, 0.0f, 1, 7500.0f, -2.0f, 0.40f, 50.0f, 0.80f, 0.15f, 0.9f, 0.35f },
        { "Suitcase Rhodes Spring", "Keys, EP | Warm, Organic | Melodic coil response preserving bell transients and rounding midrange chords", 5, 0.80f, 2.40f, 0.65f, 0.25f, 0.70f, 0.85f, 10.0f, 0.55f, 0.60f, 0.50f, 0.28f, 0, 0.0f, -11.5f, 0.1f, -20.0f, 15.0f, 180.0f, 0, 150.0f, 0.0f, 0, 6000.0f, 0.0f, 0.50f, 45.0f, 0.68f, 0.20f, 0.8f, 0.25f },
        { "Hammond Tone Coil", "Organ, B3 | Swirling, Mechanical | Internal organ tank resonance designed for percussive key clicks and drawbars", 5, 0.95f, 2.60f, 0.58f, 0.35f, 1.20f, 0.75f, 8.0f, 0.70f, 0.48f, 0.38f, 0.35f, 2, 0.0f, -9.5f, 0.0f, -24.0f, 10.0f, 150.0f, 0, 120.0f, 0.0f, 0, 5200.0f, 0.0f, 0.70f, 60.0f, 0.60f, 0.35f, 0.7f, 0.28f },
        { "Motown Snare Crack", "Snare, 60s | Punchy, Splashy | Short and fat mechanical crack providing explosive body to vintage snare tracks", 5, 0.65f, 1.80f, 0.48f, 0.10f, 0.50f, 0.60f, 15.0f, 0.85f, 0.42f, 0.30f, 0.45f, 1, 0.0f, -8.0f, 0.2f, -18.0f, 8.0f, 80.0f, 0, 180.0f, 0.0f, 2, 4200.0f, 3.5f, 0.80f, 70.0f, 0.50f, 0.28f, 0.8f, 0.20f },
        { "Rockabilly Slap Tank", "Slapback, 50s | Tight, Springy | Short decay slap tank delivering authentic Sun Studio bounce and twang", 5, 0.50f, 1.50f, 0.40f, 0.08f, 0.60f, 0.40f, 65.0f, 0.78f, 0.52f, 0.45f, 0.30f, 1, 0.0f, -10.0f, 0.0f, -20.0f, 10.0f, 100.0f, 0, 160.0f, 0.0f, 0, 5500.0f, 0.0f, 0.75f, 65.0f, 0.45f, 0.20f, 0.9f, 0.25f },
        { "Dusty Folk Acoustic", "Acoustic, Roots | Earthy, Dry | Restrained coil coloration that adds vintage patina without metallic harshness", 5, 0.75f, 1.90f, 0.70f, 0.12f, 0.45f, 0.80f, 20.0f, 0.45f, 0.65f, 0.52f, 0.18f, 0, 0.0f, -13.0f, 0.0f, -24.0f, 15.0f, 150.0f, 0, 150.0f, 0.0f, 0, 4500.0f, -1.5f, 0.45f, 50.0f, 0.72f, 0.15f, 0.8f, 0.40f },
        { "Overdriven Trash Spring", "FX, Industrial | Crushed, Chaotic | Slammed input springs pushed into hard clipping for aggressive indie textures", 5, 1.10f, 3.60f, 0.35f, 0.55f, 1.80f, 0.95f, 5.0f, 0.95f, 0.25f, 0.20f, 0.88f, 3, 0.0f, -6.0f, 0.3f, -16.0f, 5.0f, 90.0f, 0, 100.0f, 0.0f, 1, 4000.0f, 5.0f, 0.95f, 90.0f, 0.40f, 0.65f, 0.6f, 0.20f },
        { "Alien Helix Resonance", "FX, Sci-Fi | Warped, Dispersive | Extreme dispersion and deep asymmetry creating metallic pitch-bending vortices", 5, 1.50f, 5.80f, 0.25f, 0.85f, 0.35f, 1.00f, 45.0f, 0.75f, 0.18f, 0.15f, 0.45f, 1, 0.0f, -4.5f, 0.0f, -24.0f, 30.0f, 300.0f, 2, 350.0f, 4.0f, 2, 2800.0f, 6.0f, 1.00f, 100.0f, 0.28f, 0.90f, 0.5f, 0.20f },
        { "AM Radio Lo-Fi Tank", "FX, Vintage | Bandpassed, Grungy | Narrow bandpass and spring flutter evoking 1940s broadcast transmission", 5, 0.45f, 2.00f, 0.60f, 0.40f, 2.00f, 0.05f, 15.0f, 0.60f, 0.70f, 0.60f, 0.55f, 0, 0.0f, -8.0f, 0.0f, -20.0f, 15.0f, 120.0f, 0, 450.0f, 0.0f, 0, 3000.0f, 0.0f, 0.60f, 50.0f, 0.55f, 0.10f, 0.7f, 0.50f },

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 6: GOLDFOIL (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "01 Gold Modern Lead Vocal", "Vocal, Pop | Silky, Dynamic | Luxurious gold foil sheen with smart ducking for upfront mix clarity", 6, 0.95f, 2.20f, 0.92f, 0.20f, 0.65f, 0.90f, 35.0f, 0.50f, 0.12f, 0.15f, 0.12f, 1, 0.0f, -7.5f, 4.5f, -24.0f, 12.0f, 280.0f, 1, 160.0f, 0.0f, 2, 10500.0f, 1.5f, 0.85f, 18.0f, 0.95f, 0.35f, 1.5f, 0.90f },
        { "02 Delicate Fingerstyle Foil", "Acoustic Guitar, Folk | Intimate, Pristine | Responsive micro-plate tailored for fingerpicked strings without body bloom", 6, 0.85f, 1.65f, 0.86f, 0.15f, 0.40f, 0.85f, 18.0f, 0.65f, 0.18f, 0.25f, 0.08f, 0, 0.0f, -9.0f, 2.0f, -28.0f, 15.0f, 220.0f, 2, 220.0f, -2.5f, 2, 8000.0f, 1.0f, 0.80f, 15.0f, 0.90f, 0.40f, 2.0f, 1.05f },
        { "03 Silk String Ensemble Air", "Strings, Orchestral | Lush, Expansive | Wide 3D gold halo providing immense depth and harmonic warmth to strings", 6, 1.25f, 2.85f, 0.95f, 0.32f, 0.45f, 1.00f, 28.0f, 0.42f, 0.08f, 0.10f, 0.15f, 2, 0.0f, -6.0f, 0.0f, -30.0f, 30.0f, 350.0f, 1, 120.0f, 0.0f, 2, 12000.0f, 2.0f, 0.90f, 24.0f, 0.96f, 0.45f, 0.0f, 0.80f },
        { "04 Grand Piano Golden Aura", "Piano, Keys | Majestic, Resonant | Dense noble tail wrapping complex piano chords in a warm, shimmering glow", 6, 1.10f, 2.40f, 0.90f, 0.14f, 0.35f, 0.92f, 24.0f, 0.55f, 0.10f, 0.20f, 0.10f, 0, 0.0f, -8.0f, 1.5f, -22.0f, 20.0f, 300.0f, 2, 180.0f, -1.5f, 1, 16000.0f, 0.0f, 0.82f, 20.0f, 0.92f, 0.30f, 1.0f, 0.95f },
        { "05 Crisp Cymbals & Percussion", "Drums, Cymbals | Ultra-Fast, Sizzle | Instant diffusion tail accentuating cymbal wash without metallic harshness", 6, 0.70f, 1.10f, 0.96f, 0.22f, 1.10f, 0.88f, 2.0f, 0.70f, 0.05f, 0.45f, 0.18f, 1, 0.0f, -11.0f, 0.0f, -20.0f, 5.0f, 100.0f, 1, 450.0f, 0.0f, 2, 9500.0f, 1.8f, 0.92f, 12.0f, 0.98f, 0.50f, 2.5f, 0.75f },
        { "06 Intimate Jazz Club Ballad", "Jazz, Acoustic | Smoky, Organic | Vintage club intimacy with deep tube warmth and unhurried natural decay", 6, 0.90f, 2.10f, 0.82f, 0.18f, 0.38f, 0.80f, 22.0f, 0.62f, 0.22f, 0.10f, 0.24f, 2, 0.0f, -8.5f, 0.0f, -25.0f, 20.0f, 200.0f, 2, 150.0f, 1.0f, 2, 7500.0f, -2.0f, 0.75f, 16.0f, 0.88f, 0.35f, 0.5f, 1.30f },
        { "07 Snare 70s Gold Hit", "Snare, Drums | Punchy, Dense | Classic 1970s dense plate explosion adding explosive weight and crack to snares", 6, 0.80f, 1.35f, 0.94f, 0.12f, 0.80f, 0.85f, 10.0f, 0.75f, 0.15f, 0.35f, 0.26f, 1, 0.0f, -9.5f, 3.0f, -16.0f, 4.0f, 150.0f, 1, 200.0f, 0.0f, 2, 6000.0f, 2.5f, 0.88f, 14.0f, 0.95f, 0.25f, 3.0f, 1.00f },
        { "08 Warm Cello & Upright Bass", "Strings, Bass | Woody, Controlled | Rich low-mid resonance tailored for bowed low strings and upright bass body", 6, 0.88f, 1.80f, 0.85f, 0.10f, 0.30f, 0.75f, 16.0f, 0.58f, 0.30f, 0.08f, 0.16f, 0, 0.0f, -10.0f, 1.5f, -24.0f, 25.0f, 250.0f, 1, 60.0f, 0.0f, 1, 9000.0f, 0.0f, 0.78f, 18.0f, 0.90f, 0.20f, 1.0f, 1.40f },
        { "09 Regal Brass Section Glow", "Brass, Horns | Brilliant, Golden | Noble metallic bloom with high headroom that cushions aggressive brass stabs", 6, 1.15f, 2.30f, 0.91f, 0.25f, 0.50f, 0.95f, 20.0f, 0.52f, 0.14f, 0.20f, 0.20f, 2, 0.0f, -7.0f, 2.5f, -18.0f, 10.0f, 220.0f, 1, 150.0f, 0.0f, 2, 8500.0f, 1.2f, 0.84f, 22.0f, 0.93f, 0.40f, 1.2f, 0.90f },
        { "10 Smooth Velvet R&B Vocal", "Vocal, R&B, Soul | Velvety, Intoxicating | Deep, lush gold foil envelope that envelopes intimate multi-tracked harmonies", 6, 1.05f, 2.60f, 0.94f, 0.28f, 0.70f, 0.96f, 40.0f, 0.45f, 0.08f, 0.12f, 0.15f, 1, 0.0f, -6.5f, 5.0f, -26.0f, 10.0f, 320.0f, 1, 180.0f, 0.0f, 2, 11000.0f, 2.0f, 0.86f, 20.0f, 0.96f, 0.42f, 0.8f, 0.85f },
        { "11 Celestial Crystal Shimmer", "Creative, Ambient | Ethereal, Sparkling | Glistening crystalline gold foil reflections with cascading high-frequency air", 6, 1.50f, 3.90f, 0.98f, 0.58f, 0.85f, 1.00f, 45.0f, 0.35f, 0.00f, 0.40f, 0.22f, 1, 0.0f, -5.0f, 3.5f, -22.0f, 15.0f, 450.0f, 1, 350.0f, 0.0f, 2, 10000.0f, 4.5f, 0.95f, 28.0f, 0.99f, 0.60f, -1.0f, 0.25f },
        { "12 Tube Driver Warm Glow", "Creative, Lo-Fi, Indie | Saturated, Golden | Driven tube preamp coloring dense micro-plate with rich analog harmonics", 6, 0.80f, 2.50f, 0.78f, 0.35f, 0.40f, 0.88f, 8.0f, 0.68f, 0.25f, 0.05f, 0.55f, 2, 0.0f, -7.0f, 2.0f, -20.0f, 8.0f, 180.0f, 2, 250.0f, 2.0f, 1, 6500.0f, 0.0f, 0.70f, 14.0f, 0.85f, 0.45f, 1.5f, 1.50f },
        { "13 Micro-Space Gold Resonator", "Creative, FX, Perc | Metallic, Holographic | Ultra-tight gold membrane resonation creating microscopic acoustic dimensions", 6, 0.35f, 0.85f, 0.65f, 0.42f, 1.45f, 0.95f, 0.0f, 0.88f, 0.08f, 0.20f, 0.30f, 3, 0.0f, -8.0f, 0.0f, -20.0f, 10.0f, 100.0f, 1, 100.0f, 0.0f, 2, 5000.0f, 3.0f, 0.60f, 10.0f, 0.70f, 0.75f, 4.0f, 0.60f },

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        //  ALGORITHM 7: INCHINDOWN (13 Presets)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        { "Guinness Subterranean 112s", "Cinematic, Documentary | Colossal, Archival | Authentic Guinness World Record 112-second decay in the historic Inchindown oil depot", 7, 2.00f, 112.00f, 0.92f, 0.35f, 0.12f, 1.00f, 75.0f, 0.45f, 0.15f, 0.00f, 0.10f, 0, 0.0f, -9.0f, 0.0f, -20.0f, 20.0f, 200.0f, 1, 28.0f, 0.0f, 2, 8000.0f, -2.0f, 0.85f, 80.0f, 0.88f, 0.85f, -4.5f, 1.40f },
        { "Cinematic Drone Horizon", "Cinematic, Ambient | Infinite, Ethereal | Majestic continuous drone generator turning simple synth chords into galactic beds", 7, 1.90f, 90.00f, 0.98f, 0.45f, 0.18f, 1.00f, 50.0f, 0.30f, 0.10f, 0.05f, 0.15f, 1, 0.0f, -6.0f, 6.0f, -24.0f, 15.0f, 600.0f, 1, 40.0f, 0.0f, 0, 16000.0f, 0.0f, 0.90f, 70.0f, 0.95f, 0.60f, -3.0f, 1.10f },
        { "Singing Bowl Meditation", "Meditation, Healing | Sacred, Pure | Extended harmonic resonance with ultra-clean reflections tailored for singing bowls and chimes", 7, 1.75f, 65.00f, 0.95f, 0.22f, 0.15f, 0.95f, 35.0f, 0.50f, 0.25f, 0.10f, 0.05f, 1, 0.0f, -8.0f, 0.0f, -20.0f, 10.0f, 150.0f, 1, 60.0f, 0.0f, 2, 7000.0f, -1.5f, 0.75f, 55.0f, 0.90f, 0.40f, 2.0f, 1.20f },
        { "Dark Post-Rock Cavern", "Post-Rock, Shoegaze | Massive, Moody | Deep granite underground decay for atmospheric swell guitars and slow heavy basslines", 7, 1.85f, 45.00f, 0.88f, 0.50f, 0.25f, 1.00f, 60.0f, 0.40f, 0.30f, 0.20f, 0.25f, 0, 0.0f, -8.0f, 4.0f, -22.0f, 10.0f, 350.0f, 1, 75.0f, 0.0f, 2, 5500.0f, -3.0f, 0.82f, 65.0f, 0.85f, 0.78f, -1.5f, 1.50f },
        { "Slow-Tempo Soundscape", "Soundscape, Minimal | Vast, Contemplative | Expansive breathing reverb space with gentle dynamic ducking for low-BPM modern compositions", 7, 1.95f, 55.00f, 0.90f, 0.40f, 0.20f, 1.00f, 45.0f, 0.35f, 0.18f, 0.12f, 0.12f, 1, 0.0f, -7.0f, 9.0f, -22.0f, 12.0f, 450.0f, 1, 50.0f, 0.0f, 2, 9000.0f, -1.0f, 0.88f, 60.0f, 0.92f, 0.65f, 0.0f, 1.30f },
        { "Subterranean Cathedral", "Sacred, Choral | Heavenly, Monumental | Cavernous sacred chamber merging ancient cathedral majesty with endless underground echoes", 7, 2.00f, 75.00f, 0.94f, 0.30f, 0.14f, 1.00f, 70.0f, 0.45f, 0.12f, 0.08f, 0.08f, 0, 0.0f, -9.0f, 0.0f, -20.0f, 20.0f, 200.0f, 1, 45.0f, 0.0f, 2, 8500.0f, -2.5f, 0.85f, 70.0f, 0.90f, 0.50f, 1.5f, 1.25f },
        { "Tidal Slow Strings", "Orchestral, Film Score | Lush, Sweeping | Ocean-like slow swelling string chamber with rich low-mid body and high-frequency air damping", 7, 1.80f, 38.00f, 0.92f, 0.38f, 0.16f, 0.95f, 85.0f, 0.38f, 0.22f, 0.15f, 0.10f, 2, 0.0f, -10.0f, 3.0f, -24.0f, 25.0f, 500.0f, 1, 55.0f, 0.0f, 2, 6500.0f, -2.0f, 0.80f, 75.0f, 0.86f, 0.70f, -1.0f, 1.60f },
        { "Modular Drone Architect", "Electronic, Modular | Hypnotic, Deep | Slow-moving pitch modulation and dense multi-tap reflections for evolving Eurorack drones", 7, 1.95f, 80.00f, 0.96f, 0.58f, 0.08f, 1.00f, 40.0f, 0.42f, 0.14f, 0.00f, 0.22f, 2, 0.0f, -6.0f, 0.0f, -20.0f, 10.0f, 200.0f, 2, 120.0f, 1.5f, 1, 14000.0f, 0.0f, 0.92f, 65.0f, 0.94f, 0.82f, -2.5f, 1.05f },
        { "Granite SFX Resonator", "SFX, Game Audio | Industrial, Ominous | Massive granite shockwave resonance for explosions, blast impacts, and mechanical hits", 7, 2.00f, 30.00f, 0.85f, 0.25f, 0.30f, 1.00f, 15.0f, 0.70f, 0.18f, 0.00f, 0.35f, 1, -2.0f, -4.0f, 0.0f, -20.0f, 10.0f, 150.0f, 2, 90.0f, 2.5f, 2, 4500.0f, -3.5f, 0.92f, 85.0f, 0.88f, 0.90f, 1.0f, 1.70f },
        { "Dark Ambient Piano Sanctuary", "Neo-Classical, Solo Piano | Nostalgic, Distant | Poignant felt piano space balancing intimacy of the hammers with infinite subterranean tail", 7, 1.70f, 42.00f, 0.93f, 0.32f, 0.15f, 0.90f, 45.0f, 0.35f, 0.28f, 0.18f, 0.12f, 0, 0.0f, -9.0f, 5.0f, -24.0f, 15.0f, 400.0f, 1, 50.0f, 0.0f, 1, 11000.0f, 0.0f, 0.80f, 55.0f, 0.90f, 0.60f, -1.5f, 1.65f },
        { "Frozen Time Crystal", "Experimental, Glitch | Suspended, Zero-Gravity | Endless crystalline temporal freeze holding single moments in an infinite suspended state", 7, 2.00f, 120.00f, 1.00f, 0.20f, 0.05f, 1.00f, 10.0f, 0.25f, 0.00f, 0.10f, 0.18f, 1, 0.0f, -4.0f, 0.0f, -20.0f, 10.0f, 200.0f, 1, 80.0f, 0.0f, 2, 6000.0f, 3.0f, 0.95f, 50.0f, 1.00f, 0.30f, -4.0f, 0.20f },
        { "Black Hole Singularity", "Dark Ambient, Experimental | Void, Monolithic | Ultra-dense gravity well absorbing all high frequencies into an ominous sub-bass singularity", 7, 2.00f, 105.00f, 0.95f, 0.65f, 0.07f, 1.00f, 100.0f, 0.20f, 0.85f, 0.00f, 0.30f, 2, 0.0f, -5.0f, 0.0f, -20.0f, 10.0f, 200.0f, 2, 90.0f, 4.0f, 1, 1800.0f, 0.0f, 0.85f, 90.0f, 0.95f, 0.95f, -6.0f, 2.50f },
        { "Distortion Catacomb", "Industrial, Noise | Savage, Demonic | Crushed concrete and overdriven tube saturation burning inside a subterranean nuclear bunker", 7, 1.90f, 50.00f, 0.82f, 0.45f, 0.40f, 1.00f, 20.0f, 0.65f, 0.20f, 0.05f, 0.85f, 3, 0.0f, -4.0f, 0.0f, -20.0f, 10.0f, 200.0f, 1, 65.0f, 0.0f, 2, 3500.0f, 2.5f, 0.95f, 80.0f, 0.70f, 0.85f, 2.0f, 1.20f },
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

const PresetBrowser::DisplayItem* PresetBrowser::getSelectedDisplayItem() const
{
    if (currentPresetName.isNotEmpty()) {
        for (const auto& item : filteredItems) {
            if (item.displayName == currentPresetName)
                return &item;
        }
    }
    if (!filteredItems.isEmpty())
        return &filteredItems.getReference(0);
    return nullptr;
}

void PresetBrowser::setCurrentPreset(const juce::String& name)
{
    currentPresetName = name;
    for (int i = 0; i < filteredItems.size(); ++i) {
        if (filteredItems.getReference(i).displayName == currentPresetName) {
            fileList.selectRow(i);
            break;
        }
    }
    fileList.repaint();
    repaint();
}

bool PresetBrowser::loadPresetByName(const juce::String& name)
{
    for (const auto& def : factoryPresets) {
        if (def.name == name) {
            currentPresetName = name;
            setCurrentPreset(name);
            if (onLoadFactory)
                onLoadFactory(def);
            return true;
        }
    }
    for (const auto& uname : presetManager.getPresetNames()) {
        if (uname == name) {
            currentPresetName = name;
            setCurrentPreset(name);
            if (onLoadUser)
                onLoadUser(name);
            return true;
        }
    }
    return false;
}

bool PresetBrowser::loadPreviousPreset()
{
    if (filteredItems.isEmpty())
        return false;

    int curIdx = -1;
    for (int i = 0; i < filteredItems.size(); ++i) {
        if (filteredItems.getReference(i).displayName == currentPresetName) {
            curIdx = i;
            break;
        }
    }

    int nextIdx = 0;
    if (curIdx < 0) {
        nextIdx = filteredItems.size() - 1;
    } else {
        nextIdx = (curIdx - 1 + filteredItems.size()) % filteredItems.size();
    }

    const auto& item = filteredItems.getReference(nextIdx);
    currentPresetName = item.displayName;
    fileList.selectRow(nextIdx);
    fileList.repaint();
    repaint();

    if (item.isFactory) {
        if (onLoadFactory)
            onLoadFactory(item.factoryDef);
    } else {
        if (onLoadUser)
            onLoadUser(item.userName);
    }
    return true;
}

bool PresetBrowser::loadNextPreset()
{
    if (filteredItems.isEmpty())
        return false;

    int curIdx = -1;
    for (int i = 0; i < filteredItems.size(); ++i) {
        if (filteredItems.getReference(i).displayName == currentPresetName) {
            curIdx = i;
            break;
        }
    }

    int nextIdx = 0;
    if (curIdx < 0) {
        nextIdx = 0;
    } else {
        nextIdx = (curIdx + 1) % filteredItems.size();
    }

    const auto& item = filteredItems.getReference(nextIdx);
    currentPresetName = item.displayName;
    fileList.selectRow(nextIdx);
    fileList.repaint();
    repaint();

    if (item.isFactory) {
        if (onLoadFactory)
            onLoadFactory(item.factoryDef);
    } else {
        if (onLoadUser)
            onLoadUser(item.userName);
    }
    return true;
}

void PresetBrowser::updateSubCategories()
{
    subList.updateContent();
    subList.repaint();
    updateFiles();
}

void PresetBrowser::mouseDown(const juce::MouseEvent& e)
{
    if (modeRoomBtnRect.contains(e.getPosition())) {
        if (subCategoryMode != 0) {
            subCategoryMode = 0;
            subList.updateContent();
            updateFiles();
            repaint();
        }
    }
    else if (modeTagBtnRect.contains(e.getPosition())) {
        if (subCategoryMode != 1) {
            subCategoryMode = 1;
            subList.updateContent();
            updateFiles();
            repaint();
        }
    }
}

void PresetBrowser::updateFiles()
{
    filteredItems.clear();

    const juce::String currentCategory = categories[selCat];
    const juce::String query = searchBox.getText().trim().toLowerCase();

    static const char* algoNames[] = {
        "Room1","Room2","Hall1","Hall2","Plate","Spring","GoldFoil","InchinDown"
    };

    auto matchQuery = [&](const juce::String& text) {
        return query.isEmpty() || text.toLowerCase().contains(query);
    };

    auto matchTag = [&](const juce::String& name, const juce::String& desc, int tagIndex) -> bool {
        if (tagIndex == 0) return true; // "All"
        juce::String target = (name + " " + desc).toLowerCase();
        switch (tagIndex) {
            case 1: // "Vocal & Speech"
                return target.contains("vocal") || target.contains("speech") || target.contains("voiceover")
                    || target.contains("choir") || target.contains("choral") || target.contains("opera");
            case 2: // "Drums & Snare"
                return target.contains("drum") || target.contains("snare") || target.contains("percussion")
                    || target.contains("cymbal") || target.contains("clap") || target.contains("rimshot") || target.contains("tom");
            case 3: // "Acoustic & Guitar"
                return target.contains("guitar") || target.contains("acoustic") || target.contains("harp")
                    || target.contains("folk") || target.contains("fingerstyle");
            case 4: // "Piano & Keys"
                return target.contains("piano") || target.contains("keys") || target.contains("rhodes")
                    || target.contains("organ") || target.contains("soundboard");
            case 5: // "Strings & Orch"
                return target.contains("string") || target.contains("orchestral") || target.contains("cello")
                    || target.contains("symphon") || target.contains("chamber") || target.contains("score") || target.contains("scoring");
            case 6: // "Brass & Horns"
                return target.contains("brass") || target.contains("horn") || target.contains("fanfare");
            case 7: // "Bass & LowEnd"
                return target.contains("bass") || target.contains("upright");
            case 8: // "Ambient & Pad"
                return target.contains("ambient") || target.contains("drone") || target.contains("soundscape")
                    || target.contains("pad") || target.contains("ethereal") || target.contains("cathedral")
                    || target.contains("abyss") || target.contains("space") || target.contains("infinite") || target.contains("cloud");
            case 9: // "Creative & FX"
                return target.contains("creative") || target.contains("fx") || target.contains("distort")
                    || target.contains("industrial") || target.contains("lo-fi") || target.contains("glitch")
                    || target.contains("alien") || target.contains("slam") || target.contains("chaos")
                    || target.contains("tape crushed") || target.contains("concrete closet");
            default:
                return true;
        }
    };

    // ── 1. Factory プリセット ──
    if (currentCategory == "Factory" || currentCategory == "Favorite") {
        for (const auto& fp : factoryPresets) {
            juce::String roomName = (fp.algorithmIndex >= 0 && fp.algorithmIndex < 8) ? algoNames[fp.algorithmIndex] : "Other";
            
            if (currentCategory == "Favorite" && !isFavorite(fp.name))
                continue;

            if (subCategoryMode == 0) {
                const juce::String currentSub = (selSub >= 0 && selSub < subCategories.size()) ? subCategories[selSub] : "All";
                if (currentSub != "All" && roomName != currentSub)
                    continue;
            } else {
                if (!matchTag(fp.name, fp.description, selTag))
                    continue;
            }

            if (!matchQuery(fp.name) && !matchQuery(fp.description))
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

            if (subCategoryMode == 0) {
                const juce::String currentSub = (selSub >= 0 && selSub < subCategories.size()) ? subCategories[selSub] : "All";
                if (currentSub != "All" && currentSub != "User")
                    continue;
            }

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
    repaint();
}

void PresetBrowser::paint(juce::Graphics& g)
{
    const auto& theme = laf.getTheme();

    // ソリッドなパネル背景
    g.setColour(theme.panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    g.setColour(theme.border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);

    const int col1W = 110;
    const int col2W = 135;
    const int listW = 370;

    const int x1 = 12;
    const int x2 = x1 + col1W + 8;
    const int x3 = x2 + col2W + 8;
    const int x4 = x3 + listW + 8;

    // カラム1見出し: CATEGORY
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 10.0f, juce::Font::bold)));
    g.setColour(theme.textSecondary);
    g.drawText("CATEGORY", x1, 6, col1W, 16, juce::Justification::centredLeft);

    // カラム2見出し: [ ROOM ] [ TAGS ] ハイブリッド切替ピルトグル
    modeRoomBtnRect = juce::Rectangle<int>(x2, 4, 62, 18);
    modeTagBtnRect  = juce::Rectangle<int>(x2 + 66, 4, 62, 18);

    bool isRoom = (subCategoryMode == 0);
    g.setColour(isRoom ? theme.primary.withAlpha(0.25f) : theme.surface.withAlpha(0.5f));
    g.fillRoundedRectangle(modeRoomBtnRect.toFloat(), 3.0f);
    g.setColour(isRoom ? theme.primary : theme.border.withAlpha(0.6f));
    g.drawRoundedRectangle(modeRoomBtnRect.toFloat().reduced(0.5f), 3.0f, 1.0f);
    g.setColour(isRoom ? theme.primary : theme.textSecondary);
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.5f, juce::Font::bold)));
    g.drawText("ROOM", modeRoomBtnRect, juce::Justification::centred);

    bool isTag = (subCategoryMode == 1);
    g.setColour(isTag ? theme.primary.withAlpha(0.25f) : theme.surface.withAlpha(0.5f));
    g.fillRoundedRectangle(modeTagBtnRect.toFloat(), 3.0f);
    g.setColour(isTag ? theme.primary : theme.border.withAlpha(0.6f));
    g.drawRoundedRectangle(modeTagBtnRect.toFloat().reduced(0.5f), 3.0f, 1.0f);
    g.setColour(isTag ? theme.primary : theme.textSecondary);
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.5f, juce::Font::bold)));
    g.drawText("TAGS", modeTagBtnRect, juce::Justification::centred);

    // カラム3見出し: PRESETS
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 10.0f, juce::Font::bold)));
    g.setColour(theme.textSecondary);
    g.drawText("PRESETS", x3, 6, listW, 16, juce::Justification::centredLeft);

    // カラム4見出し: INFO & ACOUSTIC TAGS
    g.drawText("INFO & ACOUSTIC TAGS", x4, 6, getWidth() - x4 - 12, 16, juce::Justification::centredLeft);

    // カラム境界線
    g.setColour(theme.separator.withAlpha(0.4f));
    g.drawVerticalLine(x2 - 4, 4.0f, (float)getHeight() - 4.0f);
    g.drawVerticalLine(x3 - 4, 4.0f, (float)getHeight() - 4.0f);
    g.drawVerticalLine(x4 - 4, 4.0f, (float)getHeight() - 4.0f);

    // ── 右側 1/3: 選択中プリセットの英文説明・タグパネル描画 ──
    if (!infoPanelArea.isEmpty()) {
        auto r = infoPanelArea.toFloat();
        g.setColour(theme.surface.withAlpha(0.35f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(theme.border.withAlpha(0.40f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);

        const auto* item = getSelectedDisplayItem();
        if (item != nullptr) {
            auto inner = infoPanelArea.reduced(8);

            // 1. プリセット名
            g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 12.5f, juce::Font::bold)));
            g.setColour(theme.primary);
            g.drawFittedText(item->displayName, inner.getX(), inner.getY(), inner.getWidth(), 18, juce::Justification::topLeft, 1);

            // 2. アルゴリズム / カテゴリバッジ
            juce::String badgeText = item->isFactory ? ("ALGO: " + item->subCategory) : "USER PRESET";
            g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.0f, juce::Font::bold)));
            int badgeW = (int)g.getCurrentFont().getStringWidthFloat(badgeText) + 12;
            juce::Rectangle<int> badgeRect(inner.getX(), inner.getY() + 20, badgeW, 15);
            g.setColour(theme.primary.withAlpha(0.20f));
            g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
            g.setColour(theme.primary);
            g.drawText(badgeText, badgeRect, juce::Justification::centred);

            // 3. 英文説明文 (Description / Tags)
            juce::String desc = item->isFactory ? item->factoryDef.description : "Custom user created preset.";
            if (desc.isNotEmpty()) {
                auto tokens = juce::StringArray::fromTokens(desc, "|", "");
                int curY = inner.getY() + 38;

                if (tokens.size() >= 2) {
                    // TAGS
                    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.5f, juce::Font::bold)));
                    g.setColour(theme.textPrimary);
                    g.drawText(tokens[0].trim(), inner.getX(), curY, inner.getWidth(), 14, juce::Justification::centredLeft);
                    curY += 15;

                    // CHARACTER
                    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.0f, juce::Font::plain)));
                    g.setColour(juce::Colour(0xFFFFB703)); // ゴールド
                    g.drawText(tokens[1].trim(), inner.getX(), curY, inner.getWidth(), 14, juce::Justification::centredLeft);
                    curY += 15;

                    // DESCRIPTION TEXT
                    if (tokens.size() >= 3) {
                        g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.0f, juce::Font::plain)));
                        g.setColour(theme.textSecondary);
                        g.drawFittedText(tokens[2].trim(), inner.getX(), curY, inner.getWidth(), inner.getBottom() - curY - 20, juce::Justification::topLeft, 4);
                    }
                } else {
                    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.0f, juce::Font::plain)));
                    g.setColour(theme.textSecondary);
                    g.drawFittedText(desc, inner.getX(), curY, inner.getWidth(), inner.getBottom() - curY - 20, juce::Justification::topLeft, 5);
                }
            }

            // 4. パラメータサマリー行 (下端)
            if (item->isFactory) {
                const auto& fd = item->factoryDef;
                juce::String summary = juce::String::formatted("Decay: %.1fs | Size: %.2fx | Pre: %.0fms | Wet: %.0fdB",
                    fd.decayTime, fd.roomSize, fd.preDelayMs, fd.wetDB);
                g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::bold)));
                g.setColour(theme.textSecondary.withAlpha(0.7f));
                g.drawText(summary, inner.getX(), inner.getBottom() - 15, inner.getWidth(), 15, juce::Justification::centredLeft);
            }
        }
    }
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced(6);
    area.removeFromTop(24); // ヘッダー見出し領域（ROOM/TAGSボタン配置用）

    const int col1W = 110;
    const int col2W = 135;
    const int listW = 370;

    catList.setBounds(area.removeFromLeft(col1W).reduced(2));
    area.removeFromLeft(8);

    subList.setBounds(area.removeFromLeft(col2W).reduced(2));
    area.removeFromLeft(8);

    // プリセットリストカラム (検索バー + 閉じるボタン + リスト)
    auto listCol = area.removeFromLeft(listW);
    auto topBar = listCol.removeFromTop(24);
    closeButton.setBounds(topBar.removeFromRight(60).reduced(1));
    searchBox.setBounds(topBar.reduced(1));
    listCol.removeFromTop(4);
    fileList.setBounds(listCol.reduced(1));

    area.removeFromLeft(8);

    // 右側: INFO & ACOUSTIC TAGS パネル
    infoPanelArea = area.reduced(2);
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
    owner->repaint();
}

int PresetBrowser::SubModel::getNumRows()
{
    if (!owner) return 0;
    return (owner->subCategoryMode == 0) ? owner->subCategories.size() : owner->tagCategories.size();
}

void PresetBrowser::SubModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (!owner) return;
    const auto& theme = owner->laf.getTheme();
    bool isSel = (owner->subCategoryMode == 0) ? (row == owner->selSub) : (row == owner->selTag);

    if (isSel) {
        g.fillAll(theme.primary.withAlpha(0.18f));
        g.setColour(theme.primary);
        g.fillRect(0, 0, 3, h);
    }

    juce::String text = (owner->subCategoryMode == 0) ? owner->subCategories[row] : owner->tagCategories[row];
    g.setColour(isSel ? theme.textPrimary : theme.textSecondary);
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 11.5f, isSel ? juce::Font::bold : juce::Font::plain)));
    g.drawText(text, 10, 0, w - 16, h, juce::Justification::centredLeft);
}

void PresetBrowser::SubModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (!owner) return;
    if (owner->subCategoryMode == 0)
        owner->selSub = row;
    else
        owner->selTag = row;
    owner->updateFiles();
    owner->subList.repaint();
    owner->repaint();
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
    g.drawText(item.displayName, 28, 0, w - 120, h, juce::Justification::centredLeft);

    // ── 右端: RoomType/Category タグ ──
    g.setColour(theme.textSecondary.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 10.0f, juce::Font::plain)));
    g.drawText(item.subCategory, w - 95, 0, 85, h, juce::Justification::centredRight);
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
    owner->repaint();

    if (item.isFactory) {
        if (owner->onLoadFactory)
            owner->onLoadFactory(item.factoryDef);
    } else {
        if (owner->onLoadUser)
            owner->onLoadUser(item.userName);
    }
}
