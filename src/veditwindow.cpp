#include <QtWidgets>
#include <QtDebug>
#include "veditwindow.h"
#include "vedittab.h"
#include "vnote.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VEditWindow::VEditWindow(VNote *vnote, QWidget *parent)
    : QTabWidget(parent), vnote(vnote)
{
    setupCornerWidget();

    setTabsClosable(true);
    setMovable(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &VEditWindow::tabCloseRequested,
            this, &VEditWindow::handleTabCloseRequest);
    connect(this, &VEditWindow::tabBarClicked,
            this, &VEditWindow::handleTabbarClicked);
    connect(this, &VEditWindow::customContextMenuRequested,
            this, &VEditWindow::contextMenuRequested);
}

void VEditWindow::setupCornerWidget()
{
    // Right corner button
    // Actions
    splitAct = new QAction(QIcon(":/resources/icons/split_window.svg"),
                                    tr("Split"), this);
    splitAct->setStatusTip(tr("Split current window vertically"));
    connect(splitAct, &QAction::triggered,
            this, &VEditWindow::splitWindow);

    removeSplitAct = new QAction(QIcon(":/resources/icons/remove_split.svg"),
                                          tr("Remove split"), this);
    removeSplitAct->setStatusTip(tr("Remove current split window"));
    connect(removeSplitAct, &QAction::triggered,
            this, &VEditWindow::removeSplit);

    rightBtn = new QPushButton(QIcon(":/resources/icons/corner_menu.svg"),
                               "", this);
    QMenu *rightMenu = new QMenu(this);
    rightMenu->addAction(splitAct);
    rightMenu->addAction(removeSplitAct);
    rightBtn->setMenu(rightMenu);
    setCornerWidget(rightBtn, Qt::TopRightCorner);

    // Left corner button
    tabListAct = new QActionGroup(this);
    connect(tabListAct, &QActionGroup::triggered,
            this, &VEditWindow::tabListJump);
    leftBtn = new QPushButton(QIcon(":/resources/icons/corner_tablist.svg"),
                              "", this);
    QMenu *leftMenu = new QMenu(this);
    leftBtn->setMenu(leftMenu);
    setCornerWidget(leftBtn, Qt::TopLeftCorner);
}

void VEditWindow::splitWindow()
{
    emit requestSplitWindow(this);
}

void VEditWindow::removeSplit()
{
    // Close all the files one by one
    // If user do not want to close a file, just stop removing
    if (closeAllFiles()) {
        Q_ASSERT(count() == 0);
        emit requestRemoveSplit(this);
    }
}

void VEditWindow::setRemoveSplitEnable(bool enabled)
{
    removeSplitAct->setVisible(enabled);
}

void VEditWindow::openWelcomePage()
{
    int idx = openFileInTab("", vconfig.getWelcomePagePath(), false);
    setTabText(idx, "Welcome to VNote");
    setTabToolTip(idx, "VNote");
}

int VEditWindow::insertTabWithData(int index, QWidget *page,
                                   const QJsonObject &tabData)
{
    QString label = getFileName(tabData["relative_path"].toString());
    int idx = insertTab(index, page, label);
    QTabBar *tabs = tabBar();
    tabs->setTabData(idx, tabData);
    noticeStatus(currentIndex());
    return idx;
}

int VEditWindow::appendTabWithData(QWidget *page, const QJsonObject &tabData)
{
    return insertTabWithData(count(), page, tabData);
}

int VEditWindow::openFile(const QString &notebook, const QString &relativePath, int mode)
{
    // Find if it has been opened already
    int idx = findTabByFile(notebook, relativePath);
    if (idx > -1) {
        goto out;
    }
    idx = openFileInTab(notebook, relativePath, true);
out:
    setCurrentIndex(idx);
    if (mode == OpenFileMode::Edit) {
        editFile();
    }
    focusWindow();
    noticeStatus(idx);
    return idx;
}

void VEditWindow::closeFile(const QString &notebook, const QString &relativePath)
{
    // Find if it has been opened already
    int idx = findTabByFile(notebook, relativePath);
    if (idx == -1) {
        return;
    }

    // Do not check if modified
    VEditTab *editor = getTab(idx);
    Q_ASSERT(editor);
    removeTab(idx);
    delete editor;

    updateTabListMenu();
}

