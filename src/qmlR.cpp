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
#include <QQuickStyle>
#include "qutils.h"
#include "preferencesmodelbase.h"
#include "jasptheme.h"
#include "controls/jaspcontrol.h"
#include <qdebug.h>
#include "knownissues.h"
#include "qmlutils.h"
#include "dataset.h"
#include "databaseinterface.h"
#include "columnutils.h"

#include <QtPlugin>
Q_IMPORT_PLUGIN(JASP_ControlsPlugin)

#define _STRINGIZE(x) #x
#define STRINGIZE(x) _STRINGIZE(x)


static bool initialized							= false;
static QString qt_install_dir;
static QString jasp_qmlcomponents_dir;
static QGuiApplication* application				= nullptr;
static QQmlApplicationEngine* engine			= nullptr;
static bool hasError							= false;


static DatabaseInterface*	_db					= nullptr;
static DataSet*				_dataset			= nullptr;


// [[Rcpp::export]]
String test(Rcpp::DataFrame data)
{

	Rcpp::CharacterVector names  = data.names();

	int i = 0;

	return "Hallo";


}



template<int RTYPE>  inline std::string RVectorEntry_to_String(Rcpp::Vector<RTYPE> obj, int row) { return ""; }

template<> inline std::string RVectorEntry_to_String<INTSXP>(Rcpp::Vector<INTSXP> obj, int row)
{
	return obj[row] == NA_INTEGER	? "" : std::to_string((int)(obj[row]));
}

template<> inline std::string RVectorEntry_to_String<LGLSXP>(Rcpp::Vector<LGLSXP> obj, int row)
{
	return obj[row] == NA_LOGICAL	? "" : std::to_string((bool)(obj[row]));
}

template<> inline std::string RVectorEntry_to_String<STRSXP>(Rcpp::Vector<STRSXP> obj, int row)
{
	return obj[row] == NA_STRING	? "" : std::string(obj[row]);
}

template<> inline std::string RVectorEntry_to_String<REALSXP>(Rcpp::Vector<REALSXP> obj, int row)
{
	double val = static_cast<double>(obj[row]);
	return	R_IsNA(val) ? "" :
					  R_IsNaN(val) ? "NaN" :
										  val == std::numeric_limits<double>::infinity() ? "\u221E" :
												val == -1 * std::numeric_limits<double>::infinity() ? "-\u221E"  :
											ColumnUtils::doubleToString((double)(obj[row]));
}


template<int RTYPE>
std::vector<std::string> readCharacterVector(Rcpp::Vector<RTYPE>	obj)
{
	std::vector<std::string> vecresult;
	for(int row=0; row<obj.size(); row++)
		vecresult.push_back(RVectorEntry_to_String(obj, row));

	return vecresult;
}

// [[Rcpp::export]]
void loadDataSet(Rcpp::List data)
{
	if (_db == nullptr)
		_db	= new DatabaseInterface(true, true);

	if (_dataset)
		delete _dataset;

	_dataset = new DataSet();


	Rcpp::RObject namesListRObject = data.names();
	Rcpp::CharacterVector namesList;

	if (!namesListRObject.isNULL())
		namesList = namesListRObject;

	std::string hello;


	_dataset->setColumnCount(data.size());

	for (int colNr = 0; colNr < data.size(); colNr++)
	{
		std::string name(namesList[colNr]);
		std::vector<std::string> column;

		if(name == "")
			name = "column_" + std::to_string(colNr);

		Rcpp::RObject colObj = (Rcpp::RObject)data[colNr];

		if(Rcpp::is<Rcpp::NumericVector>(colObj))			column = readCharacterVector<REALSXP>((Rcpp::NumericVector)colObj);
		else if(Rcpp::is<Rcpp::IntegerVector>(colObj))		column = readCharacterVector<INTSXP>((Rcpp::IntegerVector)colObj);
		else if(Rcpp::is<Rcpp::LogicalVector>(colObj))		column = readCharacterVector<LGLSXP>((Rcpp::LogicalVector)colObj);
		else if(Rcpp::is<Rcpp::CharacterVector>(colObj))	column = readCharacterVector<STRSXP>((Rcpp::CharacterVector)colObj);
		else if(Rcpp::is<Rcpp::StringVector>(colObj))		column = readCharacterVector<STRSXP>((Rcpp::StringVector)colObj);

		if (colNr == 0)
			_dataset->setRowCount(column.size());

		_dataset->initColumnWithStrings(colNr, name, column, {}, name, columnType::unknown, {}, 10, true);
	}

}


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


