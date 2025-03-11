//
// Copyright (C) 2013-2018 University of Amsterdam
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public
// License along with this program.  If not, see
// <http://www.gnu.org/licenses/>.
//

#include <Rcpp.h>
using namespace Rcpp;

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQuickItem>
#include <QDir>
#include <QQuickStyle>
#include <QThread>
#include "qutils.h"
#include "preferencesmodelbase.h"
#include "jasptheme.h"
#include "controls/jaspcontrol.h"
#include "knownissues.h"
#include "qmlutils.h"
#include "columnutils.h"
#include "DataSetProvider.h"
#include "enginebase.h"
#include "rbridge.h"
#include "processinfo.h"
#include "tempfiles.h"

#include <QtPlugin>
Q_IMPORT_PLUGIN(JASP_ControlsPlugin)

#define _STRINGIZE(x) #x
#define STRINGIZE(x) _STRINGIZE(x)


static bool								gl_initialized					= false;
static QString							gl_qt_install_dir;
static QGuiApplication			*		gl_application					= nullptr;
static QQmlApplicationEngine	*		gl_qmlEngine					= nullptr;
static EngineBase				*		gl_jaspEngine					= nullptr;
static ColumnEncoder			*		gl_extraEncodings				= nullptr;

static bool								gl_param_dbInMemory				= true;
static bool								gl_param_preloadData			= true;
static bool								gl_param_orderLabelsByValue		= true;
static int								gl_param_threshold				= 10;
static std::string						gl_param_resultFont				=
#ifdef WIN32
	"Arial,sans-serif,freesans,\"Segoe UI\"";
#elif __APPLE__
	"\"Lucida Grande\",Helvetica,Arial,sans-serif,\"Helvetica Neue\",freesans";
#else
	"freesans,sans-serif";
#endif



// [[Rcpp::export]]
bool setParameter(String name, SEXP value)
{
	std::string nameStr		= name.get_cstring();

	if (nameStr == "resultFont" && Rcpp::is<String>(value))
	{
		gl_param_resultFont = Rcpp::as<std::string>(value);
		return true;
	}
	else if (nameStr == "dbInMemory" && Rcpp::is<bool>(value))
	{
		gl_param_dbInMemory = Rcpp::as<bool>(value);
		return true;
	}
	else if (nameStr == "preloadData" && Rcpp::is<bool>(value))
	{
		gl_param_preloadData = Rcpp::as<bool>(value);
		return true;
	}
	else if (nameStr == "threshold" && Rcpp::is<int>(value))
	{
		gl_param_threshold = Rcpp::as<int>(value);
		return true;
	}
	else if (nameStr == "orderLabelsByValue" && Rcpp::is<bool>(value))
	{
		gl_param_orderLabelsByValue = Rcpp::as<bool>(value);
		return true;
	}

	return false;
}


template<int RTYPE>  inline std::string RVectorEntry_to_String(Rcpp::Vector<RTYPE> obj, int row) { return ""; }

template<> inline std::string RVectorEntry_to_String<INTSXP>(Rcpp::Vector<INTSXP> obj, int row)
{
	return obj[row] == NA_INTEGER	? "" : std::to_string((int)(obj[row]));
}

template<> inline std::string RVectorEntry_to_String<LGLSXP>(Rcpp::Vector<LGLSXP> obj, int row)
{
	return obj[row] == NA_LOGICAL	? "" : ((bool)(obj[row]) ? "1" : "0");
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


void addMsg(Json::Value& jsonResult, const std::string& msg, const std::string& type)
{
	std::string errorMsg;
	if (jsonResult.isMember(type))
		errorMsg = jsonResult[type].toStyledString() + "\n";
	errorMsg += msg;

	jsonResult[type] = errorMsg;
}

void addError(Json::Value& jsonResult, const std::string& msg)
{
	addMsg(jsonResult, msg, "error");
}

void addInfo(Json::Value& jsonResult, const std::string& msg)
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
		engine->rootContext()->setContextProperty("jaspTheme",				defaultJaspTheme);
	}

	qmlRegisterUncreatableMetaObject(JASPControl::staticMetaObject, // static meta object
									 "JASP.Controls",        // import statement
									 0, 1,                   // major and minor version of the import
									 "JASP",                 // name in QML
									 "Error: only enums");
	if (!KnownIssues::issues())
		new KnownIssues();

}

