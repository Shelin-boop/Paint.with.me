// Main window: clipboard, selection, layer, image and view commands.
// Split from mainwindow.cpp; one class, several files.
#include "mainwindow.h"
#include "core/imageoperation.h"
#include "dialogs/pastedialog.h"
#include "core/document.h"
#include "core/thumbnails.h"
#include "canvas/canvaswidget.h"
#include "panels/layerspanel.h"
#include "panels/colorspanel.h"
#include "panels/historypanel.h"
#include "panels/tooloptionspanel.h"
#include "panels/imagelistbar.h"
#include "panels/toolspanel.h"
#include "core/pdfexport.h"
#include "core/perspective.h"
#include "dialogs/pdfexportdialog.h"
#include "core/history.h"
#include "dialogs/resizedialog.h"
#include "dialogs/canvassizedialog.h"
#include "dialogs/layerpropertiesdialog.h"
#include "dialogs/settingsdialog.h"
#include "dialogs/previewdialog.h"
#include "dialogs/saveconfigdialog.h"
#include "tools/tool.h"
#include "tools/texttool.h"
#include "tools/clonestamptool.h"
#include "adjustments/invertcolors.h"
#include "adjustments/sepia.h"
#include "adjustments/desaturate.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QToolTip>
#include "tools/texttool.h"
#include <QApplication>
#include <QMimeData>
#include <QUrl>
#include <QInputDialog>
#include <QSlider>
#include <QLabel>
#include <QPainter>
#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QMenu>
#include "desktopintegration.h"
#include <QClipboard>

void MainWindow::selectLayerAbove() {
    if (!m_document) return;
    int nextIndex = std::min(m_document->activeLayerIndex() + 1, m_document->layerCount() - 1);
    m_document->setActiveLayer(nextIndex);
}

void MainWindow::selectLayerBelow() {
    if (!m_document) return;
    int nextIndex = std::max(m_document->activeLayerIndex() - 1, 0);
    m_document->setActiveLayer(nextIndex);
}

void MainWindow::selectTopLayer() {
    if (!m_document || m_document->layerCount() == 0) return;
    m_document->setActiveLayer(m_document->layerCount() - 1);
}

void MainWindow::selectBottomLayer() {
    if (!m_document || m_document->layerCount() == 0) return;
    m_document->setActiveLayer(0);
}

void MainWindow::adjustCurrentToolBrushSize(int delta) {
    if (!m_currentTool || !allowSingleKeyShortcuts()) return;
    m_currentTool->setBrushSize(m_currentTool->brushSize() + delta);
    if (m_toolOptionsPanel) {
        m_toolOptionsPanel->setTool(m_currentTool);
    }
}

void MainWindow::toggleActiveLayerVisibility() {
    auto *layer = m_document ? m_document->activeLayer() : nullptr;
    if (!layer) return;
    layer->setVisible(!layer->isVisible());
    emit m_document->layersChanged();
    emit m_document->documentChanged();
}

void MainWindow::showLayerPropertiesDialog() {
    auto *layer = m_document ? m_document->activeLayer() : nullptr;
    if (!layer) return;

    // Remember the original state so Cancel can restore it after live previews.
    const float origOpacity = layer->opacity();
    const bool origVisible = layer->isVisible();
    const BlendMode origBlend = layer->blendMode();
    const auto modes = Layer::allBlendModes();

    LayerPropertiesDialog dlg(layer, this);
    // Apply every change to the canvas immediately for a live preview.
    connect(&dlg, &LayerPropertiesDialog::previewChanged, this,
            [this, layer, &modes](int opacity255, bool visible, int blendIdx) {
        layer->setOpacity(opacity255 / 255.0f);
        layer->setVisible(visible);
        if (blendIdx >= 0 && blendIdx < modes.size())
            layer->setBlendMode(modes[blendIdx]);
        emit m_document->documentChanged();
        emit m_document->layersChanged();
    });

    if (dlg.exec() == QDialog::Accepted) {
        // Restore the original state, then apply the final one THROUGH history so
        // the whole change is a single undoable step.
        layer->setOpacity(origOpacity);
        layer->setVisible(origVisible);
        layer->setBlendMode(origBlend);
        const int idx = m_document->activeLayerIndex();
        auto cmd = std::make_unique<LayerPropertyCommand>(
            m_document, idx, origOpacity, origVisible, static_cast<int>(origBlend),
            layer->name(), layer->isLocked(), "Layer Properties");
        layer->setName(dlg.layerName());
        layer->setVisible(dlg.isVisible());
        layer->setBlendMode(dlg.blendMode());
        layer->setOpacity(dlg.opacity() / 255.0f);
        cmd->captureAfter();
        m_document->history().push(std::move(cmd));
        emit m_document->layersChanged();
        emit m_document->documentChanged();
    } else {
        // Cancelled: revert to the original state.
        layer->setOpacity(origOpacity);
        layer->setVisible(origVisible);
        layer->setBlendMode(origBlend);
        emit m_document->documentChanged();
        emit m_document->layersChanged();
    }
}

