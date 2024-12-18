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

#include "application.hpp"

#include "../application/application_service.hpp"
#include "../application/language_service.hpp"
#include "../application/progress_manager.hpp"
#include "../application/recent_files_manager.hpp"
#include "../application/service_container.hpp"
#include "../application/settings_proxy.hpp"
#include "../application/state_machine.hpp"
#include "../common/constants.hpp"
#include "../domain/layout_optimizer.hpp"
#include "../infra/settings.hpp"
#include "../infra/version_checker.hpp"
#include "../view/dialogs/export/png_export_dialog.hpp"
#include "../view/dialogs/export/svg_export_dialog.hpp"
#include "../view/dialogs/layout_optimization_dialog.hpp"
#include "../view/dialogs/scene_color_dialog.hpp"
#include "../view/editor_view.hpp"
#include "../view/main_window.hpp"
#include "../view/node_action.hpp"

#include "argengine.hpp"
#include "simple_logger.hpp"

#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QStandardPaths>

using juzzlin::Argengine;
using juzzlin::L;

static const auto TAG = "Application";

Application::Application(int & argc, char ** argv)
  : m_application(argc, argv)
  , m_serviceContainer(std::make_unique<SC>())
  , m_stateMachine(new StateMachine { this }) // Parented to this
  , m_versionChecker(new VersionChecker { this }) // Parented to this
{
    parseArgs(argc, argv);

    initializeTranslations();

    // Instantiate components here because the possible language given
    // in the command line must have been loaded before this
    instantiateAndConnectComponents();

    initializeAndShowMainWindow();

    openGivenMindMapOrAutoloadRecentMindMap();

    checkForNewReleases();
}

void Application::connectComponents()
{
    // Connect views and StateMachine together
    connect(this, &Application::actionTriggered, m_stateMachine, &StateMachine::calculateState);
    connect(m_editorView, &EditorView::actionTriggered, m_stateMachine, [this](StateMachine::Action action) {
        m_stateMachine->calculateState(action);
    });
    connect(m_mainWindow.get(), &MainWindow::actionTriggered, m_stateMachine, &StateMachine::calculateState);
    connect(m_stateMachine, &StateMachine::stateChanged, this, &Application::runState);

    connect(m_mainWindow.get(), &MainWindow::gridVisibleChanged, m_editorView, [this](int state) {
        bool visible = state == Qt::Checked;
        m_editorView->setGridVisible(visible);
    });
}

void Application::instantiateComponents()
{
    m_mainWindow = std::make_unique<MainWindow>();
    m_serviceContainer->setMainWindow(m_mainWindow);

    m_editorView = new EditorView;
    m_editorView->setParent(m_mainWindow.get());
    m_serviceContainer->applicationService()->setEditorView(*m_editorView);
}

void Application::instantiateAndConnectComponents()
{
    instantiateComponents();

    connectComponents();
}

void Application::initializeAndShowMainWindow()
{
    m_mainWindow->initialize();
    m_mainWindow->appear();
}

void Application::openGivenMindMapOrAutoloadRecentMindMap()
{
    if (!m_mindMapFile.isEmpty()) {
        QTimer::singleShot(0, this, &Application::openArgMindMap);
    } else if (SC::instance().settingsProxy()->autoload()) {
        if (const auto recentFile = SC::instance().recentFilesManager()->recentFile(); recentFile.has_value()) {
            // Exploit same code as used to open arg mind map
            m_mindMapFile = recentFile.value();
            QTimer::singleShot(0, this, &Application::openArgMindMap);
        }
    }
}

void Application::checkForNewReleases()
{
    connect(m_versionChecker, &VersionChecker::newVersionFound, this, [this](Version version, QString downloadUrl) {
        m_serviceContainer->applicationService()->showStatusText(QString { tr("A new version %1 available at <a href='%2'>%2</a>") }.arg(version.toString(), downloadUrl));
    });
    m_versionChecker->checkForNewReleases();
}

QString Application::getFileDialogFileText() const
{
    return tr("Heimer Files") + " (*" + Constants::Application::fileExtension() + ")";
}

void Application::initializeTranslations()
{
    m_serviceContainer->languageService()->initializeTranslations(m_application);
}