void init(Json::Value& output)
{
	if (gl_initialized) return;
	gl_initialized = true;

	TempFiles::init(ProcessInfo::currentPID());

	DataSetProvider::getProvider(gl_param_dbInMemory, false); // Create the DataSetProvider in case the loadDataSet was not already called

	gl_jaspEngine = new EngineBase(ProcessInfo::currentPID(), gl_param_dbInMemory);
	gl_extraEncodings = new ColumnEncoder("JaspExtraOptions_");

	rbridge_init(gl_jaspEngine, [](const char *msg){ }, [](){ return false; }, gl_extraEncodings, gl_param_resultFont.c_str());

	jaspRCPP_init_jaspBase(false);

	gl_qt_install_dir = qgetenv("QT_DIR");
#ifdef QT_DIR
	if (gl_qt_install_dir.isEmpty())
		gl_qt_install_dir = STRINGIZE(QT_DIR);
#endif
	addInfo(output, "QT_DIR found in environment: " + fq(gl_qt_install_dir));


	QString rHome = qgetenv("R_HOME");
	addInfo(output, "R_HOME: " + fq(rHome));

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

	gl_application = new QGuiApplication(argc, argvs);
	gl_qmlEngine = new QQmlApplicationEngine();

	addContextObjects(gl_qmlEngine);

	gl_qmlEngine->addImportPath(":/jasp-stats.org/imports");
	gl_qmlEngine->rootContext()->setContextProperty("NO_DESKTOP_MODE",	true);
	QQuickStyle::setStyle("Basic"); // This removes warnings "The current style does not support customization of this control"

}

// [[Rcpp::export]]
void loadDataSet(Rcpp::List data)
{
	Json::Value output;
	init(output);
	DataSetProvider* provider = DataSetProvider::getProvider(gl_param_dbInMemory);

	Rcpp::RObject namesListRObject = data.names();
	Rcpp::CharacterVector namesList;

	if (!namesListRObject.isNULL())
		namesList = namesListRObject;

	std::string hello;


	provider->dataSet()->setColumnCount(data.size());

	int maxRows = 0;

	for (int colNr = 0; colNr < data.size(); colNr++)
	{
		std::string name(namesList[colNr]);
		std::vector<std::string> column;

		if(name == "")
			name = "column_" + std::to_string(colNr);

		Rcpp::RObject colObj = (Rcpp::RObject)data[colNr];

		// TODO: This could be made more efficient: if we have integers or doubles, initialize the column in the dataset directly with these integers and doubles
		// Now we transform them into strings, and inside initColumnWithStrings re-transform them into integers or doubles.
		if(Rcpp::is<Rcpp::NumericVector>(colObj))			column = readCharacterVector<REALSXP>((Rcpp::NumericVector)colObj);
		else if(Rcpp::is<Rcpp::IntegerVector>(colObj))		column = readCharacterVector<INTSXP>((Rcpp::IntegerVector)colObj);
		else if(Rcpp::is<Rcpp::LogicalVector>(colObj))		column = readCharacterVector<LGLSXP>((Rcpp::LogicalVector)colObj);
		else if(Rcpp::is<Rcpp::CharacterVector>(colObj))	column = readCharacterVector<STRSXP>((Rcpp::CharacterVector)colObj);
		else if(Rcpp::is<Rcpp::StringVector>(colObj))		column = readCharacterVector<STRSXP>((Rcpp::StringVector)colObj);
		else
		{
			// TODO print an error
			column = std::vector<std::string>(maxRows);
		}

		if (column.size() > maxRows)
			maxRows = column.size();
		if (provider->dataSet()->rowCount() < maxRows)
			provider->dataSet()->setRowCount(maxRows);

		provider->dataSet()->initColumnWithStrings(colNr, name, column, {}, name, columnType::unknown, {}, gl_param_threshold, gl_param_orderLabelsByValue);
	}

	ColumnEncoder::columnEncoder()->setCurrentNames(provider->dataSet()->getColumnNames(), true);
}


