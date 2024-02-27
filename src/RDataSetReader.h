#ifndef RDATASETREADER_H
#define RDATASETREADER_H

#include "variableinfo.h"
#include <Rcpp.h>

class RDataSetReader : public VariableInfoProvider
{

public:
	explicit RDataSetReader(QObject* parent = nullptr);

	static RDataSetReader*		singleton()	{ return _singleton; }


	QVariant					provideInfo(VariableInfo::InfoType info, const QString& colName = "", int row = 0)		const	override;
	QAbstractItemModel*			providerModel()																					override		{ return nullptr;	}
	QQmlContext*				providerQMLContext()																	const	override;

private:
	static RDataSetReader*		_singleton;

	int							_getColumnCount()							const;
	columnType					_getColumnType(const QString& colName)		const;
	QStringList					_getColumnNames()							const;
	QStringList					_getColumnLabels(const QString& colName)	const;
	QList<QVariant>				_getColumnValues(const QString& colName)	const;
	//static Rcpp::Environment	_getJaspBase();

	QQmlEngine*					_engine					= nullptr;
	//static Rcpp::Environment	_jaspBaseEnvironment;
};


#endif //RDATASETREADER_H