std::string Application::buildAvailableLanguagesHelpString() const
{
    QStringList languageHelpStrings;
    for (auto && language : Constants::Application::supportedLanguages()) {
        languageHelpStrings << language.c_str();
    }
    return languageHelpStrings.join(", ").toStdString() + ".";
}

void Application::parseArgs(int argc, char ** argv)
{
    Argengine ae(argc, argv);

    ae.addOption(
      { "-d", "--debug" }, [] {
          L::setLoggingLevel(L::Level::Debug);
      },
      false, "Show debug logging.");

    ae.addOption(
      { "-t", "--trace" }, [] {
          L::setLoggingLevel(L::Level::Trace);
      },
      false, "Show trace logging.");

    ae.addOption(
      { "--lang" }, [this](std::string value) {
          if (!Constants::Application::supportedLanguages().count(value)) {
              L(TAG).error() << "Unsupported language: '" << value << "'";
          } else {
              m_serviceContainer->languageService()->setCommandLineLanguage(value.c_str());
          }
      },
      false, "Force language: " + buildAvailableLanguagesHelpString());

    ae.setPositionalArgumentCallback([=](Argengine::ArgumentVector args) {
        m_mindMapFile = args.at(0).c_str();
    });

    ae.setHelpText(std::string("\nUsage: ") + argv[0] + " [OPTIONS] [MIND_MAP_FILE]");

    ae.parse();
}

int Application::run()
{
    return m_application.exec();
}

void Application::runState(StateMachine::State state)
{
    switch (state) {
    case StateMachine::State::TryCloseWindow:
        m_mainWindow->saveWindowSize();
        m_mainWindow->close();
        break;
    case StateMachine::State::Exit:
        m_mainWindow->saveWindowSize();
        QApplication::exit(EXIT_SUCCESS);
        break;
    default:
    case StateMachine::State::Edit:
        m_mainWindow->setTitle();
        break;
    case StateMachine::State::InitializeNewMindMap:
        m_serviceContainer->applicationService()->initializeNewMindMap();
        break;
    case StateMachine::State::OpenRecent:
        doOpenMindMap(SC::instance().recentFilesManager()->selectedFile());
        break;
    case StateMachine::State::OpenDrop:
        doOpenMindMap(m_editorView->dropFile());
        break;
    case StateMachine::State::Save:
        saveMindMap();
        break;
    case StateMachine::State::ShowBackgroundColorDialog:
        showBackgroundColorDialog();
        break;
    case StateMachine::State::ShowEdgeColorDialog:
        showEdgeColorDialog();
        break;
    case StateMachine::State::ShowGridColorDialog:
        showGridColorDialog();
        break;
    case StateMachine::State::ShowNodeColorDialog:
        showNodeColorDialog();
        break;
    case StateMachine::State::ShowTextColorDialog:
        showTextColorDialog();
        break;
    case StateMachine::State::ShowImageFileDialog:
        showImageFileDialog();
        break;
    case StateMachine::State::ShowPngExportDialog:
        showPngExportDialog();
        break;
    case StateMachine::State::ShowLayoutOptimizationDialog:
        showLayoutOptimizationDialog();
        break;
    case StateMachine::State::ShowNotSavedDialog:
        switch (showNotSavedDialog()) {
        case QMessageBox::Save:
            emit actionTriggered(StateMachine::Action::NotSavedDialogAccepted);
            break;
        case QMessageBox::Discard:
            emit actionTriggered(StateMachine::Action::NotSavedDialogDiscarded);
            break;
        case QMessageBox::Cancel:
            emit actionTriggered(StateMachine::Action::NotSavedDialogCanceled);
            break;
        }
        break;
    case StateMachine::State::ShowSaveAsDialog:
        saveMindMapAs();
        break;
    case StateMachine::State::ShowSvgExportDialog:
        showSvgExportDialog();
        break;
    case StateMachine::State::ShowOpenDialog:
        openMindMap();
        break;
    }
}

void Application::updateProgress()
{
    SC::instance().progressManager()->updateProgress();
}

void Application::openArgMindMap()
{
    doOpenMindMap(m_mindMapFile);
}