// [[Rcpp::export]]
String loadQmlFileAndCheckOptions(String moduleName, String analysisName, String qmlFile, String options, String version, bool preloadData)
{
	bool hasError = false;
	Json::Value jsonResult;
	init(jsonResult);

//	gl_qmlEngine->clearSingletons();
//	gl_qmlEngine->clearComponentCache();

	std::string qmlFileStr		= qmlFile.get_cstring(),
				optionsStr		= options.get_cstring(),
				versionStr		= version.get_cstring(),
				analysisNameStr	= analysisName.get_cstring(),
				moduleNameStr	= moduleName.get_cstring();

	QFileInfo	qmlFileInfo(QString::fromStdString(qmlFileStr));
	if (!qmlFileInfo.exists())
	{
		hasError = true;
		addError(jsonResult, "File NOT found");
	}

	QQuickItem* item = nullptr;
	if (!hasError)
	{
		QUrl urlFile = QUrl::fromLocalFile(qmlFileInfo.absoluteFilePath());
		QQmlComponent	qmlComp( gl_qmlEngine, urlFile, QQmlComponent::PreferSynchronous);

		item = qobject_cast<QQuickItem*>(qmlComp.create());

		for(const auto & error : qmlComp.errors())
		{
			hasError = true;
			addError(jsonResult, "Error when creating component at " + fq(QString::number(error.line())) + "," + fq(QString::number(error.column())) + ": " + fq(error.description()));
		}

		if (!item)
		{
			hasError = true;
			addError(jsonResult, "Item not created");
		}
	}

	if (hasError)
		return jsonResult.toStyledString();

	gl_application->processEvents();

	QString returnedValue;

	QMetaObject::invokeMethod(item, "parseOptions",
		Q_RETURN_ARG(QString, returnedValue),
		Q_ARG(QString, tq(optionsStr)));

	Json::Reader	jsonReader;
	jsonReader.parse(fq(returnedValue), jsonResult);
	Json::Value		jsonOptions = jsonResult["options"];

	gl_extraEncodings->setCurrentNamesFromOptionsMeta(jsonOptions);
	gl_jaspEngine->updateOptionsAccordingToMeta(jsonOptions);
	ColumnEncoder::colsPlusTypes analysisColsTypes = ColumnEncoder::encodeColumnNamesinOptions(jsonOptions, preloadData);

	static int analysisRevision = 0;
	analysisRevision++;

	// This does not call the analysis, but sets some configuration settings
	rbridge_runModuleCall(analysisNameStr, analysisNameStr, moduleNameStr, "{}",
												   jsonOptions.toStyledString(), "{}", 1, analysisRevision,
												   false, analysisColsTypes, preloadData, false);

	jsonResult["options"] = jsonOptions;

	return jsonResult.toStyledString();
}

