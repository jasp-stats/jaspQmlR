//
// Copyright (C) 2013-2025 University of Amsterdam
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

#include "DataFrameImporter.h"
#include "columnutils.h"

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
std::vector<std::string> DataFrameImporter::readCharacterVector(Rcpp::Vector<RTYPE>	obj)
{
	std::vector<std::string> vecresult;
	for(int row=0; row<obj.size(); row++)
		vecresult.push_back(RVectorEntry_to_String(obj, row));

	return vecresult;
}

void DataFrameImporter::loadDataSet(DataSet* dataset, Rcpp::List dataframe, int threshold, bool orderLabelsByValue)
{

	Rcpp::RObject namesListRObject = dataframe.names();
	Rcpp::CharacterVector namesList;

	if (!namesListRObject.isNULL())
		namesList = namesListRObject;

	dataset->setColumnCount(dataframe.size());

	int maxRows = 0;

	for (int colNr = 0; colNr < dataframe.size(); colNr++)
	{
		std::string name(namesList[colNr]);
		std::vector<std::string> column;

		if(name == "")
			name = "column_" + std::to_string(colNr);

		Rcpp::RObject colObj = (Rcpp::RObject)dataframe[colNr];

		// TODO: This could be made more efficient: if we have integers or doubles, initialize the column in the dataset directly with these integers and doubles
		// Now we transform them into strings, and inside initColumnWithStrings re-transform them into integers or doubles.
		if(Rcpp::is<Rcpp::NumericVector>(colObj))			column = readCharacterVector<REALSXP>((Rcpp::NumericVector)colObj);
		else if(Rcpp::is<Rcpp::IntegerVector>(colObj))		column = readCharacterVector<INTSXP>((Rcpp::IntegerVector)colObj);
		else if(Rcpp::is<Rcpp::LogicalVector>(colObj))		column = readCharacterVector<LGLSXP>((Rcpp::LogicalVector)colObj);
		else if(Rcpp::is<Rcpp::CharacterVector>(colObj))	column = readCharacterVector<STRSXP>((Rcpp::CharacterVector)colObj);
		else if(Rcpp::is<Rcpp::StringVector>(colObj))		column = readCharacterVector<STRSXP>((Rcpp::StringVector)colObj);
		else
		{
			Rcout << "Unknown type of variable " << name << "!" << std::endl;
			column = std::vector<std::string>(maxRows);
		}

		if (column.size() > maxRows)
			maxRows = column.size();
		if (dataset->rowCount() < maxRows)
			dataset->setRowCount(maxRows);

		dataset->initColumnWithStrings(colNr, name, column, {}, name, columnType::unknown, {}, threshold, orderLabelsByValue);
	}

}