void Application::openMindMap()
{
    L(TAG).debug() << "Open file";

    const auto path = Settings::Custom::loadRecentPath();
    if (const auto fileName = QFileDialog::getOpenFileName(m_mainWindow.get(), tr("Open File"), path, getFileDialogFileText()); !fileName.isEmpty()) {
        doOpenMindMap(fileName);
    } else {
        emit actionTriggered(StateMachine::Action::OpeningMindMapCanceled);
    }
}

void Application::doOpenMindMap(QString fileName)
{
    L(TAG).debug() << "Opening '" << fileName.toStdString();
    m_mainWindow->showSpinnerDialog(true, tr("Opening '%1'..").arg(fileName));
    updateProgress();
    if (m_serviceContainer->applicationService()->openMindMap(fileName)) {
        m_mainWindow->disableUndoAndRedo();
        updateProgress();
        m_mainWindow->setSaveActionStatesOnOpenedMindMap();
        updateProgress();
        Settings::Custom::saveRecentPath(fileName);
        updateProgress();
        m_mainWindow->showSpinnerDialog(false);
        updateProgress();
        emit actionTriggered(StateMachine::Action::MindMapOpened);
    } else {
        m_mainWindow->showSpinnerDialog(false);
        updateProgress();
        emit actionTriggered(StateMachine::Action::OpeningMindMapFailed);
    }
}

void Application::saveMindMap()
{
    L(TAG).debug() << "Save..";

    if (!m_serviceContainer->applicationService()->saveMindMap()) {
        const auto msg = QString(tr("Failed to save file."));
        L(TAG).error() << msg.toStdString();
        showMessageBox(msg);
        emit actionTriggered(StateMachine::Action::MindMapSaveFailed);
        return;
    }

    m_mainWindow->enableSave(false);
    emit actionTriggered(StateMachine::Action::MindMapSaved);
}

void Application::saveMindMapAs()
{
    L(TAG).debug() << "Save as..";

    QString fileName = QFileDialog::getSaveFileName(
      m_mainWindow.get(),
      tr("Save File As"),
      Settings::Custom::loadRecentPath(),
      getFileDialogFileText());

    if (fileName.isEmpty()) {
        emit actionTriggered(StateMachine::Action::MindMapSaveAsCanceled);
        return;
    }

    if (!fileName.endsWith(Constants::Application::fileExtension())) {
        fileName += Constants::Application::fileExtension();
    }

    if (m_serviceContainer->applicationService()->saveMindMapAs(fileName)) {
        const auto msg = QString(tr("File '")) + fileName + tr("' saved.");
        L(TAG).debug() << msg.toStdString();
        m_mainWindow->enableSave(false);
        Settings::Custom::saveRecentPath(fileName);
        emit actionTriggered(StateMachine::Action::MindMapSavedAs);
    } else {
        const auto msg = QString(tr("Failed to save file as '") + fileName + "'.");
        L(TAG).error() << msg.toStdString();
        showMessageBox(msg);
        emit actionTriggered(StateMachine::Action::MindMapSaveAsFailed);
    }
}

void Application::showBackgroundColorDialog()
{
    Dialogs::SceneColorDialog(Dialogs::ColorDialog::Role::Background).exec();
    emit actionTriggered(StateMachine::Action::BackgroundColorChanged);
}

void Application::showEdgeColorDialog()
{
    if (Dialogs::SceneColorDialog(Dialogs::ColorDialog::Role::Edge).exec() != QDialog::Accepted) {
        // Clear implicitly selected edges on cancel
        m_serviceContainer->applicationService()->clearEdgeSelectionGroup(true);
    }
    emit actionTriggered(StateMachine::Action::EdgeColorChanged);
}

void Application::showGridColorDialog()
{
    Dialogs::SceneColorDialog(Dialogs::ColorDialog::Role::Grid).exec();
    emit actionTriggered(StateMachine::Action::GridColorChanged);
}

void Application::showNodeColorDialog()
{
    if (Dialogs::SceneColorDialog(Dialogs::ColorDialog::Role::Node).exec() != QDialog::Accepted) {
        // Clear implicitly selected nodes on cancel
        m_serviceContainer->applicationService()->clearNodeSelectionGroup(true);
    }
    emit actionTriggered(StateMachine::Action::NodeColorChanged);
}

