// This file is part of Heimer.
// Copyright (C) 2018 Jussi Lind <jussi.lind@iki.fi>
//
// Heimer is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// Heimer is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Heimer. If not, see <http://www.gnu.org/licenses/>.

#include "editor_scene.hpp"

#include "../common/constants.hpp"
#include "magic_zoom.hpp"

#include "scene_items/edge.hpp"
#include "scene_items/edge_text_edit.hpp"
#include "scene_items/node.hpp"

#include "simple_logger.hpp"

#include <QGraphicsEffect>
#include <QGraphicsLineItem>
#include <QPainter>
#include <QtSvg/QSvgGenerator>

static const auto TAG = "EditorScene";

EditorScene::EditorScene()
{
    setSceneRect(-m_initialSize, -m_initialSize, m_initialSize * 2, m_initialSize * 2);

    connect(this, &QGraphicsScene::focusItemChanged, this, [](QGraphicsItem *, QGraphicsItem * oldFocus, Qt::FocusReason) {
        if (const auto edgeTextEdit = dynamic_cast<SceneItems::EdgeTextEdit *>(oldFocus); edgeTextEdit) {
            edgeTextEdit->updateDueToLostFocus();
        }
    });
}

void EditorScene::adjustSceneRect()
{
    while (!containsAll()) {
        setSceneRect(sceneRect().adjusted(-m_initialSize, -m_initialSize, m_initialSize, m_initialSize));
        juzzlin::L(TAG).debug() << "New scene rect: " << sceneRect().x() << " " << sceneRect().y() << " " << sceneRect().width() << " " << sceneRect().height();
    }
}

QRectF EditorScene::calculateZoomToFitRectangle(bool isForExport) const
{
    return MagicZoom::calculateRectangleByItems(items(), isForExport);
}

QRectF EditorScene::calculateZoomToFitRectangleByNodes(const std::vector<NodeP> & nodes) const
{
    return MagicZoom::calculateRectangleByNodes(nodes, false);
}

bool EditorScene::containsAll() const
{
    const double marginFactor = .25;
    const auto testMarginX = sceneRect().width() * marginFactor;
    const auto testMarginY = sceneRect().height() * marginFactor;
    const auto testRect = sceneRect().adjusted(testMarginX, testMarginY, -testMarginX, -testMarginY);
    for (auto && item : items()) {
        if (const auto node = dynamic_cast<NodeP>(item); node && !testRect.contains(item->sceneBoundingRect())) {
            return false;
        }
    }
    return true;
}

void EditorScene::enableGraphicsEffects(bool enable) const
{
    for (auto && item : items()) {
        if (item->graphicsEffect()) {
            item->graphicsEffect()->setEnabled(enable);
        }
    }
}

bool EditorScene::hasEdge(NodeR node0, NodeR node1)
{
    for (auto && item : items()) {
        if (const auto edge = dynamic_cast<EdgeP>(item); edge && edge->sourceNode().index() == node0.index() && edge->targetNode().index() == node1.index()) {
            return true;
        }
    }
    return false;
}

void EditorScene::removeItems()
{
    // We don't want the scene to destroy the items as they are managed elsewhere
    for (auto && item : items()) {
        removeItem(item);
    }
}

QImage EditorScene::toImage(QSize size, QColor backgroundColor, bool transparentBackground)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(transparentBackground ? Qt::transparent : backgroundColor);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    render(&painter);

    return image;
}

void EditorScene::toSvg(QString filename, QString title)
{
    // Need to disable effects in order to get vectorized SVG.
    // Otherwise all items will be just bitmapped.
    enableGraphicsEffects(false);

    QSvgGenerator generator;
    generator.setFileName(filename);
    generator.setSize(QSize(static_cast<int>(width()), static_cast<int>(height())));
    generator.setViewBox(QRect(0, 0, static_cast<int>(width()), static_cast<int>(height())));
    generator.setTitle(title);
    generator.setDescription(QString { "SVG exported from %1 version %2" }.arg(Constants::Application::applicationName(), Constants::Application::applicationVersion()));

    QPainter painter;
    painter.begin(&generator);
    render(&painter);
    painter.end();

    enableGraphicsEffects(true);
}

EditorScene::~EditorScene()
{
    removeItems();

    juzzlin::L(TAG).debug() << "EditorScene deleted";
}
