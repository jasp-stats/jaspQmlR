#include "RDataSetReader.h"
#include <QQmlEngine>
#include <Rcpp.h>
using namespace Rcpp;

RDataSetReader* RDataSetReader::_singleton = nullptr;

RDataSetReader::RDataSetReader(QObject *parent)
{
	_engine = qobject_cast<QQmlEngine*>(parent);
	new VariableInfo(this);
	_singleton = this;
}

//Rcpp::Environment RDataSetReader::_jaspBaseEnvironment = nullptr;

/*Rcpp::Environment RDataSetReader::_getJaspBase()
{
	if (!_jaspBaseEnvironment)
		_jaspBaseEnvironment = Environment::namespace_env("jaspBase");

	return _jaspBaseEnvironment;
} */

int RDataSetReader::_getColumnCount() const
{
	int result = 0;

	Environment pkg = Environment::namespace_env("jaspBase"); //_getJaspBase();
	if (pkg)
	{
		Function f = pkg["getColumnCount"];
		SEXP ret = f();
		result = Rf_asInteger(ret);
	}

	return result;
}

columnType RDataSetReader::_getColumnType(const QString& colName) const
{
	columnType result = columnType::unknown;

	if (colName.isEmpty())
		return result;

	Environment pkg = Environment::namespace_env("jaspBase"); //_getJaspBase();
	if (pkg)
	{
		Function f = pkg["getColumnType"];

		Rcpp::String colNameRcpp = colName.toStdString();
		SEXP ret = f(colNameRcpp);
		std::string retstr = Rcpp::as<Rcpp::String>(ret);

		result = columnTypeFromString(retstr);
	}

	return result;

}

QStringList RDataSetReader::_getColumnNames() const
{
	QStringList result;

	Environment pkg = Environment::namespace_env("jaspBase"); //_getJaspBase();
	if (pkg)
	{
		Function f = pkg["getColumnNames"];

		SEXP ret = f();
	}

	return result;
}


QStringList RDataSetReader::_getColumnLabels(const QString& colName) const
{
	QStringList result;

	if (colName.isEmpty())
		return result;

	Environment pkg = Environment::namespace_env("jaspBase"); //_getJaspBase();
	if (pkg)
	{
		Function f = pkg["getColumnLabels"];

		Rcpp::String colNameRcpp = colName.toStdString();
		SEXP ret = f(colNameRcpp);

		Rcpp::CharacterVector retstr = Rcpp::as<Rcpp::CharacterVector>(ret);
		for (auto s : retstr)
			result.push_back(QString::fromStdString(Rcpp::as<Rcpp::String>(s)));
	}

	return result;
}

QList<QVariant> RDataSetReader::_getColumnValues(const QString& colName) const
{
	QList<QVariant> result;

	if (colName.isEmpty())
		return result;

	Environment pkg = Environment::namespace_env("jaspBase"); //_getJaspBase();
	if (pkg)
	{
		Function f = pkg["getColumnValues"];

		Rcpp::String colNameRcpp = colName.toStdString();
		SEXP ret = f(colNameRcpp);

	}

	return result;
}


QVariant RDataSetReader::provideInfo(VariableInfo::InfoType info, const QString& colName, int row) const
{
	try
	{
		columnType	colType	= _getColumnType(colName);

		switch(info)
		{
		case VariableInfo::VariableType:				return	int(colType);
		case VariableInfo::VariableTypeName:			return	"";
		case VariableInfo::VariableTypeIcon:			return	VariableInfo::getIconFile(colType, VariableInfo::DefaultIconType);
		case VariableInfo::VariableTypeDisabledIcon:	return	VariableInfo::getIconFile(colType, VariableInfo::DisabledIconType);
		case VariableInfo::VariableTypeInactiveIcon:	return	VariableInfo::getIconFile(colType, VariableInfo::InactiveIconType);
		case VariableInfo::Labels:						return	_getColumnLabels(colName);
		case VariableInfo::DoubleValues:				return	_getColumnValues(colName);
		case VariableInfo::NameRole:					return	Qt::DisplayRole;
		case VariableInfo::DataSetRowCount:				return  _getColumnCount();
		case VariableInfo::DataSetValue:				return	"";
		case VariableInfo::MaxWidth:					return	100;
		case VariableInfo::SignalsBlocked:				return	false;
		case VariableInfo::VariableNames:				return	_getColumnNames();
		case VariableInfo::DataAvailable:				return	true;

		default: break;
		}
	}
	catch(std::exception & e)
	{
		throw e;
	}
	return QVariant("");
}

QQmlContext *RDataSetReader::providerQMLContext() const
{
	return _engine->rootContext();
}