void addMsg(QJsonObject& jsonResult, const QString& msg, const QString& type)
{
	QString errorMsg;
	if (jsonResult.contains(type))
		errorMsg = jsonResult[type].toString() + "\n";
	errorMsg += msg;

	jsonResult[type] = errorMsg;
}

void addError(QJsonObject& jsonResult, const QString& msg)
{
	addMsg(jsonResult, msg, "error");
	hasError = true;
}

void addInfo(QJsonObject& jsonResult, const QString& msg)
{
	addMsg(jsonResult, msg, "info");
}

void addContextObjects(QQmlApplicationEngine* engine)
{
	QLocale::setDefault(QLocale(QLocale::English)); // make decimal points == .

	QmlUtils::setGlobalPropertiesInQMLContext(engine->rootContext());

	PreferencesModelBase* prefModel = engine->rootContext()->contextProperty("preferencesModel").value<PreferencesModelBase*>();
	if (prefModel == nullptr)
	{
		prefModel = new PreferencesModelBase();
		engine->rootContext()->setContextProperty("preferencesModel",		prefModel);
	}

	if (engine->rootContext()->contextProperty("jaspTheme").isNull())
	{
		JaspTheme* defaultJaspTheme = new JaspTheme();
		defaultJaspTheme->setIconPath("/default/");
		engine->rootContext()->setContextProperty("jaspTheme",				defaultJaspTheme	);
	}

	qmlRegisterUncreatableMetaObject(JASPControl::staticMetaObject, // static meta object
									 "JASP.Controls",        // import statement
									 0, 1,                   // major and minor version of the import
									 "JASP",                 // name in QML
									 "Error: only enums");
	if (!KnownIssues::issues())
		new KnownIssues();

}

void init(QJsonObject& output)
{
	if (initialized) return;
	initialized = true;

	qt_install_dir = qgetenv("QT_DIR");
#ifdef QT_DIR
	if (qt_install_dir.isEmpty())
		qt_install_dir = STRINGIZE(QT_DIR);
#endif
	addInfo(output, "QT_DIR found in environment: " + qt_install_dir);


	QString rHome = qgetenv("R_HOME");
	addInfo(output, "R_HOME: " + rHome);

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


	qputenv("QT_QPA_PLATFORM", "minimal");
	// Keep this in case
	// qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", qmlRFolder.toStdString() + "/plugins");


	application = new QGuiApplication(argc, argvs);
	engine = new QQmlApplicationEngine();

	addContextObjects(engine);

	engine->addImportPath(":/jasp-stats.org/imports");
	engine->rootContext()->setContextProperty("NO_DESKTOP_MODE",	true);
	QQuickStyle::setStyle("Basic"); // This removes warnings "The current style does not support customization of this control"

	//new RDataSetReader(engine);

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

// [[Rcpp::export]]
String loadQmlFileAndCheckOptions(String qmlFile, String options, String version)
{
	hasError = false;
	QJsonObject jsonResult;
	init(jsonResult);

	engine->clearComponentCache();

	std::string qmlFileStr = qmlFile.get_cstring(),
				optionsStr = options.get_cstring(),
				versionStr = version.get_cstring();

	QFileInfo	qmlFileInfo(QString::fromStdString(qmlFileStr));
	if (!qmlFileInfo.exists())
		addError(jsonResult, "File NOT found");

	QQuickItem* item = nullptr;
	if (!hasError)
	{
		QUrl urlFile = QUrl::fromLocalFile(qmlFileInfo.absoluteFilePath());
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

		QString returnedValue;

		QMetaObject::invokeMethod(item, "parseOptions",
			Q_RETURN_ARG(QString, returnedValue),
			Q_ARG(QString, QString::fromStdString(optionsStr)));

		QJsonDocument docReturned = QJsonDocument::fromJson(returnedValue.toUtf8());

		jsonResult = docReturned.object();
	}

	QJsonDocument docResult(jsonResult);
	std::string srtrResult = docResult.toJson().toStdString();
	return srtrResult;
}

void _generateWrapper(QJsonObject& jsonResult, const QString& modulePath, const QString& analysisName, const QString& qmlFileName)
{
	QQuickItem* item = nullptr;
	QFileInfo	qmlFile(modulePath + "/inst/qml/" + qmlFileName);
	if (!qmlFile.exists())
		addError(jsonResult, "QML File NOT found: " + qmlFile.absoluteFilePath());
	else
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
		QString returnedValue;
		QString moduleName = QDir(modulePath).dirName();

		application->processEvents();

		QMetaObject::invokeMethod(item, "generateWrapper",
			Q_RETURN_ARG(QString, returnedValue),
			Q_ARG(QString, moduleName),
			Q_ARG(QString, analysisName),
			Q_ARG(QString, qmlFileName)
		);

		QFile file(modulePath + "/R/" + analysisName + "Wrapper.R");
		if (file.open(QIODevice::ReadWrite)) {
			QTextStream stream(&file);
			stream << returnedValue;
		}
	}
}

