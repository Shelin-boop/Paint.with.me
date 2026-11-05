#include "tools/toolregistry.h"

#include "tools/brushtool.h"
#include "tools/clonestamptool.h"
#include "tools/colorpickertool.h"
#include "tools/erasertool.h"
#include "tools/filltool.h"
#include "tools/gradienttool.h"
#include "tools/lassotool.h"
#include "tools/linetool.h"
#include "tools/magicwandtool.h"
#include "tools/moveselectiontool.h"
#include "tools/movetool.h"
#include "tools/pantool.h"
#include "tools/penciltool.h"
#include "tools/perspectivetool.h"
#include "tools/recolortool.h"
#include "tools/selectiontool.h"
#include "tools/shapetool.h"
#include "tools/texttool.h"
#include "tools/zoomtool.h"

const std::vector<ToolInfo> &toolInfos() {
    // Panel slots follow Paint.NET's Tools window: the four selection tools
    // down the left column, Move / Move Selection / Zoom / Pan down the right,
    // then pairs. Perspective takes the free slot next to Shapes.
    static const std::vector<ToolInfo> tools = {
        {ToolType::RectSelection, "rectangle-select", "Rectangle Select", "S", 0, 0,
         "Selects a rectangular area to edit.",
         "Click and drag to draw a selection. Hold Shift to constrain."},
        {ToolType::LassoSelection, "lasso-select", "Lasso Select", "S", 1, 0,
         "Selects a freehand area by drawing around it.",
         "Click and drag to draw a freehand selection."},
        {ToolType::EllipseSelection, "ellipse-select", "Ellipse Select", "S", 2, 0,
         "Selects an oval area to edit.",
         "Click and drag to draw a selection. Hold Shift to constrain."},
        {ToolType::MagicWand, "magic-wand", "Magic Wand", "S", 3, 0,
         "Automatically selects a range of similar colors.",
         "Click to select an area of similar color."},
        {ToolType::Move, "move-selected-pixels", "Move Selected Pixels", "M", 0, 1,
         "Moves the selected pixels (or the whole layer).",
         "Click and drag to move the selected pixels or the layer."},
        {ToolType::MoveSelection, "move-selection", "Move Selection", "M", 1, 1,
         "Moves the selection outline without moving the pixels.",
         "Click and drag to move the selection outline without moving pixels."},
        {ToolType::Zoom, "zoom", "Zoom", "Z", 2, 1,
         "Zooms in (left click) or out (right click).",
         "Left click to zoom in. Right click to zoom out."},
        {ToolType::Pan, "pan", "Pan", "H", 3, 1,
         "Pans the view around the image by dragging (hand).",
         "Click and drag to pan around the image."},
        {ToolType::Fill, "paint-bucket", "Paint Bucket", "F", 4, 0,
         "Fills a uniformly-colored area with the active color.",
         "Left click to fill an area with the primary color."},
        {ToolType::Gradient, "gradient", "Gradient", "G", 4, 1,
         "Draws a gradient between the primary and secondary color.",
         "Click and drag to draw a color gradient."},
        {ToolType::Brush, "paintbrush", "Paintbrush", "B", 5, 0,
         "Paints soft strokes with the active color.",
         "Left click to draw with the primary color, right click for the secondary color."},
        {ToolType::Eraser, "eraser", "Eraser", "E", 5, 1,
         "Erases to transparency (or the secondary color).",
         "Left click to erase to transparent. Right click to the secondary color."},
        {ToolType::Pencil, "pencil", "Pencil", "P", 6, 0,
         "Draws crisp 1-px pixels with no antialiasing.",
         "Left click to draw 1px with the primary color. Right click for the secondary."},
        {ToolType::ColorPicker, "color-picker", "Color Picker", "K", 6, 1,
         "Picks a color from the image as the active color.",
         "Left click to set the primary color. Right click for the secondary."},
        {ToolType::CloneStamp, "clone-stamp", "Clone Stamp", "L", 7, 0,
         "Clones part of the image (Ctrl+click sets the source).",
         "Ctrl + click to set the source. Left click to paint from the source."},
        {ToolType::Recolor, "recolor", "Recolor", "R", 7, 1,
         "Replaces a color with the active color where you paint.",
         "Left click to replace the secondary color with the primary."},
        {ToolType::Text, "text", "Text", "T", 8, 0,
         "Adds text on the image.",
         "Click to place text on the canvas."},
        {ToolType::Line, "line-curve", "Line / Curve", "O", 8, 1,
         "Draws lines and curves.",
         "Click and drag to draw a line. Hold Shift to constrain the angle."},
        {ToolType::Shape, "shapes", "Shapes", "O", 9, 0,
         "Draws shapes (rectangle, ellipse, star...).",
         "Click and drag to draw a shape. Hold Shift to constrain proportions."},
        {ToolType::Perspective, "perspective", "Perspective", "V", 9, 1,
         "Places vanishing points: guide lines for drawing in perspective, which strokes can follow.",
         "Click to place a vanishing point (up to 3, also outside the image). Drag to move, right click to remove."},
    };
    return tools;
}

const ToolInfo &toolInfo(ToolType type) {
    for (const ToolInfo &info : toolInfos())
        if (info.type == type) return info;
    return toolInfos().front();   // unreachable while every ToolType is listed
}

std::unique_ptr<Tool> createTool(ToolType type) {
    switch (type) {
    case ToolType::RectSelection:    return std::make_unique<SelectionTool>(SelectionShape::Rectangle);
    case ToolType::EllipseSelection: return std::make_unique<SelectionTool>(SelectionShape::Ellipse);
    case ToolType::LassoSelection:   return std::make_unique<LassoTool>();
    case ToolType::MagicWand:        return std::make_unique<MagicWandTool>();
    case ToolType::Move:             return std::make_unique<MoveTool>();
    case ToolType::MoveSelection:    return std::make_unique<MoveSelectionTool>();
    case ToolType::Zoom:             return std::make_unique<ZoomTool>();
    case ToolType::Pan:              return std::make_unique<PanTool>();
    case ToolType::Fill:             return std::make_unique<FillTool>();
    case ToolType::Gradient:         return std::make_unique<GradientTool>();
    case ToolType::Brush:            return std::make_unique<BrushTool>();
    case ToolType::Eraser:           return std::make_unique<EraserTool>();
    case ToolType::Pencil:           return std::make_unique<PencilTool>();
    case ToolType::ColorPicker:      return std::make_unique<ColorPickerTool>();
    case ToolType::CloneStamp:       return std::make_unique<CloneStampTool>();
    case ToolType::Recolor:          return std::make_unique<RecolorTool>();
    case ToolType::Text:             return std::make_unique<TextTool>();
    case ToolType::Line:             return std::make_unique<LineTool>();
    case ToolType::Shape:            return std::make_unique<ShapeTool>();
    case ToolType::Perspective:      return std::make_unique<PerspectiveTool>();
    }
    return nullptr;
}
