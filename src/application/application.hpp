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

#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "../common/types.hpp"
#include "state_machine.hpp"

#include <QApplication>
#include <QColor>
#include <QObject>

#include <memory>

class EditorView;
class ImageManager;
class MainWindow;
class VersionChecker;

class Application : public QObject
{
    Q_OBJECT

public:
    Application(int & argc, char ** argv);

    ~Application() override;

    int run();

public slots:

    void runState(StateMachine::State state);

signals:

    void actionTriggered(StateMachine::Action action);

    void backgroundColorChanged(QColor color);

private:
    QStringList userLanguageOrAvailableSystemUiLanguages() const;

    std::string buildAvailableLanguagesHelpString() const;

    void checkForNewReleases();

    void connectComponents();

    void doOpenMindMap(QString fileName);

    QString getFileDialogFileText() const;

    void initializeTranslations();

    void instantiateComponents();

    void instantiateAndConnectComponents();

    void openArgMindMap();

    void openGivenMindMapOrAutoloadRecentMindMap();

    void openMindMap();

    void saveMindMap();

    void saveMindMapAs();

    void showBackgroundColorDialog();

    void showEdgeColorDialog();

    void showGridColorDialog();

    void showImageFileDialog();

    void showLayoutOptimizationDialog();

    void showNodeColorDialog();

    void showPngExportDialog();

    void showSvgExportDialog();

    void showTextColorDialog();

    void showMessageBox(QString message);

    void initializeAndShowMainWindow();

    int showNotSavedDialog();

    void parseArgs(int argc, char ** argv);

    void updateProgress();

    QApplication m_application;

    MainWindowS m_mainWindow;

    std::unique_ptr<SC> m_serviceContainer;

    StateMachine * m_stateMachine;

    QString m_mindMapFile;

    EditorView * m_editorView = nullptr;

    VersionChecker * m_versionChecker;
};

#endif // APPLICATION_HPP
