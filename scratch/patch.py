import os
import re

def patch_file(filepath):
    if not os.path.exists(filepath): return
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # We need to prepend fetching the theme if it's not LookAndFeel.
    # In AmbienceUI.cpp, we can just change it and later manually fix the ones that don't have 'theme' defined.
    # Actually, for AmbienceUI.cpp and PluginEditor.cpp, I'll just write replace rules.
    content = content.replace("AmbienceColors::Surface", "theme.surface")
    content = content.replace("AmbienceColors::Background", "theme.background")
    content = content.replace("AmbienceColors::Border", "theme.border")
    content = content.replace("AmbienceColors::Panel", "theme.panel")
    content = content.replace("AmbienceColors::AccentBlue", "theme.secondary")
    content = content.replace("AmbienceColors::Accent", "theme.primary")
    content = content.replace("AmbienceColors::ArcFill", "theme.primary")
    content = content.replace("AmbienceColors::ArcTrack", "theme.arcTrack")
    content = content.replace("AmbienceColors::Separator", "theme.separator")
    content = content.replace("AmbienceColors::TextSecondary", "theme.textSecondary")
    content = content.replace("AmbienceColors::TextPrimary", "theme.textPrimary")

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)

patch_file('d:/VST_Project/Ambience/Source/GUI/AmbienceUI.cpp')
patch_file('d:/VST_Project/Ambience/Source/GUI/SpectrumAnalyzer.h')
patch_file('d:/VST_Project/Ambience/Source/GUI/DecayCurveViz.h')
patch_file('d:/VST_Project/Ambience/Source/PluginEditor.cpp')
