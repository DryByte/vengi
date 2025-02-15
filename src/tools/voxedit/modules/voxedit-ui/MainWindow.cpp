/**
 * @file
 */

#include "MainWindow.h"
#include "ScopedStyle.h"
#include "Viewport.h"
#include "Util.h"
#include "command/CommandHandler.h"
#include "core/StringUtil.h"
#include "core/ArrayLength.h"
#include "core/Log.h"
#include "core/collection/DynamicArray.h"
#include "io/FormatDescription.h"
#include "ui/IconsForkAwesome.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/IMGUIApp.h"
#include "ui/FileDialog.h"
#include "ui/IMGUIEx.h"
#include "voxedit-util/Config.h"
#include "voxedit-util/SceneManager.h"
#include "voxedit-util/modifier/Modifier.h"
#include "voxelformat/VolumeFormat.h"
#include <glm/gtc/type_ptr.hpp>

#define TITLE_STATUSBAR "##statusbar"
#define TITLE_PALETTE ICON_FA_PALETTE " Palette##title"
#define TITLE_POSITIONS ICON_FA_LOCATION_CROSSHAIRS " Positions##title"
#define TITLE_ANIMATION_TIMELINE ICON_FA_TABLE_LIST " Animation##animationtimeline"
#define TITLE_TOOLS ICON_FA_TOOLBOX " Tools##title"
#define TITLE_MEMENTO ICON_FA_BOOK_OPEN " History##title"
#define TITLE_ASSET ICON_FA_LIST_UL " Assets##title"
#define TITLE_SCENEGRAPH ICON_FA_SHARE_NODES " Scene##title"
#define TITLE_MODIFIERS ICON_FA_TOOLBOX " Modifiers##title"
#define TITLE_TREES ICON_FA_TREE " Trees##title"
#define TITLE_SCRIPTPANEL ICON_FA_CODE " Script##title"
#define TITLE_LSYSTEMPANEL ICON_FA_LEAF " L-System##title"
#define TITLE_ANIMATION_SETTINGS ICON_FA_ARROWS_SPIN " Animation##animationsettings"
#define TITLE_SCRIPT_EDITOR ICON_FK_CODE " Script Editor##scripteditor"

#define POPUP_TITLE_UNSAVED "Unsaved Modifications##popuptitle"
#define POPUP_TITLE_NEW_SCENE "New scene##popuptitle"
#define POPUP_TITLE_FAILED_TO_SAVE "Failed to save##popuptitle"
#define POPUP_TITLE_UNSAVED_SCENE "Unsaved scene##popuptitle"
#define POPUP_TITLE_SCENE_SETTINGS "Scene settings##popuptitle"
#define POPUP_TITLE_MODEL_NODE_SETTINGS "Model settings##popuptitle"

