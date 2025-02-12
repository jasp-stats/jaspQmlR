#ifndef DATASETPROVIDER_H
#define DATASETPROVIDER_H

#include <QAbstractTableModel>
#include "variableinfo.h"
#include "dataset.h"
#include "databaseinterface.h"


class DataSetProvider : public QAbstractTableModel, public VariableInfoProvider
{

public:
	static DataSetProvider	*	getProvider(bool inMemory, bool reset = true);

	DataSet					*	dataSet()	{ return _dataset; }
	void						resetDataSet();

	int							rowCount(	const QModelIndex & parent = QModelIndex())									const	override;
	int							columnCount(const QModelIndex & parent = QModelIndex())									const	override;
	QVariant					data(		const QModelIndex & index, int role = Qt::DisplayRole)						const	override;


	QVariant					provideInfo(VariableInfo::InfoType info, const QString& colName = "", int row = 0)		const	override;
	bool						absorbInfo(	VariableInfo::InfoType info, const QString& name, int row, QVariant value)			override;
	QAbstractItemModel		*	providerModel()																					override	{ return this;	}

private:
	explicit DataSetProvider(bool inMemory = true, QObject* parent = nullptr);

	static DataSetProvider	*	_singleton;

	QVariantList				_getDoubleList(Column * column) const;
	QVariantList				_getStringList(Column * column)	const;
	QStringList					_getColumnNames()				const;

	DatabaseInterface		*	_db					= nullptr;
	DataSet					*	_dataset			= nullptr;

};


#endif //DATASETPROVIDER_H