bool _generateWrapper(Json::Value& jsonResult, const QString& modulePath, const QString& analysisName, const QString& qmlFileName, bool preloadData)
{
	bool hasError = false;
	QQuickItem* item = nullptr;
	QFileInfo	qmlFile(modulePath + "/inst/qml/" + qmlFileName);

	if (!qmlFile.exists())
	{
		hasError = true;
		addError(jsonResult, "QML File NOT found: " + fq(qmlFile.absoluteFilePath()));
	}
	else
	{
		QUrl urlFile = QUrl::fromLocalFile(qmlFile.absoluteFilePath());
		QQmlComponent	qmlComp( gl_qmlEngine, urlFile, QQmlComponent::PreferSynchronous);

		item = qobject_cast<QQuickItem*>(qmlComp.create());

		for(const auto & error : qmlComp.errors())
		{
			hasError = true;
			addError(jsonResult, "Error when creating component at " + fq(QString::number(error.line())) + "," + fq(QString::number(error.column())) + ": " + fq(error.description()));
		}

		if (!item)
		{
			hasError = true;
			addError(jsonResult, "Item not created");
		}
	}

	if (!hasError)
	{
		QString returnedValue;
		QString moduleName = QDir(modulePath).dirName();

		gl_application->processEvents();

		QMetaObject::invokeMethod(item, "generateWrapper",
			Q_RETURN_ARG(QString, returnedValue),
			Q_ARG(QString, moduleName),
			Q_ARG(QString, analysisName),
			Q_ARG(QString, qmlFileName),
			Q_ARG(bool, preloadData)
		);

		QFile file(modulePath + "/R/" + analysisName + "Wrapper.R");
		if (file.open(QIODevice::ReadWrite)) {
			QTextStream stream(&file);
			stream << returnedValue;
		}
	}

	return !hasError;
}

// [[Rcpp::export]]
String generateModuleWrappers(String modulePath)
{
	bool hasError = false;
	Json::Value jsonResult;
	init(jsonResult);

	gl_qmlEngine->clearComponentCache();

	QString modulePathQ		= tq(modulePath.get_cstring()),
			moduleNameQ;

	QDir moduleDir(modulePathQ);

	if (!moduleDir.exists())
	{
		hasError = true;
		addError(jsonResult, "Module path not found: " + fq(modulePathQ));
	}

	QVector<std::pair<QString, QString> > analyses;
	if (!hasError)
	{
		QFile qmlDescriptionFile(modulePathQ + "/inst/Description.qml");
		if (!qmlDescriptionFile.exists())
		{
			hasError = true;
			addError(jsonResult, "Description.qml file not found in " + fq(modulePathQ));
		}
		else
		{
			QString fileContent;
			QStringList analysesPart;
			// TODO: The Description.qml cannot be loaded by the QML Engine, since it does not have access to the JASP.Module
			// If JASP.Module is set as a a Qt module/plugin (as JASP.Controls), then we could load it and uses the Description object directly.
			// For the time being, just try to parse the Description.qml to detect the Analyses and their properties.
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
		hasError = !_generateWrapper(jsonResult, modulePathQ, analysis.first, analysis.second, gl_param_preloadData);
	}

	if (hasError)
		return jsonResult["error"].toStyledString();
	else
		return fq(result);
}


// [[Rcpp::export]]
String generateAnalysisWrapper(String modulePath, String qmlFileName, String analysisName, bool preloadData)
{
	bool hasError = false;
	Json::Value jsonResult;
	init(jsonResult);

	gl_qmlEngine->clearComponentCache();

	QString qmlFileNameQ	= tq(qmlFileName.get_cstring()),
			modulePathQ		= tq(modulePath.get_cstring()),
			moduleNameQ,
			analysisNameQ	= tq(analysisName.get_cstring());

	QDir moduleDir(modulePathQ);

	if (!moduleDir.exists())
		addError(jsonResult, "Module path not found: " + fq(modulePathQ));

	if (!hasError)
		// If JASP.Module is set as a a Qt module/plugin (as JASP.Controls), then we could load it and uses the Description object directly, and know directly
		// what is the name of the qml file and if it uses preloadData
		hasError = !_generateWrapper(jsonResult, modulePathQ, analysisNameQ, qmlFileNameQ, preloadData);

	if (!hasError)
		return "Wrapper generated for analysis " + fq(analysisNameQ);
	else
		return jsonResult["error"].toStyledString();
}







