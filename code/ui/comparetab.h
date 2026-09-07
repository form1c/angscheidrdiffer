#pragma once

#include <QWidget>

// Base class of every comparison tab: file, folder and three-way merge.
// The window routes toolbar and menu actions to the current tab.
class CompareTab : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;

    // Text for the tab label and the window title.
    virtual QString tabTitle() const = 0;

    // Current text for the status bar, for example "Difference 3 of 12".
    virtual QString currentStatusText() const { return {}; }

    // The global options have changed and are to be applied again.
    virtual void optionsChanged() {}

    // Reload, bound to F5: read the files again or scan the folders again.
    virtual void reloadContent() {}

    // Asked before closing. False cancels, for example on unsaved changes.
    virtual bool maybeClose() { return true; }

Q_SIGNALS:
    void titleChanged();
    void statusTextChanged(const QString &text);
    void transientMessage(const QString &text, int timeoutMs);
};
