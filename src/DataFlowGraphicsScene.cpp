#include "DataFlowGraphicsScene.hpp"

#include "ConnectionGraphicsObject.hpp"
#include "GraphicsView.hpp"
#include "NodeDelegateModelRegistry.hpp"
#include "NodeGraphicsObject.hpp"
#include "UndoCommands.hpp"

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsSceneMoveEvent>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QWidgetAction>

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtGlobal>

#include <stdexcept>
#include <utility>

namespace QtNodes {

DataFlowGraphicsScene::DataFlowGraphicsScene(DataFlowGraphModel &graphModel, QObject *parent)
    : BasicGraphicsScene(graphModel, parent)
    , _graphModel(graphModel)
{
    connect(&_graphModel,
            &DataFlowGraphModel::inPortDataWasSet,
            [this](NodeId const nodeId, PortType const, PortIndex const) { onNodeUpdated(nodeId); });
}

// TODO constructor for an empyt scene?

std::vector<NodeId> DataFlowGraphicsScene::selectedNodes() const
{
    QList<QGraphicsItem *> graphicsItems = selectedItems();

    std::vector<NodeId> result;
    result.reserve(graphicsItems.size());

    for (QGraphicsItem *item : graphicsItems) {
        auto ngo = qgraphicsitem_cast<NodeGraphicsObject *>(item);

        if (ngo != nullptr) {
            result.push_back(ngo->nodeId());
        }
    }

    return result;
}

QMenu *DataFlowGraphicsScene::createSceneMenu(QPointF const scenePos)
{
    QMenu *modelMenu = new QMenu();

    // Add filterbox to the context menu
    auto *txtBox = new QLineEdit(modelMenu);
    txtBox->setPlaceholderText(QStringLiteral("Filter"));
    txtBox->setClearButtonEnabled(true);

    auto *txtBoxAction = new QWidgetAction(modelMenu);
    txtBoxAction->setDefaultWidget(txtBox);

    // 1.
    modelMenu->addAction(txtBoxAction);

    // Add result treeview to the context menu
    QTreeWidget *treeView = new QTreeWidget(modelMenu);
    treeView->header()->close();

    auto *treeViewAction = new QWidgetAction(modelMenu);
    treeViewAction->setDefaultWidget(treeView);

    // 2.
    modelMenu->addAction(treeViewAction);

    auto registry = _graphModel.dataModelRegistry();

    for (auto const &cat : registry->categories()) {

        QTreeWidgetItem *cateItem = nullptr;
        auto const& catPath = cat.split(".");
        for(auto const& c : catPath)
        {
            QTreeWidgetItem *item = nullptr;
            if(cateItem)
            {
                item = new QTreeWidgetItem(cateItem);
            }
            else
            {
                item = new QTreeWidgetItem(treeView);
            }
            item->setText(0, c);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

            cateItem = item;
        }

    }

    for (auto const &assoc : registry->registeredModelsCategoryAssociation()) {

        const auto find_path = assoc.second.split(".");

        QTreeWidgetItemIterator parent(treeView);
        while (*parent) {
            QStringList currentItemPath;
            QTreeWidgetItem *currentItem = *parent;
            while (currentItem) {
                currentItemPath.prepend(currentItem->text(0));
                currentItem = currentItem->parent();
            }

            if (currentItemPath == find_path) {
                break;
            }

            ++parent;
        }

        if (not *parent)
            continue;

        auto item = new QTreeWidgetItem(*parent);
        item->setText(0, assoc.first);
    }

    treeView->expandAll();

    connect(treeView,
            &QTreeWidget::itemClicked,
            [this, modelMenu, scenePos](QTreeWidgetItem *item, int) {
                if (!(item->flags() & (Qt::ItemIsSelectable))) {
                    return;
                }

                this->undoStack().push(new CreateCommand(this, item->text(0), scenePos));

                modelMenu->close();
            });

    //Setup filtering
    connect(txtBox, &QLineEdit::textChanged, [treeView](const QString &text) {
        QTreeWidgetItemIterator categoryIt(treeView, QTreeWidgetItemIterator::HasChildren);
        while (*categoryIt)
            (*categoryIt++)->setHidden(true);
        QTreeWidgetItemIterator it(treeView, QTreeWidgetItemIterator::NoChildren);
        while (*it) {
            auto modelName = (*it)->text(0);
            const bool match = (modelName.contains(text, Qt::CaseInsensitive));
            (*it)->setHidden(!match);
            if (match) {
                QTreeWidgetItem *parent = (*it)->parent();
                while (parent) {
                    parent->setHidden(false);
                    parent = parent->parent();
                }
            }
            ++it;
        }
    });

    // make sure the text box gets focus so the user doesn't have to click on it
    txtBox->setFocus();

    // QMenu's instance auto-destruction
    modelMenu->setAttribute(Qt::WA_DeleteOnClose);

    return modelMenu;
}

void DataFlowGraphicsScene::save() const
{
    QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    QDir::homePath(),
                                                    tr("Flow Scene Files (*.flow)"));

    if (!fileName.isEmpty()) {
        if (!fileName.endsWith("flow", Qt::CaseInsensitive))
            fileName += ".flow";

        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(_graphModel.save()).toJson());
        }
    }
}

void DataFlowGraphicsScene::load()
{
    QString fileName = QFileDialog::getOpenFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    QDir::homePath(),
                                                    tr("Flow Scene Files (*.flow)"));

    if (!QFileInfo::exists(fileName))
        return;

    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly))
        return;

    clearScene();

    QByteArray const wholeFile = file.readAll();

    _graphModel.load(QJsonDocument::fromJson(wholeFile).object());

    Q_EMIT sceneLoaded();
}

} // namespace QtNodes
