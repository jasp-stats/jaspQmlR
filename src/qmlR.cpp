
#include <Rcpp.h>
using namespace Rcpp;

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <iostream>
#include <QQmlContext>
#include <QThread>
#include <QFileInfo>
#include <QAbstractEventDispatcher>
#include <QQmlComponent>
#include <QQuickItem>
#include <QDir>
#include <filesystem>
#include <QJsonDocument>
#include <QJsonObject>

#define _STRINGIZE(x) #x
#define STRINGIZE(x) _STRINGIZE(x)

static bool initialized							= false;
static std::string qt_install_dir;
static std::string jasp_qmlcomponents_dir;
static QGuiApplication* application				= nullptr;
static QQmlApplicationEngine* engine			= nullptr;
static bool hasError							= false;

void printFolder(StringVector& output, const QDir& dir, int depth = 0)
{
	for (QFileInfo info : dir.entryInfoList())
	{
		QString text = QString("Niveau %1: ").arg(depth);
		text += info.baseName() + ", " + info.absoluteFilePath() + ", " + info.canonicalFilePath() + ": " + (info.isDir() ? "Folder" : (info.isFile() ? "file" : "???"));
		output.push_back(text.toStdString());
		if (info.isDir() && info.baseName() != "qt-project")
		{
			QDir dir2(info.absoluteFilePath());
			printFolder(output, dir2, depth + 1);
		}
		else if (info.baseName() == "qmldir")
		{
			QFile inputFile(info.absoluteFilePath());
			if (inputFile.open(QIODevice::ReadOnly))
			{
			   QTextStream in(&inputFile);
			   while (!in.atEnd())
				  output.push_back(in.readLine().toStdString());
			   inputFile.close();
			}
		}

	}

}

String getEnv(const std::string& name)
{
	Function f("Sys.getenv('" + name + "')");

	return f();
}

void init(StringVector& output)
{
	if (initialized) return;
	initialized = true;

	qt_install_dir = qgetenv("QT_DIR");
#ifdef QT_DIR
	if (qt_install_dir.empty())
		qt_install_dir = STRINGIZE(QT_DIR);
#endif
	output.push_back("QT_DIR found in environment: " + qt_install_dir);

	jasp_qmlcomponents_dir = qgetenv("JASP_QML_PLUGIN_DIR");
#ifdef JASP_QML_PLUGIN_DIR
	if (jasp_qmlcomponents_dir.empty())
		jasp_qmlcomponents_dir = STRINGIZE(JASP_QML_PLUGIN_DIR);
#endif
	output.push_back("JASP_QML_PLUGIN_DIR found in environment: " + jasp_qmlcomponents_dir);

	QString rHome = qgetenv("R_HOME");
	output.push_back("R_HOME: " + rHome.toStdString());

	//QString qmlRFolder = rHome + "/library/jaspQmlR";
	//QCoreApplication::addLibraryPath(qmlRFolder + "/plugins");


	int					dummyArgc = 1;
	char				dummyArgv[2];
	dummyArgv[0] = '?';
	dummyArgv[1] = '\0';

	//const char*	platformArg = "-platform";
	//const char*	platformOpt = "minimal"; //"cocoa";

	std::vector<const char*> arguments = {}; //{qmlR, platformArg, platformOpt};

	int		argc = arguments.size();
	char** argvs = new char*[argc];

	for (int i = 0; i < argc; i++)
	{
		argvs[i] = new char[strlen(arguments[i]) + 1];
		memset(argvs[i], '\0',				strlen(arguments[i]) + 1);
		memcpy(argvs[i], arguments[i],		strlen(arguments[i]));
		argvs[i][							strlen(arguments[i])] = '\0';
	}


	//qputenv("QT_QPA_PLATFORM", "cocoa");
	//qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", qmlRFolder.toStdString() + "/plugins");

	application = new QGuiApplication(argc, argvs);

	engine = new QQmlApplicationEngine();

	engine->addImportPath(QString::fromStdString(jasp_qmlcomponents_dir));

	/*output.push_back("Base URL: " + engine->baseUrl().toDisplayString().toStdString());
	output.push_back("Current Path: " + std::filesystem::current_path().string());

	QStringList paths = engine->importPathList();
	for (QString path : paths)
		output.push_back("Import path: " + path.toStdString());
	QStringList pluginPaths = engine->pluginPathList();
	for (QString path : pluginPaths)
		output.push_back("Plugin path: " + path.toStdString());

	QDir dir(":/");

	printFolder(output, dir); */
}