void MainWindow::undo() { m_document->history().undo(); m_canvas->updateCanvas(); }

void MainWindow::redo() { m_document->history().redo(); m_canvas->updateCanvas(); }

void MainWindow::cut() {
    copy();
    deleteSelectionContents();
}

void MainWindow::copy() {
    if (!m_document->activeLayer()) return;
    QImage img = m_document->activeLayer()->image();
    if (m_document->selection().hasSelection()) {
        img = m_document->selection().getMaskedImage(img, m_document->activeLayer()->offset());
    }
    QApplication::clipboard()->setImage(img);
}

void MainWindow::copyMerged() {
    if (!m_document) return;
    // Copy the flattened (all visible layers) image, cropped to the selection.
    QImage img = m_document->flatten();
    if (m_document->selection().hasSelection())
        img = m_document->selection().getMaskedImage(img);
    QApplication::clipboard()->setImage(img);
}

// Clipboard image data wins over file references. Only absolute local files are
// decoded: clipboard text is never executed and non-file URLs are never fetched.
static QImage clipboardImage() {
    auto *clipboard = QApplication::clipboard();
    QImage image = clipboard->image();
    if (!image.isNull()) return image;
    const QMimeData *mime = clipboard->mimeData();
    if (!mime) return {};
    const auto load = [](const QString &path) {
        const QFileInfo file(path);
        if (!file.isAbsolute() || !file.isFile()) return QImage();
        return QImage(path);
    };
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile() || (!url.host().isEmpty() && url.host() != "localhost")) continue;
        image = load(url.toLocalFile());
        if (!image.isNull()) return image;
    }
    for (const QString &line : mime->text().split('\n')) {
        // Do not trim filesystem paths: leading/trailing spaces can be filenames.
        QString path = line;
        if (path.endsWith('\r')) path.chop(1);
        if (!QFileInfo(path).isAbsolute()) {
            const QUrl url(path);
            if (!url.isLocalFile() || (!url.host().isEmpty() && url.host() != "localhost")) continue;
            path = url.toLocalFile();
        }
        image = load(path);
        if (!image.isNull()) return image;
    }
    return {};
}

void MainWindow::paste() {
    // While the Text tool is typing, Ctrl+V inserts clipboard *text* at the caret
    // rather than dropping a clipboard image onto a new layer.
    if (m_currentTool && m_currentTool->type() == ToolType::Text && m_currentTool->wantsKeyInput()) {
        const QString clip = QApplication::clipboard()->text();
        if (!clip.isEmpty()) {
            static_cast<TextTool *>(m_currentTool)->insertText(clip, *m_canvas);
            return;
        }
    }
    QImage img = clipboardImage();
    if (img.isNull()) return;
    bool expand = false;
    if (img.width() > m_document->width() || img.height() > m_document->height()) {
        PasteDialog dialog(img, this);
        const int choice = dialog.exec();
        if (choice == PasteDialog::Cancel) return;
        expand = choice == PasteDialog::Expand;
    }
    // Finish any pending Line/Shape edit on its original layer, not the newly
    // pasted layer. This must remain after the dialog's cancellation boundary.
    selectTool(ToolType::Move);
    m_document->pasteImage(img, QStringLiteral("Pasted Layer"), expand);
    m_canvas->updateCanvas();
}

void MainWindow::pasteIntoNewLayer() {
    // In this implementation regular paste already lands on a new layer; this
    // menu item makes the intent explicit and is what paint.net users expect.
    paste();
}

