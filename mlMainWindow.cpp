/*
*
* Copyright 2016 Activision Publishing, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/

#include "mlMainWindow.h"

#include "stdafx.h"

#include <functional>
#include <iomanip>
#include <sstream>
#include <utility>

#pragma comment(lib, "steam_api64")

static const int appId = 311210;

const char* gLanguages[] = {
	"english", "french", "italian", "spanish", "german", "portuguese", "russian", "polish", "japanese",
	"traditionalchinese", "simplifiedchinese", "englisharabic"
};
const char* gTags[] = {
	"Animation", "Audio", "Character", "Map", "Mod", "Mode", "Model", "Multiplayer", "Scorestreak", "Skin",
	"Specialist", "Texture", "UI", "Vehicle", "Visual Effect", "Weapon", "WIP", "Zombies"
};
dvar_s gDvars[] = {
	{"ai_disableSpawn", "Disable AI from spawning", DVAR_VALUE_BOOL},
	{"developer", "Run developer mode", DVAR_VALUE_INT, 0, 2},
	{"g_password", "Password for your server", DVAR_VALUE_STRING},
	{"logfile", "Console log information written to current fs_game", DVAR_VALUE_INT, 0, 2},
	{"scr_mod_enable_devblock", "Developer blocks are executed in mods ", DVAR_VALUE_BOOL},
	{"connect", "Connect to a specific server", DVAR_VALUE_STRING, NULL, NULL, true},
	{"set_gametype", "Set a gametype to load with map", DVAR_VALUE_STRING, NULL, NULL, true},
	{"splitscreen", "Enable splitscreen", DVAR_VALUE_BOOL},
	{"splitscreen_playerCount", "Allocate the number of instances for splitscreen", DVAR_VALUE_INT, 0, 2}
};

enum mlItemType
{
	ML_ITEM_UNKNOWN,
	ML_ITEM_MAP,
	ML_ITEM_MOD
};

mlBuildThread::mlBuildThread(QList<QPair<QString, QStringList>> Commands, bool IgnoreErrors)
	: mCommands(std::move(Commands)), mSuccess(false), mCancel(false), mIgnoreErrors(IgnoreErrors)
{
}

void mlBuildThread::run()
{
	auto success = true;

	for (const auto& command : mCommands)
	{
		/*if (Command.first.endsWith("BlackOps3.exe"))
		{
			auto parameters = Command.second.join(" ");
			const auto run_str = QString("steam:///rungameid//%1").arg(AppId).toStdWString();
			const auto* game_str = run_str.c_str();
			ShellExecute(nullptr, L"open", L"steam://rungameid/311210//+devmap mp_sector", nullptr, nullptr, SW_SHOWDEFAULT);

			continue;
		}*/
		
		auto* process = new QProcess();
		connect(process, SIGNAL(finished(int)), process, SLOT(deleteLater()));
		process->setWorkingDirectory(QFileInfo(command.first).absolutePath());
		process->setProcessChannelMode(QProcess::MergedChannels);

		emit OutputReady(command.first + ' ' + command.second.join(' ') + "\n");

		process->start(command.first, command.second);
		for (;;)
		{
			Sleep(100);

			if (process->waitForReadyRead(0))
			emit OutputReady(process->readAll());

			const auto state = process->state();
			if (state == QProcess::NotRunning)
				break;

			if (mCancel)
				process->kill();
		}

		if (process->exitStatus() != QProcess::NormalExit)
			return;

		if (process->exitCode() != 0)
		{
			success = false;
			if (!mIgnoreErrors)
				return;
		}
	}

	mSuccess = success;
}

mlConvertThread::mlConvertThread(QStringList& Files, QString& OutputDir, bool IgnoreErrors, bool OverwriteFiles)
	: mFiles(Files), mOutputDir(OutputDir), mOverwrite(OverwriteFiles), mSuccess(false), mCancel(false),
	  mIgnoreErrors(IgnoreErrors)
{
}

void mlConvertThread::run()
{
	auto success = true;

	auto convCountSuccess = 0;
	auto convCountSkipped = 0;
	auto convCountFailed = 0;

	for (QString file : mFiles)
	{
		QFileInfo fileInfo(file);
		const auto workingDirectory = fileInfo.absolutePath();

		auto* process = new QProcess();
		connect(process, SIGNAL(finished(int)), process, SLOT(deleteLater()));
		process->setWorkingDirectory(workingDirectory);
		process->setProcessChannelMode(QProcess::MergedChannels);

		file = fileInfo.baseName();

		const auto toolsPath = QDir::fromNativeSeparators(getenv("TA_TOOLS_PATH"));
		const auto executablePath = QString("%1bin/export2bin.exe").arg(toolsPath);

		auto args = QStringList{};
		//args.append("/v"); // Verbose
		args.append("/piped");

		const auto filepath = fileInfo.absoluteFilePath();

		auto ext = fileInfo.suffix().toUpper();
		if (ext == "XANIM_EXPORT")
		{
			ext = ".XANIM_BIN";
		}
		else if (ext == "XMODEL_EXPORT")
		{
			ext = ".XMODEL_BIN";
		}
		else
		{
			emit OutputReady("Export2Bin: Skipping file '" + filepath + "' (file has invalid extension)\n");
			convCountSkipped++;
			continue;
		}

		auto targetFilepath = QDir::cleanPath(mOutputDir) + QDir::separator() + file + ext;

		QFile infile(filepath);
		QFile outfile(targetFilepath);

		if (!mOverwrite && outfile.exists())
		{
			emit OutputReady("Export2Bin: Skipping file '" + filepath + "' (file already exists)\n");
			convCountSkipped++;
			continue;
		}

		infile.open(QIODevice::OpenMode::enum_type::ReadOnly);
		if (!infile.isOpen())
		{
			emit OutputReady("Export2Bin: Could not open '" + filepath + "' for reading\n");
			convCountFailed++;
			continue;
		}

		emit OutputReady("Export2Bin: Converting '" + file + "'");

		auto buf = infile.readAll();
		infile.close();

		process->start(executablePath, args);
		process->write(buf);
		process->closeWriteChannel();

		auto standardOutputPipeData = QByteArray{};
		auto standardErrorPipeData = QByteArray{};

		for (;;)
		{
			Sleep(20);

			if (process->waitForReadyRead(0))
			{
				standardOutputPipeData.append(process->readAllStandardOutput());
				standardErrorPipeData.append(process->readAllStandardError());
			}

			if (process->state() == QProcess::NotRunning)
				break;

			if (mCancel)
				process->kill();
		}

		if (process->exitStatus() != QProcess::NormalExit)
		{
			emit OutputReady("ERROR: Process exited abnormally");
			success = false;
			break;
		}

		if (process->exitCode() != 0)
		{
			emit OutputReady(standardOutputPipeData);
			emit OutputReady(standardErrorPipeData);

			convCountFailed++;

			if (!mIgnoreErrors)
			{
				success = false;
				break;
			}

			continue;
		}

		outfile.open(QIODevice::OpenMode::enum_type::WriteOnly);
		if (!outfile.isOpen())
		{
			emit OutputReady("Export2Bin: Could not open '" + targetFilepath + "' for writing\n");
			continue;
		}

		outfile.write(standardOutputPipeData);
		outfile.close();

		convCountSuccess++;
	}

	mSuccess = success;
	if (mSuccess)
	{
		const auto msg = QString("Export2Bin: Finished!\n\n"
			"Files Processed: %1\n"
			"Successes: %2\n"
			"Skipped: %3\n"
			"Failures: %4\n").arg(mFiles.count()).arg(convCountSuccess).arg(convCountSkipped).arg(convCountFailed);
		emit OutputReady(msg);
	}
}

