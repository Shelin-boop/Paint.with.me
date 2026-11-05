#include "pastedialog.h"
#include <QPushButton>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyleOptionButton>
#include <QStylePainter>

namespace {
// Lay out the two labels explicitly, retaining native push-button interaction.
// Some styles (Breeze) intercept QCommandLinkButton's entire paint event, which
// bypasses our frame and paints duplicate text underneath the child labels.
class PasteChoiceButton : public QPushButton {
public:
    PasteChoiceButton(const QString &title, const QString &description,
                      const QIcon &icon, QWidget *parent)
        : QPushButton(title, parent) {
        setAccessibleDescription(description);
        setIcon(icon);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(8);
        auto *leading = new QLabel(this);
        leading->setPixmap(icon.pixmap(20, 20));
        leading->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(leading, 0, Qt::AlignTop);
        auto *texts = new QVBoxLayout;
        texts->setSpacing(3);
        auto *heading = new QLabel(title, this);
        heading->setStyleSheet("font-size: 14px; font-weight: bold;");
        auto *explanation = new QLabel(description, this);
        explanation->setStyleSheet("font-size: 11px;");
        explanation->setWordWrap(true);
        for (auto *label : {heading, explanation}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            texts->addWidget(label);
        }
        layout->addLayout(texts, 1);
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    QSize sizeHint() const override { return QSize(480, heightForWidth(480)); }
    QSize minimumSizeHint() const override { return QSize(300, 40); }
    int heightForWidth(int width) const override { return layout()->totalHeightForWidth(width); }
protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = QIcon();
        QStylePainter painter(this);
        // CE_PushButton is also Qt's native command-link frame primitive.
        painter.save();
        painter.drawControl(QStyle::CE_PushButton, option);
        painter.restore();
        // Do not depend on a style's command-link or stylesheet focus/hover
        // frame: keep feedback visible around the whole clickable row.
        if (isEnabled() && (hasFocus() || underMouse())) {
            painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect().adjusted(1, 1, -2, -2));
        }
    }
};
}

PasteDialog::PasteDialog(const QImage &image, QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Paste"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    auto *layout = new QVBoxLayout(this);
    auto *header = new QHBoxLayout;
    auto *thumbnail = new QLabel(this);
    thumbnail->setFixedSize(120, 80);
    thumbnail->setAlignment(Qt::AlignCenter);
    thumbnail->setPixmap(QPixmap::fromImage(image.scaled(120, 80, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation)));
    auto *message = new QLabel(QStringLiteral("The image being pasted is larger than the canvas size. What do you want to do?"), this);
    message->setWordWrap(true);
    header->addWidget(thumbnail);
    header->addWidget(message, 1, Qt::AlignTop);
    header->setSpacing(12);
    layout->addLayout(header);
    layout->addSpacing(6);
    const auto row = [&](const QString &title, const QString &description,
                         QStyle::StandardPixmap icon, Choice choice) {
        auto *button = new PasteChoiceButton(title, description, style()->standardIcon(icon), this);
        button->setAutoDefault(false);
        // Reserve stable frame metrics; paintEvent supplies palette-derived
        // focus/hover feedback independently of the application's stylesheet.
        button->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; border: 1px solid transparent; padding: 4px; }"));

        connect(button, &QPushButton::clicked, this, [this, choice] { done(choice); });
        layout->addWidget(button);
        return button;
    };
    auto *expand = row(QStringLiteral("Expand canvas"),
        QStringLiteral("Automatically expands the canvas to fit the image being pasted."),
        QStyle::SP_FileDialogToParent, Expand);
    row(QStringLiteral("Keep canvas size"),
        QStringLiteral("Does not expand the canvas. You will have to move the pasted image around to make sure that the part you want is within the canvas boundaries."),
        QStyle::SP_FileDialogContentsView, Keep);
    row(QStringLiteral("Cancel"), QStringLiteral("Cancels the paste action."), QStyle::SP_ArrowBack, Cancel);
    expand->setDefault(true);
    expand->setFocus();
    resize(520, sizeHint().height());
}