void MainWindow::pasteIntoNewImage() {
    // Paint.NET's "Paste into New Image": open the clipboard image as its own
    // document, sized to the image, rather than dropping it on the current one.
    const QImage img = clipboardImage();
    if (img.isNull()) return;
    auto *doc = new Document(img.width(), img.height(), this);
    doc->activeLayer()->setImage(img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    doc->setModified(false);
    addDocument(doc);
}

void MainWindow::fillSelection() {
    // Paint.NET's "Fill Selection" (Backspace): flood the selected region — or the
    // whole active layer when nothing is selected — with the primary colour.
    if (!m_document) return;
    if (!convertTextLayersFor({m_document->activeLayerIndex()})) return;
    auto *layer = m_document->activeLayer();
    if (!layer || layer->isLocked()) return;

    QImage before = layer->image().copy();
    QPainter p(&layer->image());
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (m_document->selection().hasSelection()) {
        // Paint only where the selection mask is set, honouring the layer offset.
        QImage fill(layer->image().size(), QImage::Format_ARGB32_Premultiplied);
        fill.fill(m_document->primaryColor());
        fill = m_document->selection().getMaskedImage(fill, layer->offset());
        p.drawImage(0, 0, fill);
    } else {
        p.fillRect(layer->image().rect(), m_document->primaryColor());
    }
    p.end();

    if (layer->image() != before) {
        m_document->pushImageEdit(m_document->activeLayerIndex(), before, "Fill Selection");
        m_canvas->updateCanvas();
    }
}

void MainWindow::selectAll() {
    m_document->selection().selectAll();
    emit m_document->selectionChanged();
    m_canvas->update();
}

void MainWindow::deselectAll() {
    m_document->selection().clear();
    emit m_document->selectionChanged();
    m_canvas->update();
}

void MainWindow::invertSelection() {
    m_document->selection().invert();
    emit m_document->selectionChanged();
    m_canvas->update();
}

void MainWindow::moveActiveLayerUp() {
    if (!m_document) return;
    const int idx = m_document->activeLayerIndex();
    if (idx < m_document->layerCount() - 1) {
        m_document->moveLayer(idx, idx + 1);
        m_canvas->updateCanvas();
    }
}

void MainWindow::moveActiveLayerDown() {
    if (!m_document) return;
    const int idx = m_document->activeLayerIndex();
    if (idx > 0) {
        m_document->moveLayer(idx, idx - 1);
        m_canvas->updateCanvas();
    }
}

void MainWindow::importLayerFromFile() {
    if (!m_document) return;
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Import Layer from File"),
        QString(), QStringLiteral("Images (*.psd *.psb *.pdn *.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp)"));
    if (file.isEmpty()) return;
    QImage img(file);
    if (img.isNull()) {
        // Layered files come in as one layer, as they look.
        Document layered(1, 1);
        if (layered.load(file)) img = layered.flatten();
    }
    if (img.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Could not load the image file."));
        return;
    }
    m_document->addLayer(img, QFileInfo(file).fileName());
    m_canvas->updateCanvas();
}

void MainWindow::flipActiveLayerHorizontal() {
    if (!m_document) return;
    if (!convertTextLayersFor({m_document->activeLayerIndex()})) return;
    auto *layer = m_document->activeLayer();
    if (!layer || layer->isLocked()) return;
    QImage before = layer->image().copy();
    layer->setImage(layer->image().mirrored(true, false));
    m_document->pushImageEdit(m_document->activeLayerIndex(), before, "Flip Layer");
    m_canvas->updateCanvas();
}

void MainWindow::flipActiveLayerVertical() {
    if (!m_document) return;
    if (!convertTextLayersFor({m_document->activeLayerIndex()})) return;
    auto *layer = m_document->activeLayer();
    if (!layer || layer->isLocked()) return;
    QImage before = layer->image().copy();
    layer->setImage(layer->image().mirrored(false, true));
    m_document->pushImageEdit(m_document->activeLayerIndex(), before, "Flip Layer");
    m_canvas->updateCanvas();
}

