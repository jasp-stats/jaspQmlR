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

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQuickItem>
#include <QDir>
#include <QQuickStyle>
#include <QThread>
#include "preferencesmodelbase.h"
#include "jasptheme.h"
#include "controls/jaspcontrol.h"
#include "knownissues.h"
#include "qmlutils.h"
#include "columnutils.h"
#include "DataSetProvider.h"
#include "databridge.h"
#include "rbridge.h"
#include "processinfo.h"
#include "tempfiles.h"
#include "analysisbase.h"
#include "analysisform.h"
#include "DataFrameImporter.h"

#include <QtPlugin>
Q_IMPORT_PLUGIN(JASP_ControlsPlugin)

#define _STRINGIZE(x) #x
#define STRINGIZE(x) _STRINGIZE(x)

static bool									gl_initialized					= false;
static QGuiApplication			*			gl_application					= nullptr;
static QQmlApplicationEngine	*			gl_qmlEngine					= nullptr;
static DataBridge				*			gl_dataBridge					= nullptr;
static ColumnEncoder			*			gl_extraEncodings				= nullptr;
static QMap<QString, std::pair<QDateTime, AnalysisForm* > >	gl_qmlFormMap;

static bool									gl_verbose						= true;
static bool									gl_param_dbInMemory				= true;
static bool									gl_param_orderLabelsByValue		= true;
static int									gl_param_threshold				= 10;
static std::string							gl_param_resultFont				=
#ifdef WIN32
	"Arial,sans-serif,freesans,\"Segoe UI\"";
#elif __APPLE__
	"\"Lucida Grande\",Helvetica,Arial,sans-serif,\"Helvetica Neue\",freesans";
#else
	"freesans,sans-serif";
#endif

void blockSignalsRecursive(QObject* item)
{
	for (QObject* obj : item->children())
		blockSignalsRecursive(obj);
	item->blockSignals(true);
}

void deleteQuickItem(QQuickItem* item)
{
	blockSignalsRecursive(item);
	item->setParent(nullptr);
	item->setParentItem(nullptr);
	delete item;
}

// [[Rcpp::export]]
void clearUp()
{
	for (auto value : gl_qmlFormMap.values())
		deleteQuickItem(value.second);

	gl_qmlFormMap.clear();

	if (gl_qmlEngine)
	{
		gl_qmlEngine->clearSingletons();
		gl_qmlEngine->clearComponentCache();
	}
}