mlMainWindow::mlMainWindow(QWidget* parent)
{
	auto settings = QSettings{};

	mBuildThread = nullptr;
	mBuildLanguage = settings.value("BuildLanguage", "english").toString();
	mTreyarchTheme = settings.value("UseDarkTheme", false).toBool();

	// Qt prefers '/' over '\\'
	mGamePath = getenv("TA_GAME_PATH");
	mToolsPath = getenv("TA_TOOLS_PATH");

	UpdateTheme();

	setWindowIcon(QIcon(":/resources/ModLauncher.png"));
	setWindowTitle("Black Ops III Mod Tools Launcher");

	resize(1024, 768);

	CreateActions();
	CreateMenu();
	CreateToolBar();

	mExport2BinGUIWidget = nullptr;

	auto* centralWidget = new QSplitter(parent);
	centralWidget->setOrientation(Qt::Vertical);

	auto* topWidget = new QWidget(parent);
	centralWidget->addWidget(topWidget);

	auto* topLayout = new QHBoxLayout(topWidget);
	topWidget->setLayout(topLayout);

	mFileListWidget = new QTreeWidget();
	mFileListWidget->setHeaderHidden(true);
	mFileListWidget->setUniformRowHeights(true);
	mFileListWidget->setRootIsDecorated(false);
	mFileListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	topLayout->addWidget(mFileListWidget);

	connect(mFileListWidget, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(ContextMenuRequested()));

	auto* actionsLayout = new QVBoxLayout(parent);
	topLayout->addLayout(actionsLayout);

	auto* compileLayout = new QHBoxLayout(parent);
	actionsLayout->addLayout(compileLayout);

	mCompileEnabledWidget = new QCheckBox("Compile");
	compileLayout->addWidget(mCompileEnabledWidget);

	mCompileModeWidget = new QComboBox(parent);
	mCompileModeWidget->addItems(QStringList() << "Ents" << "Full");
	mCompileModeWidget->setCurrentIndex(1);
	mCompileModeWidget->setStyle(QStyleFactory::create("windows"));
	compileLayout->addWidget(mCompileModeWidget);

	auto* lightLayout = new QHBoxLayout(parent);
	actionsLayout->addLayout(lightLayout);

	mLightEnabledWidget = new QCheckBox("Light");
	lightLayout->addWidget(mLightEnabledWidget);

	mLightQualityWidget = new QComboBox(parent);
	mLightQualityWidget->addItems(QStringList() << "Low" << "Medium" << "High");
	mLightQualityWidget->setCurrentIndex(1);
	mLightQualityWidget->setStyle(QStyleFactory::create("windows"));
	mLightQualityWidget->setMinimumWidth(64); // Fix for "Medium" being cut off in the dark theme
	lightLayout->addWidget(mLightQualityWidget);

	mLinkEnabledWidget = new QCheckBox("Link");
	actionsLayout->addWidget(mLinkEnabledWidget);

	mRunEnabledWidget = new QCheckBox("Run");
	actionsLayout->addWidget(mRunEnabledWidget);

	mRunOptionsWidget = new QLineEdit(parent);
	mRunOptionsWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	actionsLayout->addWidget(mRunOptionsWidget);

	mBuildButton = new QPushButton("Build");
	connect(mBuildButton, SIGNAL(clicked()), mActionEditBuild, SLOT(trigger()));
	actionsLayout->addWidget(mBuildButton);

	mDvarsButton = new QPushButton("Dvars");
	connect(mDvarsButton, SIGNAL(clicked()), this, SLOT(OnEditDvars()));
	actionsLayout->addWidget(mDvarsButton);

	mLogButton = new QPushButton("Save Log");
	connect(mLogButton, SIGNAL(clicked()), this, SLOT(OnSaveLog()));
	actionsLayout->addWidget(mLogButton);

	mIgnoreErrorsWidget = new QCheckBox("Ignore Errors");
	actionsLayout->addWidget(mIgnoreErrorsWidget);

	actionsLayout->addStretch(1);

	mOutputWidget = new QPlainTextEdit(this);
	centralWidget->addWidget(mOutputWidget);

	setCentralWidget(centralWidget);

	mShippedMapList << "mp_aerospace" << "mp_apartments" << "mp_arena" << "mp_banzai" << "mp_biodome" << "mp_chinatown"
		<< "mp_city" << "mp_conduit" << "mp_crucible" << "mp_cryogen" << "mp_ethiopia" << "mp_freerun_01" <<
		"mp_freerun_02" << "mp_freerun_03" << "mp_freerun_04" << "mp_havoc" << "mp_infection" << "mp_kung_fu" <<
		"mp_metro" << "mp_miniature" << "mp_nuketown_x" << "mp_redwood" << "mp_rise" << "mp_rome" << "mp_ruins" <<
		"mp_sector" << "mp_shrine" << "mp_skyjacked" << "mp_spire" << "mp_stronghold" << "mp_veiled" << "mp_waterpark"
		<< "mp_western" << "zm_castle" << "zm_factory" << "zm_genesis" << "zm_island" << "zm_levelcommon" <<
		"zm_stalingrad" << "zm_zod";

	settings.beginGroup("MainWindow");
	resize(QSize(800, 600));
	move(QPoint(200, 200));
	restoreGeometry(settings.value("Geometry").toByteArray());
	restoreState(settings.value("State").toByteArray());
	settings.endGroup();

	SteamAPI_Init();

	connect(&mTimer, SIGNAL(timeout()), this, SLOT(SteamUpdate()));
	mTimer.start(1000);

	PopulateFileList();
}

void mlMainWindow::CreateActions()
{
	mActionFileNew = new QAction(QIcon(":/resources/FileNew.png"), "&New...", this);
	mActionFileNew->setShortcut(QKeySequence("Ctrl+N"));
	connect(mActionFileNew, SIGNAL(triggered()), this, SLOT(OnFileNew()));

	mActionFileAssetEditor = new QAction(QIcon(":/resources/AssetEditor.png"), "&Asset Editor", this);
	mActionFileAssetEditor->setShortcut(QKeySequence("Ctrl+A"));
	connect(mActionFileAssetEditor, SIGNAL(triggered()), this, SLOT(OnFileAssetEditor()));

	mActionFileLevelEditor = new QAction(QIcon(":/resources/Radiant.png"), "Open in &Radiant", this);
	mActionFileLevelEditor->setShortcut(QKeySequence("Ctrl+R"));
	mActionFileLevelEditor->setToolTip("Level Editor");
	connect(mActionFileLevelEditor, SIGNAL(triggered()), this, SLOT(OnFileLevelEditor()));

	mActionFileExport2Bin = new QAction(QIcon(":/resources/Export2Bin.png"), "&Export2Bin GUI", this);
	mActionFileExport2Bin->setShortcut(QKeySequence("Ctrl+E"));
	connect(mActionFileExport2Bin, SIGNAL(triggered()), this, SLOT(OnFileExport2Bin()));

	mActionFileExit = new QAction("E&xit", this);
	connect(mActionFileExit, SIGNAL(triggered()), this, SLOT(close()));

	mActionEditBuild = new QAction(QIcon(":/resources/Go.png"), "Build", this);
	mActionEditBuild->setShortcut(QKeySequence("Ctrl+B"));
	connect(mActionEditBuild, SIGNAL(triggered()), this, SLOT(OnEditBuild()));

	mActionEditPublish = new QAction(QIcon(":/resources/upload.png"), "Publish", this);
	mActionEditPublish->setShortcut(QKeySequence("Ctrl+P"));
	connect(mActionEditPublish, SIGNAL(triggered()), this, SLOT(OnEditPublish()));

	mActionEditOptions = new QAction("&Options...", this);
	connect(mActionEditOptions, SIGNAL(triggered()), this, SLOT(OnEditOptions()));

	mActionHelpAbout = new QAction("&About...", this);
	connect(mActionHelpAbout, SIGNAL(triggered()), this, SLOT(OnHelpAbout()));
}

