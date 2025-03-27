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

#include "dataframeimporter.h"

SyntaxBridgeDataSet DataFrameImporter::datasetStatic;

std::string doubleToString(double dbl)
{
	if (dbl > std::numeric_limits<double>::max())		return "∞";
	if (dbl < std::numeric_limits<double>::lowest())	return "-∞";

	std::stringstream conv; //Use this instead of std::to_string to make sure there are no trailing zeroes (and to get full precision)
	conv << dbl;
	return conv.str();
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
			   doubleToString((double)(obj[row]));
}

template<int RTYPE>
std::vector<std::string> DataFrameImporter::readCharacterVector(Rcpp::Vector<RTYPE>	obj)
{
	std::vector<std::string> vecresult;
	for(int row=0; row<obj.size(); row++)
		vecresult.push_back(RVectorEntry_to_String(obj, row));

	return vecresult;
}

void DataFrameImporter::freeDataSet()
{
	for (int colNr = 0; colNr < datasetStatic.columnCount; colNr++)
	{
		SyntaxBridgeColumn& column = datasetStatic.columns[colNr];
		free(column.name);
		for (int rowNr = 0; rowNr < datasetStatic.rowCount; rowNr++)
			free(column.values[rowNr]);
		free(column.values);
	}

	free(datasetStatic.name);
	free(datasetStatic.columns);

	datasetStatic.name = nullptr;
	datasetStatic.columnCount = 0;
	datasetStatic.rowCount = 0;
	datasetStatic.columns = nullptr;
}

const SyntaxBridgeDataSet& DataFrameImporter::loadDataFrame(Rcpp::List dataframe)
{
	freeDataSet();

	Rcpp::RObject namesListRObject = dataframe.names();
	Rcpp::CharacterVector namesList;

	if (!namesListRObject.isNULL())
		namesList = namesListRObject;

	datasetStatic.columnCount = dataframe.size();
	datasetStatic.columns = static_cast<SyntaxBridgeColumn*>(calloc(dataframe.size(), sizeof(SyntaxBridgeColumn)));

	int maxRows = 0;
	std::vector<std::vector<std::string>> allColumns;

	for (int colNr = 0; colNr < dataframe.size(); colNr++)
	{
		SyntaxBridgeColumn& column	= datasetStatic.columns[colNr];

		std::string colName(namesList[colNr]);
		if(colName == "")
			colName = "column_" + std::to_string(colNr);

		column.name = strdup(colName.c_str());

		std::vector<std::string> colValues;

		Rcpp::RObject colObj = (Rcpp::RObject)dataframe[colNr];

		if(Rcpp::is<Rcpp::NumericVector>(colObj))			colValues = readCharacterVector<REALSXP>((Rcpp::NumericVector)colObj);
		else if(Rcpp::is<Rcpp::IntegerVector>(colObj))		colValues = readCharacterVector<INTSXP>((Rcpp::IntegerVector)colObj);
		else if(Rcpp::is<Rcpp::LogicalVector>(colObj))		colValues = readCharacterVector<LGLSXP>((Rcpp::LogicalVector)colObj);
		else if(Rcpp::is<Rcpp::CharacterVector>(colObj))	colValues = readCharacterVector<STRSXP>((Rcpp::CharacterVector)colObj);
		else if(Rcpp::is<Rcpp::StringVector>(colObj))		colValues = readCharacterVector<STRSXP>((Rcpp::StringVector)colObj);
		else
		{
			Rcpp::Rcout << "Unknown type of variable " << colName << "!" << std::endl;
			colValues = std::vector<std::string>(maxRows);
		}

		if (colValues.size() > maxRows)
			maxRows = colValues.size();

		allColumns.push_back(colValues);
	}

	for (int colNr = 0; colNr < dataframe.size(); colNr++)
	{
		SyntaxBridgeColumn& column	= datasetStatic.columns[colNr];

		column.values = (char**)calloc(maxRows, sizeof(char*));
		int i = 0;
		for (const std::string &value: allColumns[colNr])
			column.values[i++] = strdup(value.c_str());
		for (; i < maxRows; i++)
			column.values[i] = strdup("");
	}

	datasetStatic.rowCount = maxRows;

	return datasetStatic;
}

Rcpp::List DataFrameImporter::getVariableNames()
{
	Rcpp::List result;
	for (int colNr = 0; colNr < datasetStatic.columnCount; colNr++)
	{
		SyntaxBridgeColumn& column = datasetStatic.columns[colNr];
		result.push_back((const char* )column.name);
	}

	return result;
}

Rcpp::List DataFrameImporter::getVariableValues(Rcpp::String variableName)
{
	Rcpp::List result;
	std::string name = variableName.get_cstring();
	for (int colNr = 0; colNr < datasetStatic.columnCount; colNr++)
	{
		SyntaxBridgeColumn& column = datasetStatic.columns[colNr];
		if (name == column.name)
		{
			for (int rowNr = 0; rowNr < datasetStatic.rowCount; rowNr++)
				result.push_back((const char*)column.values[rowNr]);
			return result;
		}
	}

	return result;

}
