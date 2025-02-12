#include "DataSetProvider.h"
#include "qutils.h"
#include <QQmlEngine>

DataSetProvider		*	DataSetProvider::_singleton		= nullptr;

DataSetProvider* DataSetProvider::getProvider(bool inMemory, bool reset)
{
	if (!_singleton)
		_singleton = new DataSetProvider(inMemory);
	else if (reset)
		_singleton->resetDataSet();

	return _singleton;
}

DataSetProvider::DataSetProvider(bool inMemory, QObject *parent) : QAbstractTableModel(parent)
{
	_db	= new DatabaseInterface(true, inMemory);
	_dataset = new DataSet();

	new VariableInfo(this);
	_singleton = this;
}

void DataSetProvider::resetDataSet()
{
	if (_dataset)
	{
		_dataset->dbDelete();
		delete _dataset;
	}

	_dataset = new DataSet();
}

int	DataSetProvider::rowCount(const QModelIndex &) const
{
	return _dataset->columnCount();
}

int	DataSetProvider::columnCount(const QModelIndex &) const
{
	return _dataset->rowCount();
}

QVariant DataSetProvider::data(const QModelIndex & index, int role) const
{
	Column * column = index.row() > columnCount() ? nullptr : _dataset->column(index.row());

	if (!column)						return QVariant();
	else if (role == Qt::DisplayRole)	return tq(column->name());
	else								return QVariant(); //QAbstractTableModel::data(index, role);
}



QVariantList DataSetProvider::_getDoubleList(Column * column) const
{
	QVariantList list;

	if (!column)
		return list;

	for (double value : column->dbls())
		list.append(value);

	return list;

}

QVariantList DataSetProvider::_getStringList(Column * column) const
{
	QVariantList list;

	if (!column)
		return list;

	int rows = _dataset->rowCount();
	for (int r = 0; r < rows; r++)
		list.append(tq(column->getDisplay(r)));

	return list;
}

QStringList DataSetProvider::_getColumnNames() const
{
	QStringList result;

	int cols = _dataset->columnCount();
	for (int i = 0; i < cols; i++)
		result.append(tq(_dataset->column(i)->name()));
	return result;
}


QVariant DataSetProvider::provideInfo(VariableInfo::InfoType info, const QString& colName, int row) const
{
	try
	{
		Column * column = _dataset->column(fq(colName));

		switch(info)
		{
		case VariableInfo::VariableType:				return	int(!column ? columnType::unknown : column->type());
		case VariableInfo::DoubleValues:				return	_getDoubleList(column);
		case VariableInfo::TotalNumericValues:			return	!column ? 0 : column->nonFilteredNumericsCount();
		case VariableInfo::TotalLevels:					return	!column ? 0 : (int)column->nonFilteredLevels().size();
		case VariableInfo::Labels:						return	!column ? QStringList() : tq(column->nonFilteredLevels());
		case VariableInfo::NameRole:					return	Qt::DisplayRole;
		case VariableInfo::DataSetRowCount:				return  _dataset->rowCount();
		case VariableInfo::DataSetValue:				return	!column ? "" : tq(column->getValue(row));
		case VariableInfo::DataSetValues:				return	_getStringList(column);
		case VariableInfo::MaxWidth:					return	100;
		case VariableInfo::SignalsBlocked:				return	false;
		case VariableInfo::VariableNames:				return	_getColumnNames();
		case VariableInfo::DataAvailable:				return	_dataset->columnCount() > 0;
		case VariableInfo::PreviewScale:				return	"";
		case VariableInfo::PreviewOrdinal:				return	"";
		case VariableInfo::PreviewNominal:				return	"";
		case VariableInfo::DataSetPointer:				return	QVariant::fromValue<void*>(_dataset);


		default: break;
		}
	}
	catch(std::exception & e)
	{
		throw e;
	}
	return QVariant("");
}

bool DataSetProvider::absorbInfo(VariableInfo::InfoType info, const QString &colName, int row, QVariant value)
{
	try
	{
		Column * column = _dataset->column(fq(colName));
		if (!column)
			return false;

		switch(info)
		{
		default:								return false;
		case VariableInfo::DataSetValue:		return column->setStringValue(row, fq(value.toString()));
		case VariableInfo::DataSetValues:
		{
			int r=0;
			for(const QVariant & val : value.toList())
				if (row + r < _dataset->rowCount())
					column->setStringValue(row + r++, fq(val.toString()));
			return true;
		}
		}
	}
	catch(std::exception & e)
	{
		throw e;
	}

	return false;
}