void Application::showTextColorDialog()
{
    if (Dialogs::SceneColorDialog(Dialogs::ColorDialog::Role::Text).exec() != QDialog::Accepted) {
        // Clear implicitly selected nodes on cancel
        m_serviceContainer->applicationService()->clearNodeSelectionGroup(true);
    }
    emit actionTriggered(StateMachine::Action::TextColorChanged);
}

void Application::showImageFileDialog()
{
    const auto path = Settings::Custom::loadRecentImagePath();
    const auto extensions = "(*.jpg *.jpeg *.JPG *.JPEG *.png *.PNG)";
    const auto fileName = QFileDialog::getOpenFileName(
      m_mainWindow.get(), tr("Open an image"), path, tr("Image Files") + " " + extensions);

    if (QImage image; image.load(fileName)) {
        m_serviceContainer->applicationService()->performNodeAction({ NodeAction::Type::AttachImage, image, fileName });
        Settings::Custom::saveRecentImagePath(fileName);
    } else if (fileName != "") {
        QMessageBox::critical(m_mainWindow.get(), tr("Load image"), tr("Failed to load image '") + fileName + "'");
    }
}

void Application::showPngExportDialog()
{
    Dialogs::Export::PngExportDialog pngExportDialog { *m_mainWindow };

    connect(&pngExportDialog, &Dialogs::Export::PngExportDialog::pngExportRequested, m_serviceContainer->applicationService().get(), &ApplicationService::exportToPng);
    connect(m_serviceContainer->applicationService().get(), &ApplicationService::pngExportFinished, &pngExportDialog, &Dialogs::Export::PngExportDialog::finishExport);

    pngExportDialog.setCurrentMindMapFileName(m_serviceContainer->applicationService()->fileName());
    pngExportDialog.setDefaultImageSize(m_serviceContainer->applicationService()->calculateExportImageSize());
    pngExportDialog.exec();

    // Doesn't matter if canceled or not
    emit actionTriggered(StateMachine::Action::PngExported);
}

void Application::showSvgExportDialog()
{
    Dialogs::Export::SvgExportDialog svgExportDialog { *m_mainWindow };

    connect(&svgExportDialog, &Dialogs::Export::SvgExportDialog::svgExportRequested, m_serviceContainer->applicationService().get(), &ApplicationService::exportToSvg);
    connect(m_serviceContainer->applicationService().get(), &ApplicationService::svgExportFinished, &svgExportDialog, &Dialogs::Export::SvgExportDialog::finishExport);

    svgExportDialog.setCurrentMindMapFileName(m_serviceContainer->applicationService()->fileName());
    svgExportDialog.exec();

    // Doesn't matter if canceled or not
    emit actionTriggered(StateMachine::Action::SvgExported);
}

void Application::showLayoutOptimizationDialog()
{
    LayoutOptimizer layoutOptimizer { m_serviceContainer->applicationService()->mindMapData(), m_editorView->grid() };
    Dialogs::LayoutOptimizationDialog dialog { *m_mainWindow, *m_serviceContainer->applicationService()->mindMapData(), layoutOptimizer, *m_editorView };
    connect(&dialog, &Dialogs::LayoutOptimizationDialog::undoPointRequested, m_serviceContainer->applicationService().get(), &ApplicationService::saveUndoPoint);

    if (dialog.exec() == QDialog::Accepted) {
        m_serviceContainer->applicationService()->zoomToFit();
    }

    emit actionTriggered(StateMachine::Action::LayoutOptimized);
}

void Application::showMessageBox(QString message)
{
    QMessageBox msgBox(m_mainWindow.get());
    msgBox.setText(message);
    msgBox.exec();
}

int Application::showNotSavedDialog()
{
    QMessageBox msgBox(m_mainWindow.get());
    msgBox.setText(tr("The mind map has been modified."));
    msgBox.setInformativeText(tr("Do you want to save your changes?"));
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    return msgBox.exec();
}

Application::~Application() = default;