void MainWindow::showRotateZoomLayerDialog() {
    if (!m_document) return;
    if (!convertTextLayersFor({m_document->activeLayerIndex()})) return;
    auto *layer = m_document->activeLayer();
    if (!layer || layer->isLocked()) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Rotate / Zoom Layer"));
    auto *form = new QVBoxLayout(&dlg);

    // Angle slider: -180..180 degrees.
    auto *angleLabel = new QLabel(QStringLiteral("Angle: 0°"), &dlg);
    auto *angleSlider = new QSlider(Qt::Horizontal, &dlg);
    angleSlider->setRange(-180, 180);
    angleSlider->setValue(0);
    form->addWidget(angleLabel);
    form->addWidget(angleSlider);

    // Scale slider: 10..400 %.
    auto *scaleLabel = new QLabel(QStringLiteral("Scale: 100 %"), &dlg);
    auto *scaleSlider = new QSlider(Qt::Horizontal, &dlg);
    scaleSlider->setRange(10, 400);
    scaleSlider->setValue(100);
    form->addWidget(scaleLabel);
    form->addWidget(scaleSlider);

    connect(angleSlider, &QSlider::valueChanged, angleLabel, [angleLabel](int v) {
        angleLabel->setText(QStringLiteral("Angle: ") + QString::number(v) + "°");
    });
    connect(scaleSlider, &QSlider::valueChanged, scaleLabel, [scaleLabel](int v) {
        scaleLabel->setText(QStringLiteral("Scale: ") + QString::number(v) + " %");
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    const double angle = angleSlider->value();
    const double scale = scaleSlider->value() / 100.0;
    if (angle == 0.0 && scale == 1.0) return;   // nothing to do

    const QImage before = layer->image().copy();
    const QSize sz = before.size();

    // Rotate + scale about the layer centre, keeping the canvas dimensions so the
    // result stays aligned with the rest of the document (one undo entry).
    QImage result(sz, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    {
        QPainter p(&result);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.translate(sz.width() / 2.0, sz.height() / 2.0);
        p.rotate(angle);
        p.scale(scale, scale);
        p.translate(-sz.width() / 2.0, -sz.height() / 2.0);
        p.drawImage(0, 0, before);
    }
    layer->setImage(result);
    m_document->pushImageEdit(m_document->activeLayerIndex(), before, "Rotate / Zoom Layer");
    m_canvas->updateCanvas();
}

void MainWindow::growSelection() {
    if (!m_document || !m_document->selection().hasSelection()) return;
    bool ok = false;
    const int px = QInputDialog::getInt(this, QStringLiteral("Grow Selection"),
                                        QStringLiteral("Grow by (pixels):"), 1, 1, 500, 1, &ok);
    if (!ok) return;
    m_document->selection().expand(px);
    emit m_document->selectionChanged();
    m_canvas->update();
}

void MainWindow::shrinkSelection() {
    if (!m_document || !m_document->selection().hasSelection()) return;
    bool ok = false;
    const int px = QInputDialog::getInt(this, QStringLiteral("Shrink Selection"),
                                        QStringLiteral("Shrink by (pixels):"), 1, 1, 500, 1, &ok);
    if (!ok) return;
    m_document->selection().contract(px);
    emit m_document->selectionChanged();
    m_canvas->update();
}

void MainWindow::featherSelection() {
    if (!m_document || !m_document->selection().hasSelection()) return;
    bool ok = false;
    const int px = QInputDialog::getInt(this, QStringLiteral("Feather Selection"),
                                        QStringLiteral("Feather radius (pixels):"), 2, 1, 200, 1, &ok);
    if (!ok) return;
    m_document->selection().feather(px);
    emit m_document->selectionChanged();
    m_canvas->update();
}

// ---- Image operations ----

void MainWindow::resizeImage() {
    ResizeDialog dlg(m_document->width(), m_document->height(), m_document->dpi(), this);
    if (dlg.exec() == QDialog::Accepted) {
        m_document->resize(dlg.newWidth(), dlg.newHeight(), dlg.resolution());
        m_sizeLabel->setText(QString("%1 × %2").arg(m_document->width()).arg(m_document->height()));
        m_canvas->updateCanvas();
    }
}

void MainWindow::canvasSize() {
    CanvasSizeDialog dlg(m_document->width(), m_document->height(), m_document->dpi(), this);
    if (dlg.exec() == QDialog::Accepted) {
        m_document->resizeCanvas(dlg.newWidth(), dlg.newHeight(), dlg.anchorX(), dlg.anchorY(),
                                 dlg.resolution());
        m_sizeLabel->setText(QString("%1 × %2").arg(m_document->width()).arg(m_document->height()));
        m_canvas->updateCanvas();
    }
}

void MainWindow::applyImageOperation(const QImage &result, const QString &description) {
    auto *layer = m_document->activeLayer();
    if (!layer) return;
    if (!convertTextLayersFor({m_document->activeLayerIndex()})) return;
    layer = m_document->activeLayer();
    QImage before = layer->image().copy();
    layer->setImage(result);
    m_document->pushImageEdit(m_document->activeLayerIndex(), before, description);
    m_canvas->updateCanvas();
}

void MainWindow::applyImageOperationToTargetLayers(const std::function<QImage(const QImage &)> &operation, const QString &description) {
    if (!m_document) return;
    if (!convertTextLayersFor(targetLayerIndices())) return;

    // Remember this operation (with its parameters bound) so "Repeat last effect"
    // can re-apply it without re-prompting.
    m_lastEffect = [this, operation, description]() {
        applyImageOperationToTargetLayers(operation, description);
    };
    m_lastEffectLabel = description;
    if (m_repeatEffectAction) {
        m_repeatEffectAction->setEnabled(true);
        m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + description);
    }

    // When a selection is active, confine the effect to it (paint.net behaviour).
    // Only do this for size-preserving operations; generative effects that
    // return a different size fall through to the whole-layer path.
    if (m_document->selection().hasSelection()) {
        applySelectionFilterToTargetLayers(operation, description);
        return;
    }

    bool changed = false;
    for (int index : targetLayerIndices()) {
        auto *layer = m_document->layerAt(index);
        if (!layer) continue;

        QImage before = layer->image().copy();
        QImage after = operation(before);
        // Compare in the same format: adjustments hand back straight-alpha
        // ARGB32 while the layer is premultiplied, so a raw != would always be
        // true and push a redundant undo step for an identity operation.
        if (after.format() != before.format())
            after = after.convertToFormat(before.format());
        if (after.size() != before.size() || after != before) {
            layer->setImage(after);
            m_document->pushImageEdit(index, before, description);
            changed = true;
        }
    }

    if (changed) {
        m_canvas->updateCanvas();
    }
}

void MainWindow::applySelectionFilterToTargetLayers(const std::function<QImage(const QImage &)> &operation, const QString &description) {
    if (!m_document || !m_document->selection().hasSelection()) return;

    const QRect selectionRect = m_document->selection().boundingRect();
    bool changed = false;

    for (int index : targetLayerIndices()) {
        auto *layer = m_document->layerAt(index);
        if (!layer) continue;

        QRect rect = selectionRect.translated(-layer->offset()).intersected(layer->image().rect());
        if (rect.isEmpty()) continue;

        QImage before = layer->image().copy();
        layer->setImage(adjustedImage(before, layer->offset(), m_document->selection(), operation));

        if (layer->image() != before) {
            m_document->pushImageEdit(index, before, description);
            changed = true;
        }
    }

    if (changed) {
        m_canvas->updateCanvas();
    }
}

void MainWindow::transformSelectionContents(const std::function<QImage(const QImage &)> &operation, const QString &description) {
    if (!m_document || !m_document->selection().hasSelection()) return;
    if (!convertTextLayersFor(targetLayerIndices())) return;

    const QRect selectionRect = m_document->selection().boundingRect();
    bool changed = false;

    for (int index : targetLayerIndices()) {
        auto *layer = m_document->layerAt(index);
        if (!layer) continue;

        QRect rect = selectionRect.translated(-layer->offset()).intersected(layer->image().rect());
        if (rect.isEmpty()) continue;

        QImage before = layer->image().copy();
        QImage source = layer->image().copy(rect).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage mask = m_document->selection().maskForImage(layer->image().size(), layer->offset()).copy(rect).convertToFormat(QImage::Format_Grayscale8);

        for (int y = 0; y < rect.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb*>(source.scanLine(y));
            const uchar *maskLine = mask.constScanLine(y);
            for (int x = 0; x < rect.width(); ++x) {
                line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), qAlpha(line[x]) * maskLine[x] / 255);
            }
        }

        QImage transformed = operation(source).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_document->selection().eraseFromImage(layer->image(), layer->offset());

        QRect targetRect(QPoint(0, 0), transformed.size());
        targetRect.moveCenter(rect.center());
        QPainter painter(&layer->image());
        painter.drawImage(targetRect.topLeft(), transformed);

        if (layer->image() != before) {
            m_document->pushImageEdit(index, before, description);
            changed = true;
        }
    }

    if (changed) {
        m_canvas->updateCanvas();
    }
}

