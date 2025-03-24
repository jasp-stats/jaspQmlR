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

#ifndef DATAFRAMEIMPORTER_H
#define DATAFRAMEIMPORTER_H

#include <Rcpp.h>
#include "rjasp_interface.h"




class DataFrameImporter
{

public:
	static const RJASP_DataSet& loadDataFrame(Rcpp::List dataframe);
	static Rcpp::List getVariableNames();
	static Rcpp::List getVariableValues(Rcpp::String variableName);

private:
	static RJASP_DataSet datasetStatic;
	static void freeDataSet();

	template<int RTYPE>
	static std::vector<std::string> readCharacterVector(Rcpp::Vector<RTYPE>	obj);
};


#endif //DATAFRAMEIMPORTER_H