bool VEditWindow::closeAllFiles()
{
    int nrTab = count();
    for (int i = 0; i < nrTab; ++i) {
        // Always close the first tab
        if (!handleTabCloseRequest(0)) {
            return false;
        }
    }
    return true;
}

int VEditWindow::openFileInTab(const QString &notebook, const QString &relativePath,
                              bool modifiable)
{
    QString rootPath;
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();
    for (int i = 0; i < notebooks.size(); ++i) {
        if (notebooks[i].getName() == notebook) {
            rootPath = notebooks[i].getPath();
            break;
        }
    }

    VEditTab *editor = new VEditTab(QDir::cleanPath(QDir(rootPath).filePath(relativePath)),
                                    modifiable);
    connect(editor, &VEditTab::getFocused,
            this, &VEditWindow::getFocused);
    connect(editor, &VEditTab::outlineChanged,
            this, &VEditWindow::handleOutlineChanged);
    connect(editor, &VEditTab::curHeaderChanged,
            this, &VEditWindow::handleCurHeaderChanged);

    QJsonObject tabJson;
    tabJson["notebook"] = notebook;
    tabJson["relative_path"] = relativePath;
    tabJson["modifiable"] = modifiable;
    int idx = appendTabWithData(editor, tabJson);
    updateTabListMenu();
    return idx;
}

int VEditWindow::findTabByFile(const QString &notebook, const QString &relativePath) const
{
    QTabBar *tabs = tabBar();
    int nrTabs = tabs->count();

    for (int i = 0; i < nrTabs; ++i) {
        QJsonObject tabJson = tabs->tabData(i).toJsonObject();
        if (tabJson["notebook"] == notebook && tabJson["relative_path"] == relativePath) {
            return i;
        }
    }
    return -1;
}

bool VEditWindow::handleTabCloseRequest(int index)
{
    qDebug() << "request closing tab" << index;
    VEditTab *editor = getTab(index);
    Q_ASSERT(editor);
    bool ok = editor->requestClose();
    if (ok) {
        removeTab(index);
        delete editor;
    }
    updateTabListMenu();
    noticeStatus(currentIndex());
    // User clicks the close button. We should make this window
    // to be current window.
    emit getFocused();
    return ok;
}

void VEditWindow::readFile()
{
    VEditTab *editor = getTab(currentIndex());
    Q_ASSERT(editor);
    editor->readFile();
    noticeStatus(currentIndex());
}

void VEditWindow::saveAndReadFile()
{
    saveFile();
    readFile();
    noticeStatus(currentIndex());
}

void VEditWindow::editFile()
{
    VEditTab *editor = getTab(currentIndex());
    Q_ASSERT(editor);
    editor->editFile();
    noticeStatus(currentIndex());
}

void VEditWindow::saveFile()
{
    VEditTab *editor = getTab(currentIndex());
    Q_ASSERT(editor);
    editor->saveFile();
}

void VEditWindow::handleNotebookRenamed(const QVector<VNotebook> &notebooks,
                                        const QString &oldName, const QString &newName)
{
    QTabBar *tabs = tabBar();
    int nrTabs = tabs->count();
    for (int i = 0; i < nrTabs; ++i) {
        QJsonObject tabJson = tabs->tabData(i).toJsonObject();
        if (tabJson["notebook"] == oldName) {
            tabJson["notebook"] = newName;
            tabs->setTabData(i, tabJson);
        }
    }
}

void VEditWindow::noticeTabStatus(int index)
{
    if (index == -1) {
        emit tabStatusChanged("", "", false, false);
        return;
    }

    QJsonObject tabJson = tabBar()->tabData(index).toJsonObject();
    Q_ASSERT(!tabJson.isEmpty());

    QString notebook = tabJson["notebook"].toString();
    QString relativePath = tabJson["relative_path"].toString();
    VEditTab *editor = getTab(index);
    bool editMode = editor->getIsEditMode();
    bool modifiable = tabJson["modifiable"].toBool();

    emit tabStatusChanged(notebook, relativePath,
                          editMode, modifiable);
}

void VEditWindow::requestUpdateTabStatus()
{
    noticeTabStatus(currentIndex());
}