void addError(QJsonObject& jsonResult, QString msg)
{
	QString errorMsg;
	if (jsonResult.contains("error"))
		errorMsg = jsonResult["error"].toString() + "\n";
	errorMsg += msg;

	jsonResult["error"] = errorMsg;
}

// [[Rcpp::export]]

String checkOptions(String jsonValue)
{
	hasError = false;
	StringVector output;
	init(output);

	engine->clearComponentCache();
	QJsonObject jsonResult;

	std::string jsonStr = jsonValue.get_cstring();
	QJsonDocument jsonDoc = QJsonDocument::fromJson(QString::fromStdString(jsonStr).toUtf8());
	QJsonObject jsonInput = jsonDoc.object();

	QJsonObject optionsJson = jsonInput["options"].toObject();
	QJsonValue qmlFileJson = jsonInput["qmlFile"];

	QFileInfo	qmlFile(qmlFileJson.toString());
	if (!qmlFile.exists())
		addError(jsonResult, "File NOT found");

	QQuickItem* item = nullptr;
	if (!hasError)
	{
		QUrl urlFile = QUrl::fromLocalFile(qmlFile.absoluteFilePath());
		QQmlComponent	qmlComp( engine, urlFile, QQmlComponent::PreferSynchronous);

		item = qobject_cast<QQuickItem*>(qmlComp.create());

		for(const auto & error : qmlComp.errors())
			addError(jsonResult, "Error when creating component at " + QString::number(error.line()) + "," + QString::number(error.column()) + ": " + error.description());

		if (!item)
			addError(jsonResult, "Item not created");
	}

	if (!hasError)
	{
		application->processEvents();

		QJsonDocument docOptions(optionsJson);
		QString returnedValue;
		QMetaObject::invokeMethod(item, "parseOptions",
			Q_RETURN_ARG(QString, returnedValue),
			Q_ARG(QString, docOptions.toJson()));

		QJsonDocument docReturned = QJsonDocument::fromJson(returnedValue.toUtf8());

		jsonResult["options"] = docReturned.object();
	}

	QJsonDocument docResult(jsonResult);
	std::string srtrResult = docResult.toJson().toStdString();
	return srtrResult;
}


StringVector runQml(String qmlFileName, String options, String data)
{
	StringVector output;
	init(output);

	engine->clearComponentCache();

	std::string qmlFileNameStr = qmlFileName.get_cstring();
	std::string optionsStr = options.get_cstring();
	std::string dataStr = data.get_cstring();

	if (qmlFileNameStr.substr(qmlFileNameStr.length() - 4, 4) != ".qml")
		qmlFileNameStr += ".qml";

	output.push_back("File: " + qmlFileNameStr);

	QFileInfo	qmlFile(QString::fromStdString(qmlFileNameStr));
	if (!qmlFile.exists())
	{
		output.push_back("File NOT found");
		return output;
	}

	output.push_back("Found file");

	QUrl urlFile = QUrl::fromLocalFile(qmlFile.absoluteFilePath());
	QQmlComponent	qmlComp( engine, urlFile, QQmlComponent::PreferSynchronous);

	QQuickItem* item = qobject_cast<QQuickItem*>(qmlComp.create());

	for(const auto & error : qmlComp.errors())
		output.push_back("Error when creating component at " + std::to_string(error.line()) + "," + std::to_string(error.column()) + ": " + error.description().toStdString());

	if (!item)
	{
		output.push_back("Item not created");
		return output;
	}

	output.push_back("Item created!!!");

	application->processEvents();

	QString returnedValue;
	/*QMetaObject::invokeMethod(item, "parseOptions",
		Q_RETURN_ARG(QString, returnedValue),
		Q_ARG(QString, QString::fromStdString(optionsStr)),
		Q_ARG(QString, QString::fromStdString(dataStr)));
	output.push_back("OPTIONS: " + returnedValue.toStdString());
	*/

	QString myProp = item->property("info").toString();
	output.push_back("info: " + myProp.toStdString());


	return output;
}
