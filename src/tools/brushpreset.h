#pragma once

#include <QJsonObject>
#include <QString>
#include "tools/tool.h"

// A saved set of brush settings (tool plus width, hardness, opacity ...) that
// the Tools panel shows as a button. Clicking it selects the tool and applies
// the settings. Colors are not part of a preset.
struct BrushPreset {
    QString id;     // stable key, used by the Tools panel to remember its place
    QString name;   // shown in the tooltip
    ToolType tool = ToolType::Brush;
    double size = 10;
    int hardness = 100;
    int opacity = 100;
    int spacing = 15;
    int blendMode = 0;
    int fillStyle = 0;
    int tolerance = 50;
    bool pressureSensitivity = true;
    PressureTarget pressureTarget = PressureTarget::Size;

    // Tools whose settings make a useful preset: the ones that paint with a
    // round tip.
    static bool supports(ToolType type);
    // The settings of `tool` now, under a new id.
    static BrushPreset capture(const Tool &tool, const QString &name);
    // "Paintbrush 1400 px" and the like.
    static QString defaultName(const Tool &tool);

    void applyTo(Tool &tool) const;
    // Same settings, ignoring id and name.
    bool sameSettings(const BrushPreset &other) const;

    QJsonObject toJson() const;
    // False if the object is not a preset this version understands.
    static bool fromJson(const QJsonObject &json, BrushPreset *preset);
};