namespace voxedit {

MainWindow::MainWindow(ui::IMGUIApp *app) : _app(app), _assetPanel(app->filesystem()) {
}

MainWindow::~MainWindow() {
	shutdownScenes();
}

void MainWindow::loadLastOpenedFiles(const core::String &string) {
	core::DynamicArray<core::String> tokens;
	core::string::splitString(string, tokens, ";");
	for (const core::String& s : tokens) {
		_lastOpenedFilesRingBuffer.push_back(s);
	}
}

void MainWindow::addLastOpenedFile(const core::String &file) {
	for (const core::String& s : _lastOpenedFilesRingBuffer) {
		if (s == file) {
			return;
		}
	}
	_lastOpenedFilesRingBuffer.push_back(file);
	core::String str;
	for (const core::String& s : _lastOpenedFilesRingBuffer) {
		if (!str.empty()) {
			str.append(";");
		}
		str.append(s);
	}
	_lastOpenedFiles->setVal(str);
}

void MainWindow::shutdownScenes() {
	for (size_t i = 0; i < _scenes.size(); ++i) {
		delete _scenes[i];
	}
	_scenes.clear();
	_lastHoveredScene = nullptr;
}

bool MainWindow::initScenes() {
	shutdownScenes();

	if (_simplifiedView->boolVal()) {
		_scenes.resize(2);
		_scenes[0] = new Viewport(0, false, false);
		_scenes[1] = new Viewport(1, true, false);
	} else {
		_scenes.resize(_numViewports->intVal());
		bool sceneMode = true;
		for (int i = 0; i < _numViewports->intVal(); ++i) {
			_scenes[i] = new Viewport(i, sceneMode, true);
			sceneMode = false;
		}
	}
	bool success = true;
	for (size_t i = 0; i < _scenes.size(); ++i) {
		if (!_scenes[i]->init()) {
			Log::error("Failed to initialize scene %i", (int)i);
			success = false;
		}
	}
	_lastHoveredScene = _scenes[0];

	_simplifiedView->markClean();
	_numViewports->markClean();
	return success;
}

bool MainWindow::init() {
	_simplifiedView = core::Var::getSafe(cfg::VoxEditSimplifiedView);
	_numViewports = core::Var::getSafe(cfg::VoxEditViewports);

	if (!initScenes()) {
		return false;
	}

	_sceneGraphPanel.init();
	_lsystemPanel.init();
	_treePanel.init();

	_lastOpenedFile = core::Var::getSafe(cfg::VoxEditLastFile);
	_lastOpenedFiles = core::Var::getSafe(cfg::VoxEditLastFiles);
	loadLastOpenedFiles(_lastOpenedFiles->strVal());

	SceneManager &mgr = sceneMgr();
	voxel::Region region = _modelNodeSettings.region();
	if (!region.isValid()) {
		_modelNodeSettings.reset();
		region = _modelNodeSettings.region();
	}
	if (!mgr.newScene(true, _modelNodeSettings.name, region)) {
		return false;
	}
	afterLoad("");
	return true;
}

void MainWindow::shutdown() {
	for (size_t i = 0; i < _scenes.size(); ++i) {
		_scenes[i]->shutdown();
	}
	_lsystemPanel.shutdown();
	_treePanel.shutdown();
}

bool MainWindow::save(const core::String &file, const io::FormatDescription *desc) {
	io::FileDescription fd;
	const core::String &ext = core::string::extractExtension(file);
	if (ext.empty()) {
		core::String newExt = "vengi";
		if (desc && !desc->exts.empty()) {
			newExt = desc->exts[0];
		}
		fd.set(file + "." + newExt, desc);
	} else {
		fd.set(file, desc);
	}
	if (!sceneMgr().save(fd)) {
		Log::warn("Failed to save the model");
		_popupFailedToSave = true;
		return false;
	}
	Log::info("Saved the model to %s", fd.c_str());
	_lastOpenedFile->setVal(fd.name);
	return true;
}

bool MainWindow::load(const core::String &file, const io::FormatDescription *desc) {
	if (file.empty()) {
		_app->openDialog([this] (const core::String file, const io::FormatDescription *desc) { load(file, desc); }, {}, voxelformat::voxelLoad());
		return true;
	}

	if (!sceneMgr().dirty()) {
		io::FileDescription fd;
		fd.set(file, desc);
		if (sceneMgr().load(fd)) {
			afterLoad(file);
			return true;
		}
		return false;
	}

	_loadFile.set(file, desc);
	_popupUnsaved = true;
	return false;
}

void MainWindow::onNewScene() {
	resetCamera();
	_animationTimeline.resetFrames();
}

void MainWindow::afterLoad(const core::String &file) {
	_lastOpenedFile->setVal(file);
	resetCamera();
}

bool MainWindow::createNew(bool force) {
	if (!force && sceneMgr().dirty()) {
		_loadFile.clear();
		_popupUnsaved = true;
	} else {
		_popupNewScene = true;
	}
	return false;
}

bool MainWindow::isSceneGraphDropTarget() const {
	return _sceneGraphPanel.hasFocus();
}

bool MainWindow::isPaletteWidgetDropTarget() const {
	return _palettePanel.hasFocus();
}

// left side

void MainWindow::configureLeftTopWidgetDock(ImGuiID dockId) {
	ImGui::DockBuilderDockWindow(TITLE_PALETTE, dockId);
}

void MainWindow::configureLeftBottomWidgetDock(ImGuiID dockId) {
	ImGui::DockBuilderDockWindow(TITLE_MODIFIERS, dockId);
}

void MainWindow::leftWidget() {
	_palettePanel.update(TITLE_PALETTE, _lastExecutedCommand);
	_modifierPanel.update(TITLE_MODIFIERS, _lastExecutedCommand);
}

// end of left side

// main space

void MainWindow::configureMainTopWidgetDock(ImGuiID dockId) {
	for (int i = 0; i < cfg::MaxViewports; ++i) {
		ImGui::DockBuilderDockWindow(Viewport::viewportId(i).c_str(), dockId);
	}
}

void MainWindow::configureMainBottomWidgetDock(ImGuiID dockId) {
	ImGui::DockBuilderDockWindow(TITLE_SCRIPT_EDITOR, dockId);
	ImGui::DockBuilderDockWindow(TITLE_ANIMATION_TIMELINE, dockId);
}

void MainWindow::mainWidget() {
	// main
	Viewport *scene = hoveredScene();
	if (scene != nullptr) {
		_lastHoveredScene = scene;
	}
	for (size_t i = 0; i < _scenes.size(); ++i) {
		_scenes[i]->update(&_lastExecutedCommand);
	}

	// bottom
	_scriptPanel.updateEditor(TITLE_SCRIPT_EDITOR, _app);
	if (isSceneMode()) {
		_animationTimeline.update(TITLE_ANIMATION_TIMELINE, _app->deltaFrameSeconds());
	}
}

bool MainWindow::isSceneMode() const {
	for (size_t i = 0; i < _scenes.size(); ++i) {
		if (_scenes[i]->isSceneMode()) {
			return true;
		}
	}
	return false;
}

// end of main space

// right side

void MainWindow::configureRightTopWidgetDock(ImGuiID dockId) {
	ImGui::DockBuilderDockWindow(TITLE_POSITIONS, dockId);
	ImGui::DockBuilderDockWindow(TITLE_TOOLS, dockId);
	ImGui::DockBuilderDockWindow(TITLE_ASSET, dockId);
	ImGui::DockBuilderDockWindow(TITLE_ANIMATION_SETTINGS, dockId);
	ImGui::DockBuilderDockWindow(TITLE_MEMENTO, dockId);
}

void MainWindow::configureRightBottomWidgetDock(ImGuiID dockId) {
	ImGui::DockBuilderDockWindow(TITLE_SCENEGRAPH, dockId);
	ImGui::DockBuilderDockWindow(TITLE_TREES, dockId);
	ImGui::DockBuilderDockWindow(TITLE_LSYSTEMPANEL, dockId);
	ImGui::DockBuilderDockWindow(TITLE_SCRIPTPANEL, dockId);
}

void MainWindow::rightWidget() {
	if (const Viewport *viewport = hoveredScene()) {
		_lastSceneMode = viewport->isSceneMode();
	}
	// top
	_positionsPanel.update(TITLE_POSITIONS, _lastSceneMode, _lastExecutedCommand);
	_toolsPanel.update(TITLE_TOOLS, _lastSceneMode, _lastExecutedCommand);
	_assetPanel.update(TITLE_ASSET, _lastSceneMode, _lastExecutedCommand);
	_animationPanel.update(TITLE_ANIMATION_SETTINGS, _lastExecutedCommand, &_animationTimeline);
	_mementoPanel.update(TITLE_MEMENTO, _lastExecutedCommand);

	// bottom
	_sceneGraphPanel.update(_lastHoveredScene->camera(), TITLE_SCENEGRAPH, &_modelNodeSettings, _lastExecutedCommand);
	if (!_simplifiedView->boolVal()) {
		_treePanel.update(TITLE_TREES);
		_lsystemPanel.update(TITLE_LSYSTEMPANEL);
		_scriptPanel.update(TITLE_SCRIPTPANEL);
	}
}

// end of right side

void MainWindow::dialog(const char *icon, const char *text) {
	ImGui::AlignTextToFramePadding();
	ImGui::PushFont(_app->bigFont());
	ImGui::TextUnformatted(icon);
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::Spacing();
	ImGui::SameLine();
	ImGui::TextUnformatted(text);
	ImGui::Spacing();
	ImGui::Separator();
}

void MainWindow::registerPopups() {
	if (_popupUnsaved) {
		ImGui::OpenPopup(POPUP_TITLE_UNSAVED);
		_popupUnsaved = false;
	}
	if (_popupNewScene) {
		ImGui::OpenPopup(POPUP_TITLE_NEW_SCENE);
		_popupNewScene = false;
	}
	if (_popupFailedToSave) {
		ImGui::OpenPopup(POPUP_TITLE_FAILED_TO_SAVE);
		_popupFailedToSave = false;
	}
	if (_menuBar._popupSceneSettings) {
		ImGui::OpenPopup(POPUP_TITLE_SCENE_SETTINGS);
		_menuBar._popupSceneSettings = false;
	}
	if (_popupUnsavedChangesQuit) {
		ImGui::OpenPopup(POPUP_TITLE_UNSAVED_SCENE);
		_popupUnsavedChangesQuit = false;
	}
	if (_sceneGraphPanel._popupNewModelNode) {
		ImGui::OpenPopup(POPUP_TITLE_MODEL_NODE_SETTINGS);
		_sceneGraphPanel._popupNewModelNode = false;
	}

	if (ImGui::BeginPopupModal(POPUP_TITLE_MODEL_NODE_SETTINGS, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Name");
		ImGui::Separator();
		ImGui::InputText("##modelsettingsname", &_modelNodeSettings.name);
		ImGui::NewLine();

		ImGui::Text("Position");
		ImGui::Separator();
		veui::InputAxisInt(math::Axis::X, "##posx", &_modelNodeSettings.position.x);
		veui::InputAxisInt(math::Axis::Y, "##posy", &_modelNodeSettings.position.y);
		veui::InputAxisInt(math::Axis::Z, "##posz", &_modelNodeSettings.position.z);
		ImGui::NewLine();

		ImGui::Text("Size");
		ImGui::Separator();
		veui::InputAxisInt(math::Axis::X, "Width##sizex", &_modelNodeSettings.size.x);
		veui::InputAxisInt(math::Axis::Y, "Height##sizey", &_modelNodeSettings.size.y);
		veui::InputAxisInt(math::Axis::Z, "Depth##sizez", &_modelNodeSettings.size.z);
		ImGui::NewLine();

		if (ImGui::Button(ICON_FA_CHECK " OK##modelsettings")) {
			ImGui::CloseCurrentPopup();
			scenegraph::SceneGraphNode node;
			voxel::RawVolume* v = new voxel::RawVolume(_modelNodeSettings.region());
			node.setVolume(v, true);
			node.setName(_modelNodeSettings.name.c_str());
			sceneMgr().addNodeToSceneGraph(node, _modelNodeSettings.parent);
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_XMARK " Cancel##modelsettings")) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup(POPUP_TITLE_SCENE_SETTINGS, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("Scene settings");
		ImGui::Separator();

		ImGui::ColorEdit3Var("Diffuse color", cfg::VoxEditDiffuseColor);
		ImGui::ColorEdit3Var("Ambient color", cfg::VoxEditAmbientColor);

		if (ImGui::Button(ICON_FA_CHECK " Done##scenesettings")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal(POPUP_TITLE_UNSAVED, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		dialog(ICON_FA_QUESTION, "There are unsaved modifications.\nDo you wish to discard them?");
		if (ImGui::Button(ICON_FA_CHECK " Yes##unsaved")) {
			ImGui::CloseCurrentPopup();
			if (!_loadFile.empty()) {
				sceneMgr().load(_loadFile);
				afterLoad(_loadFile.name);
			} else {
				createNew(true);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_XMARK " No##unsaved")) {
			ImGui::CloseCurrentPopup();
			_loadFile.clear();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal(POPUP_TITLE_UNSAVED_SCENE, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		dialog(ICON_FA_QUESTION, "Unsaved changes - are you sure to quit?");
		if (ImGui::Button(ICON_FA_CHECK " OK##unsavedscene")) {
			_forceQuit = true;
			_app->requestQuit();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_XMARK " Cancel##unsavedscene")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup(POPUP_TITLE_FAILED_TO_SAVE, ImGuiWindowFlags_AlwaysAutoResize)) {
		dialog(ICON_FA_TRIANGLE_EXCLAMATION, "Failed to save the model!");
		if (ImGui::Button(ICON_FA_CHECK " OK##failedsave")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal(POPUP_TITLE_NEW_SCENE, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Name");
		ImGui::Separator();
		ImGui::InputText("##newscenename", &_modelNodeSettings.name);
		ImGui::NewLine();

		ImGui::Text("Position");
		ImGui::Separator();
		veui::InputAxisInt(math::Axis::X, "##posx", &_modelNodeSettings.position.x);
		veui::InputAxisInt(math::Axis::Y, "##posy", &_modelNodeSettings.position.y);
		veui::InputAxisInt(math::Axis::Z, "##posz", &_modelNodeSettings.position.z);
		ImGui::NewLine();

		ImGui::Text("Size");
		ImGui::Separator();
		veui::InputAxisInt(math::Axis::X, "Width##sizex", &_modelNodeSettings.size.x);
		veui::InputAxisInt(math::Axis::Y, "Height##sizey", &_modelNodeSettings.size.y);
		veui::InputAxisInt(math::Axis::Z, "Depth##sizez", &_modelNodeSettings.size.z);
		ImGui::NewLine();

		if (ImGui::Button(ICON_FA_CHECK " OK##newscene")) {
			ImGui::CloseCurrentPopup();
			const voxel::Region &region = _modelNodeSettings.region();
			if (voxedit::sceneMgr().newScene(true, _modelNodeSettings.name, region)) {
				afterLoad("");
			}
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_XMARK " Close##newscene")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

QuitDisallowReason MainWindow::allowToQuit() {
	if (_forceQuit) {
		return QuitDisallowReason::None;
	}
	if (voxedit::sceneMgr().dirty()) {
		_popupUnsavedChangesQuit = true;
		return QuitDisallowReason::UnsavedChanges;
	}
	return QuitDisallowReason::None;
}

void MainWindow::update() {
	core_trace_scoped(MainWindow);
	if (_simplifiedView->isDirty() || _numViewports->isDirty()) {
		if (!initScenes()) {
			Log::error("Failed to update scenes");
		}
	}

	ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float statusBarHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.y * 2.0f;

	if (_lastOpenedFile->isDirty()) {
		_lastOpenedFile->markClean();
		addLastOpenedFile(_lastOpenedFile->strVal());
	}

	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - statusBarHeight));
	ImGui::SetNextWindowViewport(viewport->ID);
	{
		ui::ScopedStyle style;
		style.setWindowRounding(0.0f);
		style.setWindowBorderSize(0.0f);
		style.setWindowPadding(ImVec2(0.0f, 0.0f));
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		windowFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
		windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoMove;
		windowFlags |= ImGuiWindowFlags_NoTitleBar;
		if (sceneMgr().dirty()) {
			windowFlags |= ImGuiWindowFlags_UnsavedDocument;
		}

		core::String windowTitle = core::string::extractFilenameWithExtension(_lastOpenedFile->strVal());
		if (windowTitle.empty()) {
			windowTitle = _app->appname();
		} else {
			windowTitle.append(" - ");
			windowTitle.append(_app->appname());
		}
		windowTitle.append("###app");
		static bool keepRunning = true;
		if (!ImGui::Begin(windowTitle.c_str(), &keepRunning, windowFlags)) {
			ImGui::SetWindowCollapsed(ImGui::GetCurrentWindow(), false);
			ImGui::End();
			_app->minimize();
			return;
		}
		if (!keepRunning) {
			_app->requestQuit();
		}
	}

	ImGuiID dockIdMain = ImGui::GetID("DockSpace");

	_menuBar.setLastOpenedFiles(_lastOpenedFilesRingBuffer);
	if (_menuBar.update(_app, _lastExecutedCommand)) {
		ImGui::DockBuilderRemoveNode(dockIdMain);
	}

	const bool existingLayout = ImGui::DockBuilderGetNode(dockIdMain);
	ImGui::DockSpace(dockIdMain);

	leftWidget();
	mainWidget();
	rightWidget();

	registerPopups();

	ImGui::End();

	_statusBar.update(TITLE_STATUSBAR, statusBarHeight, _lastExecutedCommand.command);

	if (!existingLayout && viewport->WorkSize.x > 0.0f) {
		ImGui::DockBuilderAddNode(dockIdMain, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockIdMain, viewport->WorkSize);
		ImGuiID dockIdLeft = ImGui::DockBuilderSplitNode(dockIdMain, ImGuiDir_Left, 0.13f, nullptr, &dockIdMain);
		ImGuiID dockIdRight = ImGui::DockBuilderSplitNode(dockIdMain, ImGuiDir_Right, 0.20f, nullptr, &dockIdMain);
		ImGuiID dockIdLeftDown = ImGui::DockBuilderSplitNode(dockIdLeft, ImGuiDir_Down, 0.35f, nullptr, &dockIdLeft);
		ImGuiID dockIdRightDown = ImGui::DockBuilderSplitNode(dockIdRight, ImGuiDir_Down, 0.50f, nullptr, &dockIdRight);
		ImGuiID dockIdMainDown = ImGui::DockBuilderSplitNode(dockIdMain, ImGuiDir_Down, 0.20f, nullptr, &dockIdMain);

		// left side
		configureLeftTopWidgetDock(dockIdLeft);
		configureLeftBottomWidgetDock(dockIdLeftDown);

		// right side
		configureRightTopWidgetDock(dockIdRight);
		configureRightBottomWidgetDock(dockIdRightDown);

		// main
		configureMainTopWidgetDock(dockIdMain);
		configureMainBottomWidgetDock(dockIdMainDown);

		ImGui::DockBuilderFinish(dockIdMain);
	}
}

Viewport* MainWindow::hoveredScene() {
	for (size_t i = 0; i < _scenes.size(); ++i) {
		if (_scenes[i]->isHovered()) {
			return _scenes[i];
		}
	}
	return nullptr;
}

bool MainWindow::saveScreenshot(const core::String& file, const core::String &viewportId) {
	if (viewportId.empty()) {
		if (_lastHoveredScene != nullptr) {
			if (!_lastHoveredScene->saveImage(file.c_str())) {
				Log::warn("Failed to save screenshot to file '%s'", file.c_str());
				return false;
			}
			Log::info("Screenshot created at '%s'", file.c_str());
			return true;
		}
		return false;
	}
	for (Viewport *vp : _scenes) {
		if (vp->id() != viewportId.toInt()) {
			continue;
		}
		if (!vp->saveImage(file.c_str())) {
			Log::warn("Failed to save screenshot to file '%s'", file.c_str());
			return false;
		}
		Log::info("Screenshot created at '%s'", file.c_str());
		return true;
	}
	return false;
}

void MainWindow::resetCamera() {
	if (Viewport *scene = hoveredScene()) {
		scene->resetCamera();
	} else {
		for (size_t i = 0; i < _scenes.size(); ++i) {
			_scenes[i]->resetCamera();
		}
	}
}

void MainWindow::toggleScene() {
	if (Viewport *scene = hoveredScene()) {
		scene->toggleScene();
	} else {
		for (size_t i = 0; i < _scenes.size(); ++i) {
			_scenes[i]->toggleScene();
		}
	}
}

} // namespace voxedit