// [[Rcpp::export]]
String generateModuleWrappers(String modulePath)
{
	hasError = false;
	QJsonObject jsonResult;
	init(jsonResult);

	engine->clearComponentCache();

	QString modulePathQ		= tq(modulePath.get_cstring()),
			moduleNameQ;

	QDir moduleDir(modulePathQ);

	if (!moduleDir.exists())
		addError(jsonResult, "Module path not found: " + modulePathQ);

	QVector<std::pair<QString, QString> > analyses;
	if (!hasError)
	{
		QFile qmlDescriptionFile(modulePathQ + "/inst/Description.qml");
		if (!qmlDescriptionFile.exists())
			addError(jsonResult, "Description.qml file not found in " + modulePathQ);
		else
		{
			QString fileContent;
			QStringList analysesPart;
			if (qmlDescriptionFile.open(QIODevice::ReadOnly))
			{
				fileContent = qmlDescriptionFile.readAll();
				analysesPart = fileContent.split("Analysis");
				for (int i = 1; i < analysesPart.length(); i++)
				{
					QStringList lines = analysesPart[i].split("\n");
					QString analysisName, qmlFileName;

					for (QString line : lines)
					{
						line = line.trimmed();
						if (line.startsWith("func"))
							analysisName = line.split(":")[1].trimmed().replace('"', "");
						else if (line.startsWith("qml"))
							qmlFileName = line.split(":")[1].trimmed().replace('"', "");
					}

					if (!analysisName.isEmpty())
					{
						if (qmlFileName.isEmpty())
							qmlFileName = analysisName + ".qml";
						analyses.append(std::make_pair(analysisName, qmlFileName));
					}
				}
			}
		}
	}

	QString result;
	for (auto analysis : analyses)
	{
		result.append("Analysis " + analysis.first + " with qml file " + analysis.second + "\n");
		_generateWrapper(jsonResult, modulePathQ, analysis.first, analysis.second);
	}

	if (hasError)
		return fq(jsonResult["error"].toString());
	else
		return fq(result);
}


// [[Rcpp::export]]
String generateAnalysisWrapper(String modulePath, String qmlFileName, String analysisName)
{
	hasError = false;
	QJsonObject jsonResult;
	init(jsonResult);

	engine->clearComponentCache();

	QString qmlFileNameQ	= tq(qmlFileName.get_cstring()),
			modulePathQ		= tq(modulePath.get_cstring()),
			moduleNameQ,
			analysisNameQ	= tq(analysisName.get_cstring());

	QDir moduleDir(modulePathQ);

	if (!moduleDir.exists())
		addError(jsonResult, "Module path not found: " + modulePathQ);

	if (!hasError)
		_generateWrapper(jsonResult, modulePathQ, analysisNameQ, qmlFileNameQ);

	if (!hasError)
		return "Wrapper generated for analysis " + fq(analysisNameQ);
	else
		return fq(jsonResult["error"].toString());
}