void MainWindow::deleteSelectionContents() {
    if (!m_document || !m_document->selection().hasSelection()) return;
    if (!convertTextLayersFor(targetLayerIndices())) return;

    bool changed = false;
    for (int index : targetLayerIndices()) {
        auto *layer = m_document->layerAt(index);
        if (!layer) continue;

        QImage before = layer->image().copy();
        m_document->selection().eraseFromImage(layer->image(), layer->offset());
        if (layer->image() != before) {
            m_document->pushImageEdit(index, before, "Erase Selection");
            changed = true;
        }
    }

    if (changed) {
        m_canvas->updateCanvas();
    }
}

void MainWindow::showSelectionContextMenu(const QPoint &globalPos) {
    if (!m_document || !m_document->selection().hasSelection()) return;

    QMenu menu(this);
    menu.addAction(QStringLiteral("Delete"), this, &MainWindow::deleteSelectionContents);
    menu.addAction(QStringLiteral("Cut"), this, &MainWindow::cut);
    menu.addAction(QStringLiteral("Copy"), this, &MainWindow::copy);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Rotate 90° Clockwise"), this, [this]() {
        QTransform t;
        t.rotate(90);
        transformSelectionContents([t](const QImage &image) {
            return image.transformed(t, Qt::SmoothTransformation);
        }, "Rotate Selection 90° CW");
    });
    menu.addAction(QStringLiteral("Rotate 90° Counter-Clockwise"), this, [this]() {
        QTransform t;
        t.rotate(-90);
        transformSelectionContents([t](const QImage &image) {
            return image.transformed(t, Qt::SmoothTransformation);
        }, "Rotate Selection 90° CCW");
    });
    menu.addAction(QStringLiteral("Flip Horizontal"), this, [this]() {
        transformSelectionContents([](const QImage &image) {
            return image.mirrored(true, false);
        }, "Flip Selection Horizontal");
    });
    menu.addAction(QStringLiteral("Flip Vertical"), this, [this]() {
        transformSelectionContents([](const QImage &image) {
            return image.mirrored(false, true);
        }, "Flip Selection Vertical");
    });

    auto *effectsMenu = menu.addMenu(QStringLiteral("Quick effects"));
    effectsMenu->addAction(QStringLiteral("Invert Colors"), this, [this]() {
        InvertColors adj;
        applySelectionFilterToTargetLayers([adj](const QImage &image) mutable {
            return adj.apply(image);
        }, "Invert Colors (Selection)");
    });
    effectsMenu->addAction(QStringLiteral("Black and White"), this, [this]() {
        Desaturate adj;
        applySelectionFilterToTargetLayers([adj](const QImage &image) mutable {
            return adj.apply(image);
        }, "Black and White (Selection)");
    });
    effectsMenu->addAction(QStringLiteral("Sepia"), this, [this]() {
        Sepia adj;
        adj.setIntensity(80);
        applySelectionFilterToTargetLayers([adj](const QImage &image) mutable {
            return adj.apply(image);
        }, "Sepia (Selection)");
    });

    menu.addSeparator();
    menu.addAction(QStringLiteral("Crop to Selection"), this, &MainWindow::cropToSelection);
    menu.addAction(QStringLiteral("Invert Selection"), this, &MainWindow::invertSelection);
    menu.addAction(QStringLiteral("Deselect"), this, &MainWindow::deselectAll);
    menu.exec(globalPos);
}

