/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | foam-extend: Open Source CFD
   \\    /   O peration     | Version:     4.0
    \\  /    A nd           | Web:         http://www.foam-extend.org
     \\/     M anipulation  | For copyright notice see file Copyright
-------------------------------------------------------------------------------
License
	This file is part of foam-extend.

	foam-extend is free software: you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the
	Free Software Foundation, either version 3 of the License, or (at your
	option) any later version.

	foam-extend is distributed in the hope that it will be useful, but
	WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with foam-extend.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "solutionControl.H"
#include "lduMatrix.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
	defineTypeNameAndDebug(solutionControl, 0);
}


// * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * * //

void Foam::solutionControl::read(const bool absTolOnly)
{
	const dictionary& solutionDict = this->dict();

	// Read solution controls
	nNonOrthCorr_ =
		solutionDict.lookupOrDefault<label>("nNonOrthogonalCorrectors", 0);
	momentumPredictor_ =
		solutionDict.lookupOrDefault("momentumPredictor", true);
	transonic_ = solutionDict.lookupOrDefault("transonic", false);
	consistent_ = solutionDict.lookupOrDefault("consistent", false);

	// Read residual information
	const dictionary residualDict
	(
		solutionDict.subOrEmptyDict("residualControl")
	);

	DynamicList<fieldData> data(residualControl_);

	forAllConstIter(dictionary, residualDict, iter)
	{
		const word& fName = iter().keyword();
		const label fieldI = applyToField(fName, false);
		if (fieldI == -1)
		{
			fieldData fd;
			fd.name = fName.c_str();

			if (absTolOnly)
			{
				fd.absTol = readScalar(residualDict.lookup(fName));
				fd.relTol = -1;
				fd.initialResidual = -1;
			}
			else
			{
				if (iter().isDict())
				{
					const dictionary& fieldDict(iter().dict());
					fd.absTol = readScalar(fieldDict.lookup("tolerance"));
					fd.relTol = readScalar(fieldDict.lookup("relTol"));
					fd.initialResidual = 0.0;
				}
				else
				{
					FatalErrorIn
					(
						"void Foam::solutionControl::read"
						"(const bool absTolOnly)"
					)   << "Residual data for " << iter().keyword()
						<< " must be specified as a dictionary"
						<< exit(FatalError);
				}
			}

			data.append(fd);
		}
		else
		{
			fieldData& fd = data[fieldI];
			if (absTolOnly)
			{
				fd.absTol = readScalar(residualDict.lookup(fName));
			}
			else
			{
				if (iter().isDict())
				{
					const dictionary& fieldDict(iter().dict());
					fd.absTol = readScalar(fieldDict.lookup("tolerance"));
					fd.relTol = readScalar(fieldDict.lookup("relTol"));
				}
				else
				{
					FatalErrorIn
					(
						"void Foam::solutionControl::read"
						"(const bool absTolOnly)"
					)   << "Residual data for " << iter().keyword()
						<< " must be specified as a dictionary"
						<< exit(FatalError);
				}
			}
		}
	}

	residualControl_.transfer(data);

	if (debug)
	{
		forAll(residualControl_, i)
		{
			const fieldData& fd = residualControl_[i];
			Info<< "residualControl[" << i << "]:" << nl
				<< "    name	 : " << fd.name << nl
				<< "    absTol   : " << fd.absTol << nl
				<< "    relTol   : " << fd.relTol << nl
				<< "    iniResid : " << fd.initialResidual << endl;
		}
	}
}


void Foam::solutionControl::read()
{
	read(false);
}


Foam::label Foam::solutionControl::applyToField
(
	const word& fieldName,
	const bool useRegEx
) const
{
	forAll(residualControl_, i)
	{
		if (useRegEx && residualControl_[i].name.match(fieldName))
		{
			return i;
		}
		else if (residualControl_[i].name == fieldName)
		{
			return i;
		}
	}

	return -1;
}


void Foam::solutionControl::storePrevIterFields() const
{
//	storePrevIter<label>();
	storePrevIter<scalar>();
	storePrevIter<vector>();
	storePrevIter<sphericalTensor>();
	storePrevIter<symmTensor>();
	storePrevIter<tensor>();
}


template<class Type>
void Foam::solutionControl::maxTypeResidual
(
	const word& fieldName,
	ITstream& data,
	scalar& firstRes,
	scalar& lastRes
) const
{
	typedef GeometricField<Type, fvPatchField, volMesh> fieldType;

	if (mesh_.foundObject<fieldType>(fieldName))
	{
		const List<lduSolverPerformance> sp(data);
		firstRes = cmptMax(sp.first().initialResidual());
		lastRes = cmptMax(sp.last().initialResidual());
	}
}


Foam::scalar Foam::solutionControl::maxResidual
(
	const word& fieldName,
	ITstream& data,
	scalar& lastRes
) const
{
	scalar firstRes = 0;

	maxTypeResidual<scalar>(fieldName, data, firstRes, lastRes);
	maxTypeResidual<vector>(fieldName, data, firstRes, lastRes);
	maxTypeResidual<sphericalTensor>(fieldName, data, firstRes, lastRes);
	maxTypeResidual<symmTensor>(fieldName, data, firstRes, lastRes);
	maxTypeResidual<tensor>(fieldName, data, firstRes, lastRes);

	return firstRes;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::solutionControl::solutionControl(fvMesh& mesh, const word& algorithmName)
:
	IOobject
	(
		"solutionControl",
		mesh.time().timeName(),
		mesh
	),
	mesh_(mesh),
	residualControl_(),
	algorithmName_(algorithmName),
	nNonOrthCorr_(0),
	momentumPredictor_(true),
	transonic_(false),
	consistent_(false),
	corr_(0),
	corrNonOrtho_(0)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::solutionControl::~solutionControl()
{}


// ************************************************************************* //
