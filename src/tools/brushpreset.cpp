#include "tools/brushpreset.h"
#include "core/hatchpatterns.h"
#include "core/layer.h"

#include <QUuid>
#include <cmath>

bool BrushPreset::supports(ToolType type) {
    switch (type) {
    case ToolType::Brush:
    case ToolType::Eraser:
    case ToolType::Pencil:
    case ToolType::CloneStamp:
    case ToolType::Recolor:
        return true;
    default:
        return false;
    }
}

BrushPreset BrushPreset::capture(const Tool &tool, const QString &name) {
    BrushPreset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.tool = tool.type();
    p.size = tool.brushSize();
    p.hardness = tool.hardness();
    p.opacity = tool.opacity();
    p.spacing = tool.spacing();
    p.blendMode = tool.blendMode();
    p.fillStyle = tool.fillStyle();
    p.tolerance = tool.tolerance();
    p.antialiased = tool.antialiased();
    p.pressureSensitivity = tool.pressureSensitivity();
    return p;
}

QString BrushPreset::defaultName(const Tool &tool) {
    if (tool.type() == ToolType::Pencil) return tool.name();
    const double size = tool.brushSize();
    const QString width = std::fabs(size - std::round(size)) < 0.05
        ? QString::number(qRound(size))
        : QString::number(size, 'f', 1);
    return QStringLiteral("%1 %2 px").arg(tool.name(), width);
}

void BrushPreset::applyTo(Tool &tool) const {
    tool.setBrushSize(size);
    tool.setHardness(hardness);
    tool.setOpacity(opacity);
    tool.setSpacing(spacing);
    tool.setBlendMode(blendMode);
    tool.setFillStyle(fillStyle);
    tool.setTolerance(tolerance);
    tool.setAntialiased(antialiased);
    tool.setPressureSensitivity(pressureSensitivity);
}

bool BrushPreset::sameSettings(const BrushPreset &o) const {
    return tool == o.tool && std::fabs(size - o.size) < 1e-9 && hardness == o.hardness
        && opacity == o.opacity && spacing == o.spacing && blendMode == o.blendMode
        && fillStyle == o.fillStyle && tolerance == o.tolerance
        && antialiased == o.antialiased && pressureSensitivity == o.pressureSensitivity;
}

QJsonObject BrushPreset::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["tool"] = toolTypeKey(tool);
    o["size"] = size;
    o["hardness"] = hardness;
    o["opacity"] = opacity;
    o["spacing"] = spacing;
    o["blendMode"] = blendMode;
    o["fillStyle"] = fillStyle;
    o["tolerance"] = tolerance;
    o["antialiased"] = antialiased;
    o["pressureSensitivity"] = pressureSensitivity;
    return o;
}

bool BrushPreset::fromJson(const QJsonObject &o, BrushPreset *preset) {
    BrushPreset p;
    p.id = o.value("id").toString();
    p.name = o.value("name").toString();
    if (p.id.isEmpty() || !toolTypeFromKey(o.value("tool").toString(), &p.tool))
        return false;
    p.size = qBound(1.0, o.value("size").toDouble(p.size), 2000.0);
    p.hardness = qBound(0, o.value("hardness").toInt(p.hardness), 100);
    p.opacity = qBound(1, o.value("opacity").toInt(p.opacity), 100);
    p.spacing = qBound(1, o.value("spacing").toInt(p.spacing), 200);
    p.blendMode = qBound(0, o.value("blendMode").toInt(p.blendMode),
                         int(Layer::allBlendModes().size()) - 1);
    p.fillStyle = qBound(0, o.value("fillStyle").toInt(p.fillStyle), Hatch::count() - 1);
    p.tolerance = qBound(0, o.value("tolerance").toInt(p.tolerance), 100);
    p.antialiased = o.value("antialiased").toBool(p.antialiased);
    p.pressureSensitivity = o.value("pressureSensitivity").toBool(p.pressureSensitivity);
    if (preset) *preset = p;
    return true;
}