void mlMainWindow::CreateMenu()
{
	auto* menuBar = new QMenuBar(this);

	auto* fileMenu = new QMenu("&File", menuBar);
	fileMenu->addAction(mActionFileNew);
	fileMenu->addSeparator();
	fileMenu->addAction(mActionFileAssetEditor);
	fileMenu->addAction(mActionFileLevelEditor);
	fileMenu->addAction(mActionFileExport2Bin);
	fileMenu->addSeparator();
	fileMenu->addAction(mActionFileExit);
	menuBar->addAction(fileMenu->menuAction());

	auto* editMenu = new QMenu("&Edit", menuBar);
	editMenu->addAction(mActionEditBuild);
	editMenu->addAction(mActionEditPublish);
	editMenu->addSeparator();
	editMenu->addAction(mActionEditOptions);
	menuBar->addAction(editMenu->menuAction());

	auto* helpMenu = new QMenu("&Help", menuBar);
	helpMenu->addAction(mActionHelpAbout);
	menuBar->addAction(helpMenu->menuAction());

	setMenuBar(menuBar);
}

void mlMainWindow::CreateToolBar()
{
	auto* toolBar = new QToolBar("Standard", this);
	toolBar->setObjectName(QStringLiteral("StandardToolBar"));

	toolBar->addAction(mActionFileNew);
	toolBar->addAction(mActionEditBuild);
	toolBar->addAction(mActionEditPublish);
	toolBar->addSeparator();
	toolBar->addAction(mActionFileAssetEditor);
	toolBar->addAction(mActionFileLevelEditor);
	toolBar->addAction(mActionFileExport2Bin);

	addToolBar(Qt::TopToolBarArea, toolBar);
}

void mlMainWindow::InitExport2BinGUI()
{
	auto* dock = new QDockWidget(this);
	dock->setWindowTitle("Export2Bin");
	dock->setFloating(true);

	auto* widget = new QWidget(dock);
	auto* gridLayout = new QGridLayout();
	widget->setLayout(gridLayout);
	dock->setWidget(widget);

	auto* groupBox = new Export2BinGroupBox(dock, this);
	gridLayout->addWidget(groupBox, 0, 0);

	auto* label = new QLabel("Drag Files Here", groupBox);
	label->setAlignment(Qt::AlignCenter);
	auto* groupBoxLayout = new QVBoxLayout(groupBox);
	groupBoxLayout->addWidget(label);
	groupBox->setLayout(groupBoxLayout);

	mExport2BinOverwriteWidget = new QCheckBox("&Overwrite Existing Files", widget);
	gridLayout->addWidget(mExport2BinOverwriteWidget, 1, 0);

	const auto Settings = QSettings{};
	mExport2BinOverwriteWidget->setChecked(Settings.value("Export2Bin_OverwriteFiles", true).toBool());

	auto* dirLayout = new QHBoxLayout();
	auto* dirLabel = new QLabel("Ouput Directory:", widget);
	mExport2BinTargetDirWidget = new QLineEdit(widget);
	auto* dirBrowseButton = new QToolButton(widget);
	dirBrowseButton->setText("...");

	const QDir defaultPath = QString("%1/model_export/export2bin/").arg(mToolsPath);
	mExport2BinTargetDirWidget->setText(Settings.value("Export2Bin_TargetDir", defaultPath.absolutePath()).toString());

	connect(dirBrowseButton, SIGNAL(clicked()), this, SLOT(OnExport2BinChooseDirectory()));
	connect(mExport2BinOverwriteWidget, SIGNAL(clicked()), this, SLOT(OnExport2BinToggleOverwriteFiles()));

	dirBrowseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	dirLayout->addWidget(dirLabel);
	dirLayout->addWidget(mExport2BinTargetDirWidget);
	dirLayout->addWidget(dirBrowseButton);

	gridLayout->addLayout(dirLayout, 2, 0);

	groupBox->setAcceptDrops(true);

	dock->resize(QSize(256, 256));

	mExport2BinGUIWidget = dock;
}

void mlMainWindow::closeEvent(QCloseEvent* Event)
{
	auto settings = QSettings{};
	settings.beginGroup("MainWindow");
	settings.setValue("Geometry", saveGeometry());
	settings.setValue("State", saveState());
	settings.endGroup();

	Event->accept();
}

void mlMainWindow::SteamUpdate()
{
	SteamAPI_RunCallbacks();
}

void mlMainWindow::UpdateDB()
{
	if (mBuildThread != nullptr)
		return;

	QList<QPair<QString, QStringList>> Commands;
	Commands.append(QPair<QString, QStringList>(QString("%1/gdtdb/gdtdb.exe").arg(mToolsPath),
	                                            QStringList() << "/update"));

	StartBuildThread(Commands);
}

void mlMainWindow::StartBuildThread(const QList<QPair<QString, QStringList>>& Commands)
{
	mBuildButton->setText("Cancel");
	mOutputWidget->clear();

	mBuildThread = new mlBuildThread(Commands, mIgnoreErrorsWidget->isChecked());
	connect(mBuildThread, SIGNAL(OutputReady(QString)), this, SLOT(BuildOutputReady(QString)));
	connect(mBuildThread, SIGNAL(finished()), this, SLOT(BuildFinished()));
	mBuildThread->start();
}

void mlMainWindow::StartConvertThread(QStringList& pathList, QString& outputDir, bool allowOverwrite)
{
	mConvertThread = new mlConvertThread(pathList, outputDir, true, allowOverwrite);
	connect(mConvertThread, SIGNAL(OutputReady(QString)), this, SLOT(BuildOutputReady(QString)));
	connect(mConvertThread, SIGNAL(finished()), this, SLOT(BuildFinished()));
	mConvertThread->start();
}

void mlMainWindow::PopulateFileList() const
{
	mFileListWidget->clear();

	auto userMapsFolder = QDir::cleanPath(QString("%1/usermaps/").arg(mGamePath));
	auto stringList = QDir(userMapsFolder).entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
	auto* mapsRootItem = new QTreeWidgetItem(mFileListWidget, QStringList() << "Maps");

	auto font = mapsRootItem->font(0);
	font.setBold(true);
	mapsRootItem->setFont(0, font);

	for (const auto& mapName : stringList)
	{
		auto zoneFileName = QString("%1/%2/zone_source/%3.zone").arg(userMapsFolder, mapName, mapName);

		if (QFileInfo(zoneFileName).isFile())
		{
			auto* mapItem = new QTreeWidgetItem(mapsRootItem, QStringList() << mapName);
			mapItem->setCheckState(0, Qt::Unchecked);
			mapItem->setData(0, Qt::UserRole, ML_ITEM_MAP);
		}
	}

	auto modsFolder = QDir::cleanPath(QString("%1/mods/").arg(mGamePath));
	auto mods = QDir(modsFolder).entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
	auto* modsRootItem = new QTreeWidgetItem(mFileListWidget, QStringList() << "Mods");
	modsRootItem->setFont(0, font);
	const char* files[4] = {"core_mod", "mp_mod", "cp_mod", "zm_mod"};

	for (const auto& modName : mods)
	{
		QTreeWidgetItem* parentItem = nullptr;

		for (auto& file : files)
		{
			auto zoneFileName = QString("%1/%2/zone_source/%3.zone").arg(modsFolder, modName, file);

			if (QFileInfo(zoneFileName).isFile())
			{
				if (parentItem == nullptr)
					parentItem = new QTreeWidgetItem(modsRootItem, QStringList() << modName);

				auto* modItem = new QTreeWidgetItem(parentItem, QStringList() << file);
				modItem->setCheckState(0, Qt::Unchecked);
				modItem->setData(0, Qt::UserRole, ML_ITEM_MOD);
			}
		}
	}

	mFileListWidget->expandAll();
}