// [[Rcpp::export]]
bool setParameter(String name, SEXP value)
{
	std::string nameStr		= name.get_cstring();

	if (nameStr == "resultFont" && Rcpp::is<String>(value))
	{
		gl_param_resultFont = Rcpp::as<std::string>(value);
		return true;
	}
	else if (nameStr == "verbose" && Rcpp::is<bool>(value))
	{
		gl_verbose = Rcpp::as<bool>(value);
		return true;
	}
	else if (nameStr == "dbInMemory" && Rcpp::is<bool>(value))
	{
		gl_param_dbInMemory = Rcpp::as<bool>(value);
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

String getEnv(const std::string& name)
{
	Function f("Sys.getenv('" + name + "')");

	return f();
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

void sendMessage(const char * msg)
{
	if (gl_verbose)
		Rcout << "Send Message: " << msg << std::endl;
}

bool init()
{
	if (gl_initialized) return true;
	gl_initialized = true;

	TempFiles::init(ProcessInfo::currentPID());

	DataSetProvider::getProvider(gl_param_dbInMemory, false); // Create the DataSetProvider in case the loadDataSet was not already called

	gl_dataBridge = new DataBridge(ProcessInfo::currentPID(), gl_param_dbInMemory);
	gl_extraEncodings = new ColumnEncoder("JaspExtraOptions_");

	rbridge_init(gl_dataBridge, sendMessage, [](){ return false; }, gl_extraEncodings, gl_param_resultFont.c_str(), false);

	jaspRCPP_init_jaspBase();

	if (gl_verbose)
	{
		QString qt_install_dir = qgetenv("QT_DIR");
#ifdef QT_DIR
		if (qt_install_dir.isEmpty())
			qt_install_dir = STRINGIZE(QT_DIR);
#endif
		Rcout << "QT_DIR found in environment: " + fq(qt_install_dir) << std::endl;

		QString rHome = qgetenv("R_HOME");
		Rcout << "R_HOME: " << fq(rHome) << std::endl;
	}

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

	return true;
}

// [[Rcpp::export]]
void loadDataSet(Rcpp::List data)
{
	if (!init())
	{
		Rcout << "Error during initialization" << std::endl;
		return;
	}

	DataSetProvider* provider = DataSetProvider::getProvider(gl_param_dbInMemory);

	DataFrameImporter::loadDataSet(provider->dataSet(), data, gl_param_threshold, gl_param_orderLabelsByValue);

	ColumnEncoder::columnEncoder()->setCurrentNames(provider->dataSet()->getColumnNames(), true);
}

void sendRScriptHandler(AnalysisForm* form, QString script, QString controlName, bool whiteListedVersion)
{
	if (gl_verbose)
		Rcout << "R Script " << fq(script) << " sent by " << controlName << std::endl;

	bool hasError = false;
	std::string result = rbridge_evalRCodeWhiteListed(fq(script).c_str(), whiteListedVersion);

	if (result == "")
	{
		hasError = true;
		result = jaspRCPP_getLastErrorMsg();
	}

	if (gl_verbose)
		Rcout << "R Script result " << (hasError ? "has error" : "") << ": " << result << std::endl;

	form->runScriptRequestDone(tq(result), controlName, hasError);
}

AnalysisForm* getQmlForm(const QString& qmlFileStr)
{
	AnalysisForm* qmlForm = nullptr;

	QFileInfo	qmlFileInfo(qmlFileStr);
	if (!qmlFileInfo.exists())
	{
		Rcout << "File not found: " << fq(qmlFileStr) << std::endl;
		return nullptr;
	}

	if (gl_qmlFormMap.contains(qmlFileStr) && gl_qmlFormMap[qmlFileStr].first == qmlFileInfo.lastModified())
		qmlForm = gl_qmlFormMap[qmlFileStr].second;
	else
	{
		QUrl urlFile = QUrl::fromLocalFile(qmlFileInfo.absoluteFilePath());
		QQmlComponent	qmlComp( gl_qmlEngine, urlFile, QQmlComponent::PreferSynchronous);

		qmlForm = qobject_cast<AnalysisForm*>(qmlComp.create());

		if (qmlComp.errors().length() > 0)
		{
			for(const auto & error : qmlComp.errors())
				Rcout << "Error when creating component at " << fq(QString::number(error.line())) << "," << fq(QString::number(error.column())) << ": " << fq(error.description()) << std::endl;
		}

		if (!qmlForm)
		{
			Rcout << "QML Form could not be created" << std::endl;
			return nullptr;
		}

		AnalysisBase* analysis = new AnalysisBase(qmlForm); // Make dummy analysis
		QObject::connect(analysis,	&AnalysisBase::sendRScriptSignal,	[qmlForm](QString script, QString controlName, bool whiteListedVersion, QString module) { sendRScriptHandler(qmlForm, script, controlName, whiteListedVersion); });
		qmlForm->setAnalysis(analysis);

		if (gl_qmlFormMap.contains(qmlFileStr))
			deleteQuickItem(gl_qmlFormMap[qmlFileStr].second); // delete old version of the form
		gl_qmlFormMap[qmlFileStr] = std::make_pair(qmlFileInfo.lastModified(), qmlForm);

		gl_application->processEvents();

	}

	return qmlForm;
}

// [[Rcpp::export]]
String loadQmlAndParseOptions(String moduleName, String analysisName, String qmlFile, String options, String version, bool preloadData)
{
	if (!init())
	{
		Rcout << "Error during initialization" << std::endl;
		return "";
	}

	std::string qmlFileStr		= qmlFile.get_cstring(),
				optionsStr		= options.get_cstring(),
				versionStr		= version.get_cstring(),
				analysisNameStr	= analysisName.get_cstring(),
				moduleNameStr	= moduleName.get_cstring();


	AnalysisForm* form = getQmlForm(tq(qmlFileStr));

	if (!form)
	{
		Rcout << "Cannot create QML Form " << qmlFileStr << std::endl;
		return "";
	}

	QString returnedValue = form->parseOptions(tq(optionsStr));

	Json::Value		jsonResult;
	Json::Reader	jsonReader;
	jsonReader.parse(fq(returnedValue), jsonResult);
	Json::Value		jsonOptions = jsonResult["options"];

	gl_extraEncodings->setCurrentNamesFromOptionsMeta(jsonOptions);
	gl_dataBridge->updateOptionsAccordingToMeta(jsonOptions);
	ColumnEncoder::encodeColumnNamesinOptions(jsonOptions, preloadData);

	return jsonOptions.toStyledString();
}

bool _generateWrapper(const QString& modulePath, const QString& analysisName, const QString& qmlFileName, bool preloadData)
{
	QString qmlFilePath = modulePath + "/inst/qml/" + qmlFileName;

	AnalysisForm* form = getQmlForm(qmlFilePath);
	if (!form)
	{
		Rcout << "Cannot create the QML form " << qmlFilePath << std::endl;
		return false;
	}

	QString returnedValue = form->generateWrapper(QDir(modulePath).dirName(), analysisName, qmlFileName, preloadData);

	QFile file(modulePath + "/R/" + analysisName + "Wrapper.R");
	if (file.open(QIODevice::ReadWrite)) {
		QTextStream stream(&file);
		stream << returnedValue;
	}

	return true;
}

// [[Rcpp::export]]
String generateModuleWrappers(String modulePath, bool preloadData)
{
	if (!init())
		return "Error during initialization";

	QString modulePathQ		= tq(modulePath.get_cstring()),
			moduleNameQ;

	QDir moduleDir(modulePathQ);

	if (!moduleDir.exists())
		return "Module path not found: " + fq(modulePathQ);

	QVector<std::pair<QString, QString> > analyses;
	QFile qmlDescriptionFile(modulePathQ + "/inst/Description.qml");
	if (!qmlDescriptionFile.exists())
		return "Description.qml file not found in " + fq(modulePathQ);

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

	for (auto analysis : analyses)
	{
		Rcout << "Analysis " << fq(analysis.first) << " with qml file " << fq(analysis.second) << std::endl;
		if (!_generateWrapper(modulePathQ, analysis.first, analysis.second, preloadData))
			return "Error when generating wrapper of " + fq(analysis.first);
	}

	return "Wrappers generated";
}


// [[Rcpp::export]]
String generateAnalysisWrapper(String modulePath, String qmlFileName, String analysisName, bool preloadData)
{
	if (!init())
		return "Error during initialization";

	gl_qmlEngine->clearComponentCache();

	QString qmlFileNameQ	= tq(qmlFileName.get_cstring()),
			modulePathQ		= tq(modulePath.get_cstring()),
			moduleNameQ,
			analysisNameQ	= tq(analysisName.get_cstring());

	QDir moduleDir(modulePathQ);

	if (!moduleDir.exists())
		return "Module path not found: " + fq(modulePathQ);

	// If JASP.Module is set as a a Qt module/plugin (as JASP.Controls), then we could load it and uses the Description object directly, and know directly
	// what is the name of the qml file and if it uses preloadData
	if (!_generateWrapper(modulePathQ, analysisNameQ, qmlFileNameQ, preloadData))
		return "Error when generating wrapper of " + fq(analysisNameQ);

	return "Wrapper generated for analysis " + fq(analysisNameQ);
}