void VEditWindow::requestUpdateOutline()
{
    int idx = currentIndex();
    if (idx == -1) {
        emit outlineChanged(VToc());
        return;
    }
    getTab(idx)->requestUpdateOutline();
}

void VEditWindow::requestUpdateCurHeader()
{
    int idx = currentIndex();
    if (idx == -1) {
        emit curHeaderChanged(VAnchor());
        return;
    }
    getTab(idx)->requestUpdateCurHeader();
}

void VEditWindow::focusWindow()
{
    int idx = currentIndex();
    if (idx == -1) {
        setFocus();
        return;
    }
    getTab(idx)->focusTab();
}

void VEditWindow::handleTabbarClicked(int index)
{
    // The child will emit getFocused here
    focusWindow();
    noticeStatus(index);
}

void VEditWindow::mousePressEvent(QMouseEvent *event)
{
    emit getFocused();
    QTabWidget::mousePressEvent(event);
}

void VEditWindow::contextMenuRequested(QPoint pos)
{
    QMenu menu(this);

    menu.addAction(removeSplitAct);
    menu.exec(this->mapToGlobal(pos));
}

void VEditWindow::tabListJump(QAction *action)
{
    if (!action) {
        return;
    }

    QJsonObject tabJson = action->data().toJsonObject();
    int idx = findTabByFile(tabJson["notebook"].toString(),
                            tabJson["relative_path"].toString());
    Q_ASSERT(idx >= 0);
    setCurrentIndex(idx);
    noticeStatus(idx);
}

void VEditWindow::updateTabListMenu()
{
    // Re-generate the tab list menu
    QMenu *menu = leftBtn->menu();
    QList<QAction *> actions = menu->actions();
    int nrActions = actions.size();
    for (int i = 0; i < nrActions; ++i) {
        QAction *tmpAct = actions.at(i);
        menu->removeAction(tmpAct);
        tabListAct->removeAction(tmpAct);
        delete tmpAct;
    }

    QTabBar *tabbar = tabBar();
    int nrTab = tabbar->count();
    for (int i = 0; i < nrTab; ++i) {
        QAction *action = new QAction(tabbar->tabText(i), tabListAct);
        action->setData(tabbar->tabData(i));
        menu->addAction(action);
    }
}

void VEditWindow::handleOutlineChanged(const VToc &toc)
{
    // Only propagate it if it is current tab
    int idx = currentIndex();
    QJsonObject tabJson = tabBar()->tabData(idx).toJsonObject();
    Q_ASSERT(!tabJson.isEmpty());
    QString path = vnote->getNotebookPath(tabJson["notebook"].toString());
    path = QDir::cleanPath(QDir(path).filePath(tabJson["relative_path"].toString()));

    if (toc.filePath == path) {
        emit outlineChanged(toc);
    }
}

void VEditWindow::handleCurHeaderChanged(const VAnchor &anchor)
{
    // Only propagate it if it is current tab
    int idx = currentIndex();
    QJsonObject tabJson = tabBar()->tabData(idx).toJsonObject();
    Q_ASSERT(!tabJson.isEmpty());
    QString path = vnote->getNotebookPath(tabJson["notebook"].toString());
    path = QDir::cleanPath(QDir(path).filePath(tabJson["relative_path"].toString()));

    if (anchor.filePath == path) {
        emit curHeaderChanged(anchor);
    }
}

void VEditWindow::scrollCurTab(const VAnchor &anchor)
{
    int idx = currentIndex();
    QJsonObject tabJson = tabBar()->tabData(idx).toJsonObject();
    Q_ASSERT(!tabJson.isEmpty());
    QString path = vnote->getNotebookPath(tabJson["notebook"].toString());
    path = QDir::cleanPath(QDir(path).filePath(tabJson["relative_path"].toString()));

    if (path == anchor.filePath) {
        getTab(idx)->scrollToAnchor(anchor);
    }
}

void VEditWindow::noticeStatus(int index)
{
    noticeTabStatus(index);

    if (index == -1) {
        emit outlineChanged(VToc());
        emit curHeaderChanged(VAnchor());
    } else {
        VEditTab *tab = getTab(index);
        tab->requestUpdateOutline();
        tab->requestUpdateCurHeader();
    }
}