void mlMainWindow::ContextMenuRequested() const
{
	auto itemList = mFileListWidget->selectedItems();
	if (itemList.isEmpty())
		return;

	auto* item = itemList[0];
	const auto* ItemType = (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP) ? "Map" : "Mod";

	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_UNKNOWN)
		return;

	const auto gameIcon = QIcon{":/resources/BlackOps3.png"};

	auto* menu = new QMenu();
	menu->addAction(gameIcon, QString("Run %1").arg(ItemType), this, SLOT(OnRunMapOrMod()));

	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
		menu->addAction(mActionFileLevelEditor);

	menu->addAction("Edit Zone File", this, SLOT(OnOpenZoneFile()));
	menu->addAction(QString("Open %1 Folder").arg(ItemType), this, SLOT(OnOpenModRootFolder()));

	menu->addSeparator();
	menu->addAction("Delete", this, SLOT(OnDelete()));
	menu->addAction("Clean XPaks", this, SLOT(OnCleanXPaks()));

	menu->exec(QCursor::pos());
}

void mlMainWindow::OnFileAssetEditor() const
{
	auto* process = new QProcess();
	connect(process, SIGNAL(finished(int)), process, SLOT(deleteLater()));
	process->start(QString("%1/bin/AssetEditor_modtools.exe").arg(mToolsPath), QStringList());
}