bool MainWindow::convertTextLayersFor(const std::vector<int> &indices) {
    if (!m_document) return false;
    QVector<int> texts = m_document->textLayersAmong(QVector<int>(indices.begin(), indices.end()));
    if (texts.isEmpty()) return true;
    QStringList names;
    for (int i : texts) names << QStringLiteral("\u201c%1\u201d").arg(m_document->layerAt(i)->name());
    QMessageBox box(QMessageBox::Question, QStringLiteral("Text Layer"),
                    texts.size() == 1
                        ? QStringLiteral("%1 is a text layer.").arg(names.first())
                        : QStringLiteral("%1 are text layers.").arg(names.join(QStringLiteral(", "))),
                    QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("convertTextToPixels"));
    box.setInformativeText(QStringLiteral(
        "This command changes pixels. Turned into pixels, the text can no longer be "
        "edited as text. Undo brings it back."));
    QPushButton *convert = box.addButton(QStringLiteral("Convert to Pixels"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(convert);
    box.exec();
    if (box.clickedButton() != convert) return false;
    for (int i : texts) m_document->convertTextToPixels(i);
    m_canvas->updateCanvas();
    return true;
}

TextTool *MainWindow::textTool() const {
    for (const auto &tool : m_tools)
        if (tool->type() == ToolType::Text) return static_cast<TextTool *>(tool.get());
    return nullptr;
}

void MainWindow::onPrimaryColorChanged(const QColor &color) {
    TextTool *text = textTool();
    if (text && text->isEditing() && text->editedDocument() == m_document) text->setColor(color);
}

void MainWindow::onTextLayerBlocked(const QString &layerName) {
    const QString message = QStringLiteral(
        "\u201c%1\u201d is a text layer. To paint on it, choose Layers > Convert Text to Pixels, "
        "or add a new layer.").arg(layerName);
    statusBar()->showMessage(message, 8000);
    QToolTip::showText(QCursor::pos(), message, m_canvas);
}

void MainWindow::convertActiveTextToPixels() {
    if (!m_document) return;
    if (m_document->convertTextToPixels(m_document->activeLayerIndex())) {
        m_canvas->updateCanvas();
        statusBar()->showMessage(QStringLiteral("The text is now pixels. Undo makes it editable again."), 5000);
    }
}

std::vector<int> MainWindow::targetLayerIndices() const {
    std::vector<int> indices;
    if (!m_document) return indices;

    if (m_document->editAllLayers()) {
        indices.reserve(m_document->layerCount());
        for (int i = 0; i < m_document->layerCount(); ++i) {
            indices.push_back(i);
        }
        return indices;
    }

    indices.push_back(m_document->activeLayerIndex());
    return indices;
}

void MainWindow::rotateClockwise() {
    if (!m_document) return;
    m_document->rotate(1, "Rotate 90° CW");
    m_canvas->updateCanvas();
}

void MainWindow::rotateCounterClockwise() {
    if (!m_document) return;
    m_document->rotate(3, "Rotate 90° CCW");
    m_canvas->updateCanvas();
}

void MainWindow::rotate180() {
    if (!m_document) return;
    m_document->rotate(2, "Rotate 180°");
    m_canvas->updateCanvas();
}

void MainWindow::flipHorizontal() {
    if (!m_document) return;
    m_document->flip(true, "Flip Horizontal");
    m_canvas->updateCanvas();
}

void MainWindow::flipVertical() {
    if (!m_document) return;
    m_document->flip(false, "Flip Vertical");
    m_canvas->updateCanvas();
}

void MainWindow::cropToSelection() {
    if (!m_document->selection().hasSelection()) return;
    QRect rect = m_document->selection().boundingRect();
    if (rect.isEmpty()) return;

    // Undoable crop that also updates the document dimensions (keeps canvas and
    // document size in sync).
    m_document->cropTo(rect);
    emit m_document->selectionChanged();
    m_canvas->updateCanvas();
    m_sizeLabel->setText(QString("%1 × %2").arg(m_document->width()).arg(m_document->height()));
}

void MainWindow::flattenImage() {
    m_document->flattenImage();
    m_canvas->updateCanvas();
}

// ---- View operations ----

void MainWindow::zoomIn() { m_canvas->zoomIn(); }

void MainWindow::zoomOut() { m_canvas->zoomOut(); }

void MainWindow::zoomToFit() { m_canvas->zoomToFit(); }

void MainWindow::zoomToSelection() {
    if (m_document && m_document->selection().hasSelection())
        m_canvas->zoomToRect(m_document->selection().boundingRect());
    else
        m_canvas->zoomToFit();
}

void MainWindow::zoomToActual() { m_canvas->zoomToActual(); }

void MainWindow::toggleGrid() { m_canvas->setShowGrid(!m_canvas->showGrid()); }

// ---- Tool selection ----