void mlMainWindow::OnFileLevelEditor()
{
	auto* process = new QProcess();
	connect(process, SIGNAL(finished(int)), process, SLOT(deleteLater()));

	QList<QTreeWidgetItem*> ItemList = mFileListWidget->selectedItems();
	if ((ItemList.count() != 0) && ItemList[0]->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
	{
		QString mapName = ItemList[0]->text(0);
		process->start(QString("%1/bin/radiant_modtools.exe").arg(mToolsPath),
		               QStringList() << QString("%1/map_source/%2/%3.map").arg(mGamePath, mapName.left(2), mapName));
	}
	else
	{
		process->start(QString("%1/bin/radiant_modtools.exe").arg(mToolsPath), QStringList());
	}
}

void mlMainWindow::OnFileExport2Bin()
{
	if (mExport2BinGUIWidget == nullptr)
	{
		InitExport2BinGUI();
		mExport2BinGUIWidget->hide(); // Ensure the window is hidden (just in case)
	}

	mExport2BinGUIWidget->isVisible() ? mExport2BinGUIWidget->hide() : mExport2BinGUIWidget->show();
}

void mlMainWindow::OnFileNew()
{
	const auto templatesFolder = QDir{QString("%1/rex/templates").arg(mToolsPath)};
	auto templates = templatesFolder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

	if (templates.isEmpty())
	{
		QMessageBox::information(this, "Error", "Could not find any map templates.");
		return;
	}

	QDialog dialog(this, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
	dialog.setWindowTitle("New Map or Mod");

	auto* layout = new QVBoxLayout(&dialog);

	auto* formLayout = new QFormLayout();
	layout->addLayout(formLayout);

	auto* nameWidget = new QLineEdit();
	nameWidget->setValidator(new QRegularExpressionValidator(QRegularExpression("[a-zA-Z0-9_]*"), this));
	formLayout->addRow("Name:", nameWidget);

	auto* templateWidget = new QComboBox();
	templateWidget->addItems(templates);
	templateWidget->setStyle(QStyleFactory::create("windows"));
	formLayout->addRow("Template:", templateWidget);

	auto* frame = new QFrame();
	frame->setFrameShape(QFrame::HLine);
	frame->setFrameShadow(QFrame::Raised);
	layout->addWidget(frame);

	auto* buttonBox = new QDialogButtonBox(&dialog);
	buttonBox->setOrientation(Qt::Horizontal);
	buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttonBox->setCenterButtons(true);

	layout->addWidget(buttonBox);

	connect(buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

	if (dialog.exec() != QDialog::Accepted)
		return;

	const auto name = nameWidget->text();

	if (name.isEmpty())
	{
		QMessageBox::information(this, "Error", "Map name cannot be empty.");
		return;
	}

	if (mShippedMapList.contains(name, Qt::CaseInsensitive))
	{
		QMessageBox::information(this, "Error", "Map name cannot be the same as a built-in map.");
		return;
	}

	auto mapName = nameWidget->text().toLatin1().toLower();
	auto output = QString{};

	const auto template_ = templates[templateWidget->currentIndex()];

	if ((template_ == "MP Mod Level" && !mapName.startsWith("mp_")) || (template_ == "ZM Mod Level" && !mapName.
		startsWith("zm_")))
	{
		QMessageBox::information(this, "Error", "Map name must start with 'mp_' or 'zm_'.");
		return;
	}

	std::function<bool(const QString&, const QString&)> recursiveCopy = [&](const QString& SourcePath, const QString& DestPath) -> bool
	{
		const auto dir = QDir{SourcePath};
		if (!dir.exists())
			return false;


		foreach(auto dirEntry, dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			const auto newPath = QString(DestPath + QDir::separator() + dirEntry).replace(QString("template"), mapName);

			if (!dir.mkpath(newPath))
				return false;

			if (!recursiveCopy(SourcePath + QDir::separator() + dirEntry, newPath))
				return false;
		}

		foreach(auto dirEntry, dir.entryList(QDir::Files))
		{
			auto sourceFile = QFile{SourcePath + QDir::separator() + dirEntry};
			auto destFileName = QString{DestPath + QDir::separator() + dirEntry}.replace(QString("template"), mapName);
			auto destFile = QFile{destFileName};

			if (!sourceFile.open(QFile::ReadOnly) || !destFile.open(QFile::WriteOnly))
				return false;

			while (!sourceFile.atEnd())
			{
				auto line = sourceFile.readLine();

				if (line.contains("guid"))
				{
					auto lineString = QString{line};
					lineString.replace(QRegExp(R"(guid "\{(.*)\}")"),
					                   QString("guid \"%1\"").arg(QUuid::createUuid().toString()));
					line = lineString.toLatin1();
				}
				else
				{
					line.replace("template", mapName);
				}

				destFile.write(line);
			}

			output += destFileName + "\n";
		}

		return true;
	};

	if (recursiveCopy(templatesFolder.absolutePath() + QDir::separator() + templates[templateWidget->currentIndex()],
	                  QDir::cleanPath(mGamePath)))
	{
		PopulateFileList();

		QMessageBox::information(this, "New Map Created", QString("Files created:\n") + output);
	}
	else
	{
		QMessageBox::information(this, "Error", "Error creating map files.");
	}
}

void mlMainWindow::OnEditBuild()
{
	if (mBuildThread != nullptr)
	{
		mBuildThread->Cancel();
		return;
	}

	auto commands = QList<QPair<QString, QStringList>>{};
	auto updateAdded = false;

	auto addUpdateDbCommand = [&]()
	{
		if (!updateAdded)
		{
			commands.append(QPair<QString, QStringList>(QString("%1/gdtdb/gdtdb.exe").arg(mToolsPath),
			                                            QStringList() << "/update"));
			updateAdded = true;
		}
	};

	auto checkedItems = QList<QTreeWidgetItem*>{};

	std::function<void (QTreeWidgetItem*)> searchCheckedItems = [&](QTreeWidgetItem* ParentItem) -> void
	{
		for (auto childIdx = 0; childIdx < ParentItem->childCount(); childIdx++)
		{
			auto* child = ParentItem->child(childIdx);
			if (child->checkState(0) == Qt::Checked)
			{
				checkedItems.append(child);
			}
			else
			{
				searchCheckedItems(child);
			}
		}
	};

	searchCheckedItems(mFileListWidget->invisibleRootItem());
	auto lastMap = QString{};
	auto lastMod = QString{};

	auto languageArgs = QStringList{};

	if (mBuildLanguage != "All")
	{
		languageArgs << "-language" << mBuildLanguage;
	}
	else
	{
		for (const auto& language : gLanguages)
		{
			languageArgs << "-language" << language;
		}
	}

	for (auto* item : checkedItems)
	{
		if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
		{
			auto mapName = item->text(0);

			if (mCompileEnabledWidget->isChecked())
			{
				addUpdateDbCommand();

				auto args = QStringList{};
				args << "-platform" << "pc";

				if (mCompileModeWidget->currentIndex() == 0)
				{
					args << "-onlyents";
				}
				else
				{
					args << "-navmesh" << "-navvolume";
				}

				args << "-loadFrom" << QString(R"(%1\map_source\%2\%3.map)").arg(mGamePath, mapName.left(2), mapName);
				args << QString(R"(%1\share\raw\maps\%2\%3.d3dbsp)").arg(mGamePath, mapName.left(2), mapName);

				commands.append(QPair<QString, QStringList>(QString("%1\\bin\\cod2map64.exe").arg(mToolsPath), args));
			}

			if (mLightEnabledWidget->isChecked())
			{
				addUpdateDbCommand();

				auto args = QStringList{};
				args << "-ledSilent";

				switch (mLightQualityWidget->currentIndex())
				{
				case 0:
					args << "+low";
					break;

				default:
				case 1:
					args << "+medium";
					break;

				case 2:
					args << "+high";
					break;
				}

				args << "+localprobes" << "+forceclean" << "+recompute" << QString("%1/map_source/%2/%3.map").arg(mGamePath, mapName.left(2), mapName);
				commands.append(QPair<QString, QStringList>(QString("%1/bin/radiant_modtools.exe").arg(mToolsPath), args));
			}

			if (mLinkEnabledWidget->isChecked())
			{
				addUpdateDbCommand();
				commands.append(QPair<QString, QStringList>(QString("%1/bin/linker_modtools.exe").arg(mToolsPath), QStringList() << languageArgs << "-modsource" << mapName));
			}

			lastMap = mapName;
		}
		else
		{
			auto modName = item->parent()->text(0);

			if (mLinkEnabledWidget->isChecked())
			{
				addUpdateDbCommand();

				auto zoneName = item->text(0);
				commands.append(QPair<QString, QStringList>(QString("%1/bin/linker_modtools.exe").arg(mToolsPath),
				                                            QStringList() << languageArgs << "-fs_game" << modName <<
				                                            "-modsource" << zoneName));
			}

			lastMod = modName;
		}
	}

	if (mRunEnabledWidget->isChecked() && (!lastMod.isEmpty() || !lastMap.isEmpty()))
	{
		auto args = QStringList{};

		if (!mRunDvars.isEmpty())
		{
			args << mRunDvars;
		}

		args << "+set" << "fs_game" << (lastMod.isEmpty() ? lastMap : lastMod);

		if (!lastMap.isEmpty())
		{
			args << "+devmap" << lastMap;
		}

		const auto extraOptions = mRunOptionsWidget->text();
		if (!extraOptions.isEmpty())
		{
			args << extraOptions.split(' ');
		}

		commands.append(QPair<QString, QStringList>(QString("%1BlackOps3.exe").arg(mGamePath), args));
	}

	if (commands.empty() && !updateAdded)
	{
		QMessageBox::information(this, "No Tasks",
		                         "Please selected at least one file from the list and one action to be performed.");
		return;
	}

	StartBuildThread(commands);
}

void mlMainWindow::OnEditPublish()
{
	std::function<QTreeWidgetItem*(QTreeWidgetItem*)> SearchCheckedItem = [&](QTreeWidgetItem* ParentItem) -> QTreeWidgetItem*
	{
		for (auto childIdx = 0; childIdx < ParentItem->childCount(); childIdx++)
		{
			auto* child = ParentItem->child(childIdx);
			if (child->checkState(0) == Qt::Checked)
			{
				return child;
			}

			auto* checked = SearchCheckedItem(child);
			if (checked != nullptr)
			{
				return checked;
			}
		}

		return nullptr;
	};

	auto* item = SearchCheckedItem(mFileListWidget->invisibleRootItem());
	if (item == nullptr)
	{
		QMessageBox::warning(this, "Error", "No maps or mods checked.");
		return;
	}

	auto folder = QString{};
	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
	{
		folder = "usermaps/" + item->text(0);
		mType = "map";
		mFolderName = item->text(0);
	}
	else
	{
		folder = "mods/" + item->parent()->text(0);
		mType = "mod";
		mFolderName = item->parent()->text(0);
	}

	mWorkshopFolder = QString("%1/%2/zone").arg(mGamePath, folder);
	auto file = QFile{mWorkshopFolder + "/workshop.json"};

	if (!QFileInfo(mWorkshopFolder).isDir())
	{
		QMessageBox::information(this, "Error", QString("The folder '%1' does not exist.").arg(mWorkshopFolder));
		return;
	}

	mFileId = 0;
	mTitle.clear();
	mDescription.clear();
	mThumbnail.clear();
	mTags.clear();

	if (file.open(QIODevice::ReadOnly))
	{
		const auto document = QJsonDocument::fromJson(file.readAll());
		auto root = document.object();

		mFileId = root["PublisherID"].toString().toULongLong();
		mTitle = root["Title"].toString();
		mDescription = root["Description"].toString();
		mThumbnail = root["Thumbnail"].toString();
		mTags = root["Tags"].toString().split(',');
	}

	if (mFileId != 0u)
	{
		const auto steamApiCall = SteamUGC()->RequestUGCDetails(mFileId, 10);
		mSteamCallResultRequestDetails.Set(steamApiCall, this, &mlMainWindow::OnUGCRequestUGCDetails);
	}
	else
	{
		ShowPublishDialog();
	}
}

void mlMainWindow::OnUGCRequestUGCDetails(SteamUGCRequestUGCDetailsResult_t* RequestDetailsResult, bool IOFailure)
{
	if (IOFailure || RequestDetailsResult->m_details.m_eResult != k_EResultOK)
	{
		QMessageBox::warning(this, "Error",
		                     "Error retrieving item data from the Steam Workshop.\nWorkshop item possibly deleted.");
		return;
	}

	auto* details = &RequestDetailsResult->m_details;

	mTitle = details->m_rgchTitle;
	mDescription = details->m_rgchDescription;
	mTags = QString(details->m_rgchTags).split(',');

	ShowPublishDialog();
}

void mlMainWindow::ShowPublishDialog()
{
	auto dialog = QDialog{this, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint};
	dialog.setWindowTitle("Publish Mod");

	auto* layout = new QVBoxLayout(&dialog);

	auto* formLayout = new QFormLayout();
	layout->addLayout(formLayout);

	auto* titleWidget = new QLineEdit();
	titleWidget->setText(mTitle);
	formLayout->addRow("Title:", titleWidget);

	auto* descriptionWidget = new QLineEdit();
	descriptionWidget->setText(mDescription);
	formLayout->addRow("Description:", descriptionWidget);

	auto* thumbnailEdit = new QLineEdit();
	thumbnailEdit->setText(mThumbnail);

	auto* thumbnailButton = new QToolButton();
	thumbnailButton->setText("...");

	auto* thumbnailLayout = new QHBoxLayout();
	thumbnailLayout->setContentsMargins(0, 0, 0, 0);
	thumbnailLayout->addWidget(thumbnailEdit);
	thumbnailLayout->addWidget(thumbnailButton);

	auto* thumbnailWidget = new QWidget();
	thumbnailWidget->setLayout(thumbnailLayout);

	formLayout->addRow("Thumbnail:", thumbnailWidget);

	auto* tagsTree = new QTreeWidget(&dialog);
	tagsTree->setHeaderHidden(true);
	tagsTree->setUniformRowHeights(true);
	tagsTree->setRootIsDecorated(false);
	formLayout->addRow("Tags:", tagsTree);

	for (const auto* tag : gTags)
	{
		auto* item = new QTreeWidgetItem(tagsTree, QStringList() << tag);
		item->setCheckState(0, mTags.contains(tag) ? Qt::Checked : Qt::Unchecked);
	}

	auto* frame = new QFrame();
	frame->setFrameShape(QFrame::HLine);
	frame->setFrameShadow(QFrame::Raised);
	layout->addWidget(frame);

	auto* buttonBox = new QDialogButtonBox(&dialog);
	buttonBox->setOrientation(Qt::Horizontal);
	buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttonBox->setCenterButtons(true);

	layout->addWidget(buttonBox);

	auto thumbnailBrowse = [=]()
	{
		const auto FileName = QFileDialog::getOpenFileName(this, "Open Thumbnail", QString(), "All Files (*.*)");
		if (!FileName.isEmpty())
		{
			thumbnailEdit->setText(FileName);
		}
	};

	connect(thumbnailButton, &QToolButton::clicked, thumbnailBrowse);
	connect(buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	mTitle = titleWidget->text();
	mDescription = descriptionWidget->text();
	mThumbnail = thumbnailEdit->text();
	mTags.clear();

	for (auto childIdx = 0; childIdx < tagsTree->topLevelItemCount(); childIdx++)
	{
		auto* child = tagsTree->topLevelItem(childIdx);
		if (child->checkState(0) == Qt::Checked)
		{
			mTags.append(child->text(0));
		}
	}

	if (SteamUGC() == nullptr)
	{
		QMessageBox::information(this, "Error",
		                         "Could not initialize Steam, make sure you're running the launcher from the Steam client.");
		return;
	}

	if (mFileId == 0u)
	{
		const auto SteamAPICall = SteamUGC()->CreateItem(appId, k_EWorkshopFileTypeCommunity);
		mSteamCallResultCreateItem.Set(SteamAPICall, this, &mlMainWindow::OnCreateItemResult);
	}
	else
	{
		UpdateWorkshopItem();
	}
}

void mlMainWindow::OnEditOptions()
{
	auto dialog = QDialog{this, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint};
	dialog.setWindowTitle("Options");

	auto* layout = new QVBoxLayout(&dialog);

	auto settings = QSettings{};
	auto* checkBox = new QCheckBox("Use Treyarch Theme");
	checkBox->setToolTip("Toggle between the dark grey Treyarch colors and the default Windows colors");
	checkBox->setChecked(settings.value("UseDarkTheme", false).toBool());
	layout->addWidget(checkBox);

	auto* languageLayout = new QHBoxLayout();
	languageLayout->addWidget(new QLabel("Build Language:"));

	auto languages = QStringList{};
	languages << "All";
	for (auto& gLanguage : gLanguages)
	{
		languages << gLanguage;
	}

	auto* languageCombo = new QComboBox();
	languageCombo->addItems(languages);
	languageCombo->setCurrentText(mBuildLanguage);
	languageCombo->setStyle(QStyleFactory::create("windows"));
	languageLayout->addWidget(languageCombo);

	layout->addLayout(languageLayout);

	auto* buttonBox = new QDialogButtonBox(&dialog);
	buttonBox->setOrientation(Qt::Horizontal);
	buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttonBox->setCenterButtons(true);

	layout->addWidget(buttonBox);

	connect(buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	mBuildLanguage = languageCombo->currentText();
	mTreyarchTheme = checkBox->isChecked();

	settings.setValue("BuildLanguage", mBuildLanguage);
	settings.setValue("UseDarkTheme", mTreyarchTheme);

	UpdateTheme();
}

void mlMainWindow::UpdateTheme() const
{
	if (mTreyarchTheme)
	{
		qApp->setStyle("Fusion");

		QFile file(":/stylesheet/darkmode.qss");
		file.open(QFile::ReadOnly | QFile::Text);
		QTextStream stream(&file);
		qApp->setStyleSheet(stream.readAll());
	}
	else
	{
		qApp->setStyle("WindowsVista");
		qApp->setStyleSheet("");
	}
}

void mlMainWindow::OnEditDvars()
{
	auto dialog = QDialog{this, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint};
	dialog.setWindowTitle("Dvar Options");

	auto* layout = new QVBoxLayout(&dialog);

	auto* label = new QLabel(&dialog);
	label->setText("Dvars that are to be used when you run the game.\nMust press \"OK\" in order to save the values!");
	layout->addWidget(label);

	auto* dvarTree = new QTreeWidget(&dialog);
	dvarTree->setColumnCount(2);
	dvarTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	dvarTree->setHeaderLabels(QStringList() << "Dvar" << "Value");
	//DvarTree->setUniformRowHeights(true);
	dvarTree->setFixedHeight(256);
	dvarTree->setRootIsDecorated(false);
	layout->addWidget(dvarTree);

	auto* buttonBox = new QDialogButtonBox(&dialog);
	buttonBox->setOrientation(Qt::Horizontal);
	buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttonBox->setCenterButtons(true);

	layout->addWidget(buttonBox);

	for (const auto& dvar : gDvars)
	{
		Dvar{dvar, dvarTree};
	}

	connect(buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	auto size = 0;
	auto settings = QSettings{};
	auto dvarName = QString{};
	auto dvarValue = QString{};
	auto it = QTreeWidgetItemIterator{dvarTree};

	mRunDvars.clear();

	while (((*it) != nullptr) && size < static_cast<int>(ARRAYSIZE(gDvars)))
	{
		auto* widget = dvarTree->itemWidget(*it, 1);
		dvarName = (*it)->data(0, 0).toString();
		const auto dvar = Dvar::findDvar(dvarName, dvarTree, gDvars, ARRAYSIZE(gDvars));
		switch (dvar.type)
		{
		case DVAR_VALUE_BOOL:
			dvarValue = Dvar::setDvarSetting(dvar, dynamic_cast<QCheckBox*>(widget));
			break;
		case DVAR_VALUE_INT:
			dvarValue = Dvar::setDvarSetting(dvar, dynamic_cast<QSpinBox*>(widget));
			break;
		case DVAR_VALUE_STRING:
			dvarValue = Dvar::setDvarSetting(dvar, dynamic_cast<QLineEdit*>(widget));
			break;
		}

		if (!dvarValue.toLatin1().isEmpty())
		{
			if (!dvar.isCmd)
			{
				mRunDvars << "+set" << dvarName;
			}
			else
			{
				// hack for cmds
				mRunDvars << QString("+%1").arg(dvarName);
			}
			mRunDvars << dvarValue;
		}
		size++;
		++it;
	}
}

void mlMainWindow::OnSaveLog() const
{
	// save file: modlog_<timestamp>.txt
	// location: exe_root/logs/  (root/bin/logs)
	const auto time = std::time(nullptr);
	auto ss = std::stringstream{};
	const auto timeStr = std::put_time(std::localtime(&time), "%F_%T");

	ss << timeStr;

	auto dateStr = ss.str();
	std::replace(dateStr.begin(), dateStr.end(), ':', '_');

	auto log = QFile{QString{"modlog_%1.txt"}.arg(dateStr.c_str())};

	if (!log.open(QIODevice::WriteOnly))
		return;

	auto stream = QTextStream(&log);
	stream << mOutputWidget->toPlainText();

	QMessageBox::information(nullptr, QString("Save Log"), QString("The console log has been saved to %1").arg(log.fileName()));
}

void mlMainWindow::UpdateWorkshopItem()
{
	auto root = QJsonObject{};

	root["PublisherID"] = QString::number(mFileId);
	root["Title"] = mTitle;
	root["Description"] = mDescription;
	root["Thumbnail"] = mThumbnail;
	root["Type"] = mType;
	root["FolderName"] = mFolderName;
	root["Tags"] = mTags.join(',');

	const auto workshopFile = QString{mWorkshopFolder + "/workshop.json"};
	auto file = QFile{workshopFile};

	if (!file.open(QIODevice::WriteOnly))
	{
		QMessageBox::warning(this, "Error", QString("Error writing to file '%1'.").arg(workshopFile));
		return;
	}

	file.write(QJsonDocument(root).toJson());
	file.close();

	const auto updateHandle = SteamUGC()->StartItemUpdate(appId, mFileId);
	SteamUGC()->SetItemTitle(updateHandle, mTitle.toLatin1().constData());
	SteamUGC()->SetItemDescription(updateHandle, mDescription.toLatin1().constData());
	SteamUGC()->SetItemPreview(updateHandle, mThumbnail.toLatin1().constData());
	SteamUGC()->SetItemContent(updateHandle, mWorkshopFolder.toLatin1().constData());

	const char* tagList[ARRAYSIZE(gTags)];
	SteamParamStringArray_t tags{};
	tags.m_ppStrings = tagList;
	tags.m_nNumStrings = 0;

	for (auto& tag : mTags)
	{
		auto tagStr = tag.toLatin1();

		for (auto& gTag : gTags)
		{
			if (tagStr == gTag)
			{
				tagList[tags.m_nNumStrings++] = gTag;
				if (tags.m_nNumStrings == ARRAYSIZE(tagList))
				{
					break;
				}
			}
		}
	}

	SteamUGC()->SetItemTags(updateHandle, &tags);

	const auto steamApiCall = SteamUGC()->SubmitItemUpdate(updateHandle, "");
	mSteamCallResultUpdateItem.Set(steamApiCall, this, &mlMainWindow::OnUpdateItemResult);

	auto dialog = QProgressDialog{this};
	dialog.setLabelText(QString("Uploading workshop item '%1'...").arg(QString::number(mFileId)));
	dialog.setCancelButton(nullptr);
	dialog.setWindowModality(Qt::WindowModal);
	dialog.setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
	dialog.show();

	for (;;)
	{
		uint64 processed;
		uint64 total;

		//if(Dialog.wasCanceled())
		//{
		//	QMessageBox::warning(this, "Error", QString{"Uploading workshop item was cancelled."});
		//	break;
		//}

		const auto status = SteamUGC()->GetItemUpdateProgress(steamApiCall, &processed, &total);
		// if we get an invalid status exit out, it could mean we're finished or there's an actual problem
		if (status == k_EItemUpdateStatusInvalid)
		{
			break;
		}

		switch (status)
		{
		case EItemUpdateStatus::k_EItemUpdateStatusInvalid:
			dialog.setLabelText(
				QString("Uploading workshop item '%1': %2").arg(QString::number(mFileId), QString{"Invalid"}));
			break;
		case EItemUpdateStatus::k_EItemUpdateStatusPreparingConfig:
			dialog.setLabelText(
				QString("Uploading workshop item '%1': %2").arg(QString::number(mFileId), QString{"Preparing Config"}));
			break;
		case EItemUpdateStatus::k_EItemUpdateStatusPreparingContent:
			dialog.setLabelText(
				QString("Uploading workshop item '%1': %2").arg(QString::number(mFileId),
				                                                QString{"Preparing Content"}));
			break;
		case EItemUpdateStatus::k_EItemUpdateStatusUploadingContent:
			dialog.setLabelText(
				QString("Uploading workshop item '%1': %2").arg(QString::number(mFileId),
				                                                QString{"Uploading Content"}));
			break;
		case EItemUpdateStatus::k_EItemUpdateStatusUploadingPreviewFile:
			dialog.setLabelText(
				QString("Uploading workshop item '%1': %2").arg(QString::number(mFileId),
				                                                QString{"Uploading Preview file"}));
			break;
		case EItemUpdateStatus::k_EItemUpdateStatusCommittingChanges:
			dialog.setLabelText(
				QString("Uploading workshop item '%1': %2").
				arg(QString::number(mFileId), QString{"Committing changes"}));
			break;
		}

		dialog.setMaximum(total);
		dialog.setValue(processed);
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		Sleep(100);
	}
}

void mlMainWindow::OnCreateItemResult(CreateItemResult_t* CreateItemResult, bool IOFailure)
{
	if (IOFailure)
	{
		QMessageBox::warning(this, "Error", "Disk Read error.");
		return;
	}

	if (CreateItemResult->m_eResult != k_EResultOK)
	{
		QMessageBox::warning(this, "Error",
		                     QString(
			                     "Error creating Steam Workshop item. Error code: %1\nVisit https://steamerrors.com/ for more information.")
		                     .arg(CreateItemResult->m_eResult));
		return;
	}

	mFileId = CreateItemResult->m_nPublishedFileId;

	UpdateWorkshopItem();
}

void mlMainWindow::OnUpdateItemResult(SubmitItemUpdateResult_t* UpdateItemResult, bool IOFailure)
{
	if (IOFailure)
	{
		QMessageBox::warning(this, "Error", "Disk Read error.");
		return;
	}

	if (UpdateItemResult->m_eResult != k_EResultOK)
	{
		QMessageBox::warning(this, "Error",
		                     QString(
			                     "Error updating Steam Workshop item. Error code: %1\nVisit https://steamerrors.com/ for more information.")
		                     .arg(UpdateItemResult->m_eResult));
		return;
	}

	if (QMessageBox::question(this, "Update",
	                          "Workshop item successfully updated. Do you want to visit the Workshop page for this item now?",
	                          QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
	{
		ShellExecute(nullptr, L"open",
		             QString("steam://url/CommunityFilePage/%1")
		             .arg(QString::number(mFileId)).toStdWString().c_str(),
		             L"", nullptr, SW_SHOWDEFAULT);
	}
}

void mlMainWindow::OnHelpAbout()
{
	QMessageBox::about(this, "About Mod Tools Launcher", "Treyarch Mod Tools Launcher\nCopyright 2016 Treyarch");
}

void mlMainWindow::OnOpenZoneFile()
{
	auto itemList = mFileListWidget->selectedItems();
	if (itemList.isEmpty())
	{
		return;
	}

	auto* item = itemList[0];

	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
	{
		auto mapName = item->text(0);
		ShellExecute(nullptr, L"open",
		             QString("\"%1/usermaps/%2/zone_source/%3.zone\"")
		             .arg(mGamePath, mapName, mapName).toStdWString().c_str(), L"", nullptr, SW_SHOWDEFAULT);
	}
	else
	{
		auto modName = item->parent()->text(0);
		auto zoneName = item->text(0);
		ShellExecute(nullptr, L"open",
		             QString("\"%1/mods/%2/zone_source/%3.zone\"")
		             .arg(mGamePath, modName, zoneName).toStdWString().c_str(), L"", nullptr, SW_SHOWDEFAULT);
	}
}

void mlMainWindow::OnOpenModRootFolder()
{
	auto itemList = mFileListWidget->selectedItems();
	if (itemList.isEmpty())
	{
		return;
	}

	auto* item = itemList[0];

	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
	{
		auto mapName = item->text(0);
		ShellExecute(nullptr, L"open",
		             QString("\"%1/usermaps/%2\"").arg(mGamePath, mapName, mapName).toStdWString().c_str(), L"",
		             nullptr,
		             SW_SHOWDEFAULT);
	}
	else
	{
		auto modName = item->parent() != nullptr ? item->parent()->text(0) : item->text(0);
		ShellExecute(nullptr, L"open", QString("\"%1/mods/%2\"").arg(mGamePath, modName).toStdWString().c_str(), L"",
		             nullptr, SW_SHOWDEFAULT);
	}
}

void mlMainWindow::OnRunMapOrMod()
{
	auto itemList = mFileListWidget->selectedItems();
	if (itemList.isEmpty())
	{
		return;
	}

	auto* item = itemList[0];
	auto args = QStringList{};

	if (!mRunDvars.isEmpty())
	{
		args << mRunDvars;
	}

	args << "+set" << "fs_game";

	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
	{
		const auto mapName = item->text(0);
		args << mapName;
		args << "+devmap" << mapName;
	}
	else
	{
		const auto modName = item->parent() != nullptr ? item->parent()->text(0) : item->text(0);
		args << modName;
	}

	const auto extraOptions = mRunOptionsWidget->text();
	if (!extraOptions.isEmpty())
	{
		args << extraOptions.split(' ');
	}

	auto commands = QList<QPair<QString, QStringList>>{};
	commands.append(QPair<QString, QStringList>(QString("%1BlackOps3.exe").arg(mGamePath), args));
	StartBuildThread(commands);
}

void mlMainWindow::OnCleanXPaks()
{
	auto itemList = mFileListWidget->selectedItems();
	if (itemList.isEmpty())
	{
		return;
	}

	auto* item = itemList[0];
	auto folder = QString{};

	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
	{
		auto mapName = item->text(0);
		folder = QString("%1/usermaps/%2").arg(mGamePath, mapName);
	}
	else
	{
		auto modName = item->parent() != nullptr ? item->parent()->text(0) : item->text(0);
		folder = QString("%1/mods/%2").arg(mGamePath, modName);
	}

	auto fileListString = QString{};
	auto fileList = QStringList{};
	QDirIterator it(folder, QStringList() << "*.xpak", QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
	{
		QString filepath = it.next();
		fileList.append(filepath);
		fileListString.append("\n" + QDir(folder).relativeFilePath(filepath));
	}

	const auto relativeFolder = QDir(mGamePath).relativeFilePath(folder);

	if (fileList.count() == 0)
	{
		QMessageBox::information(this, QString("Clean XPaks (%1)").arg(relativeFolder),
		                         QString("There are no XPak's to clean!"));
		return;
	}

	if (QMessageBox::question(this, QString("Clean XPaks (%1)").arg(relativeFolder),
	                          QString("Are you sure you want to delete the following files?" + fileListString),
	                          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
	{
		return;
	}

	for (const auto& file : fileList)
	{
		qDebug() << file;
		QFile(file).remove();
	}
}

void mlMainWindow::OnDelete()
{
	auto itemList = mFileListWidget->selectedItems();
	if (itemList.isEmpty())
	{
		return;
	}

	auto* item = itemList[0];
	auto folder = QString{};

	if (item->data(0, Qt::UserRole).toInt() == ML_ITEM_MAP)
	{
		auto mapName = item->text(0);
		folder = QString("%1/usermaps/%2").arg(mGamePath, mapName);
	}
	else
	{
		auto modName = item->parent() != nullptr ? item->parent()->text(0) : item->text(0);
		folder = QString("%1/mods/%2").arg(mGamePath, modName);
	}

	if (QMessageBox::question(this, "Delete Folder",
	                          QString("Are you sure you want to delete the folder '%1' and all of its contents?").
	                          arg(folder), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
	{
		return;
	}

	QDir(folder).removeRecursively();
	PopulateFileList();
}

void mlMainWindow::OnExport2BinChooseDirectory() const
{
	const auto dir = QFileDialog::getExistingDirectory(mExport2BinGUIWidget, tr("Open Directory"), mToolsPath,
	                                                      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	this->mExport2BinTargetDirWidget->setText(dir);

	auto settings = QSettings{};
	settings.setValue("Export2Bin_TargetDir", dir);
}

void mlMainWindow::OnExport2BinToggleOverwriteFiles() const
{
	auto settings = QSettings{};
	settings.setValue("Export2Bin_OverwriteFiles", mExport2BinOverwriteWidget->isChecked());
}

void mlMainWindow::BuildOutputReady(const QString& Output) const
{
	mOutputWidget->appendPlainText(Output);
}

void mlMainWindow::BuildFinished()
{
	mBuildButton->setText("Build");
	mBuildThread->deleteLater();
	mBuildThread = nullptr;
}

Export2BinGroupBox::Export2BinGroupBox(QWidget* parent, mlMainWindow* parent_window) : QGroupBox(parent),
                                                                                       parentWindow(parent_window)
{
	this->setAcceptDrops(true);
}

void Export2BinGroupBox::dragEnterEvent(QDragEnterEvent* event)
{
	event->acceptProposedAction();
}

void Export2BinGroupBox::dropEvent(QDropEvent* event)
{
	const auto* mimeData = event->mimeData();

	if (parentWindow == nullptr)
	{
		return;
	}

	if (mimeData->hasUrls())
	{
		auto pathList = QStringList{};
		auto urlList = mimeData->urls();

		auto workingDir = QDir{parentWindow->mToolsPath};
		for (const auto& i : urlList)
		{
			pathList.append(i.toLocalFile());
		}

		auto* process = new QProcess();
		connect(process, SIGNAL(finished(int)), process, SLOT(deleteLater()));

		const auto allowOverwrite = this->parentWindow->mExport2BinOverwriteWidget->isChecked();

		auto outputDir = parentWindow->mExport2BinTargetDirWidget->text();
		parentWindow->StartConvertThread(pathList, outputDir, allowOverwrite);

		event->acceptProposedAction();
	}
}

void Export2BinGroupBox::dragLeaveEvent(QDragLeaveEvent* event)
{
	event->accept();
}
