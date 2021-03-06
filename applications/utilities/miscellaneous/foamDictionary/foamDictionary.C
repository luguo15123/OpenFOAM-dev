/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2016 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    foamDictionary

Description
    Interrogates and manipulates dictionaries.

Usage
    - foamDictionary [OPTION] dictionary

    \param -entry \<name\> \n
    Selects an entry

    \param -keywords \<name\> \n
    Prints the keywords (of the selected entry or of the top level if
    no entry was selected

    \param -add \<value\> \n
    Adds the entry (should not exist yet)

    \param -set \<value\> \n
    Adds or replaces the entry

    Typical usage:
    - change simulation to run for one timestep only:
    foamDictionary system/controlDict -entry stopAt -set writeNow;

    - change solver
    foamDictionary system/fvSolution -entry solvers.p.solver -set PCG

    - print bc type
    foamDictionary 0/U -entry boundaryField.movingWall.type

    - change bc parameter
    foamDictionary 0/U -entry boundaryField.movingWall.value \
        -set "uniform (2 0 0)"

    - change whole bc type
    foamDictionary 0/U -entry boundaryField.movingWall \
        -set "{type uniformFixedValue; uniformValue (2 0 0);}"

\*---------------------------------------------------------------------------*/

#include "argList.H"
#include "Time.H"
#include "IFstream.H"
#include "OFstream.H"

using namespace Foam;

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

//- Converts old scope syntax to new syntax
word scope(const fileName& entryName)
{
    if (entryName.find(':') != string::npos)
    {
        wordList entryNames(entryName.components(':'));

        word entry(entryNames[0]);
        for (label i = 1; i < entryNames.size(); i++)
        {
            entry += word('.') + entryNames[i];
        }
        return entry;
    }
    else
    {
        return entryName;
    }
}


//- Extract keyword (last bit of scoped name
word keyword(const word& scopedName)
{
    word key(scopedName);
    string::size_type i = scopedName.find_last_of(".");
    if (i != string::npos)
    {
        key = scopedName.substr(i+1, string::npos);
    }
    return key;
}


void setScoped
(
    dictionary& dict,
    const word& keyword,
    const bool overwrite,
    entry* d
)
{
    if (keyword[0] == ':')
    {
        // Go up to top level and recurse to find entries
        setScoped
        (
            const_cast<dictionary&>(dict.topDict()),
            keyword.substr(1, keyword.size()-1),
            overwrite,
            d
        );
        return;
    }
    else
    {
        string::size_type dotPos = keyword.find('.');

        if (dotPos == string::npos)
        {
            // Non-scoped lookup
            if (overwrite)
            {
                dict.set(d);
            }
            else
            {
                dict.add(d, false);
            }
            return;
        }
        else
        {
            if (dotPos == 0)
            {
                // Starting with a '.'. Go up for every 2nd '.' found

                const dictionary* dictPtr = &dict;

                string::size_type begVar = dotPos + 1;
                string::const_iterator iter =
                    keyword.begin() + begVar;
                string::size_type endVar = begVar;
                while
                (
                    iter != keyword.end()
                 && *iter == '.'
                )
                {
                    ++iter;
                    ++endVar;

                    // Go to parent
                    if (&dictPtr->parent() == &dictionary::null)
                    {
                        FatalIOErrorInFunction(dict)
                            << "No parent of current dictionary"
                            << " when searching for "
                            <<  keyword.substr
                                (
                                    begVar,
                                    keyword.size() - begVar
                                )
                            << exit(FatalIOError);
                    }
                    dictPtr = &dictPtr->parent();
                }

                setScoped
                (
                    const_cast<dictionary&>(*dictPtr),
                    keyword.substr(endVar),
                    overwrite,
                    d
                );
                return;
            }
            else
            {
                // Extract the first word
                word firstWord = keyword.substr(0, dotPos);

                const entry* entPtr = dict.lookupScopedEntryPtr
                (
                    firstWord,
                    false,          // Recursive
                    false
                );

                if (!entPtr || !entPtr->isDict())
                {
                    FatalIOErrorInFunction(dict)
                        << "keyword " << firstWord
                        << " is undefined in dictionary "
                        << dict.name() << " or is not a dictionary"
                        << endl
                        << "Valid keywords are " << dict.keys()
                        << exit(FatalIOError);
                }

                const dictionary& firstDict = entPtr->dict();

                setScoped
                (
                    const_cast<dictionary&>(firstDict),
                    keyword.substr(dotPos, keyword.size()-dotPos),
                    overwrite,
                    d
                );
                return;
            }
        }
    }
}


int main(int argc, char *argv[])
{
    argList::addNote("manipulates dictionaries");

    argList::noBanner();
    argList::validArgs.append("dictionary");
    argList::addBoolOption("keywords", "report keywords");
    argList::addOption("entry", "name", "report/select the named entry");
    argList::addOption
    (
        "set",
        "value",
        "changes existing entry or adds new entry"
    );
    argList::addOption
    (
        "add",
        "value",
        "adds a new entry"
    );

    #include "setRootCase.H"

    fileName dictFileName(args.rootPath()/args.caseName()/args[1]);

    autoPtr<IFstream> dictFile(new IFstream(dictFileName));

    if (dictFile().good())
    {
        bool changed = false;

        // Read but preserve headers
        dictionary dict;
        dict.read(dictFile(), true);

        word entryName;
        if (args.optionReadIfPresent("entry", entryName))
        {
            word scopedName(scope(entryName));

            string newValue;
            if
            (
                args.optionReadIfPresent("set", newValue)
             || args.optionReadIfPresent("add", newValue)
            )
            {
                bool overwrite = args.optionFound("set");

                word key(keyword(scopedName));

                IStringStream str(string(key) + ' ' + newValue + ';');
                setScoped(dict, scopedName, overwrite, entry::New(str).ptr());
                changed = true;

                // Print the changed entry
                const entry* entPtr = dict.lookupScopedEntryPtr
                (
                    scopedName,
                    false,
                    true            // Support wildcards
                );
                if (entPtr)
                {
                    Info<< *entPtr << endl;
                }
            }
            else
            {
                const entry* entPtr = dict.lookupScopedEntryPtr
                (
                    scopedName,
                    false,
                    true            // Support wildcards
                );

                if (entPtr)
                {
                    if (args.optionFound("keywords"))
                    {
                        const dictionary& dict = entPtr->dict();
                        forAllConstIter(dictionary, dict, iter)
                        {
                            Info<< iter().keyword() << endl;
                        }
                    }
                    else
                    {
                        Info<< *entPtr << endl;
                    }
                }
                else
                {
                    FatalIOErrorInFunction(dictFile)
                        << "Cannot find entry " << entryName
                        << exit(FatalError, 2);
                }
            }
        }
        else if (args.optionFound("keywords"))
        {
            forAllConstIter(dictionary, dict, iter)
            {
                Info<< iter().keyword() << endl;
            }
        }
        else
        {
            Info<< dict;
        }

        if (changed)
        {
            dictFile.clear();
            OFstream os(dictFileName);
            IOobject::writeBanner(os);
            dict.write(os, false);
            IOobject::writeEndDivider(os);
        }
    }
    else
    {
        FatalErrorInFunction
            << "Cannot open file " << dictFileName
            << exit(FatalError, 1);
    }

    return 0;
}


// ************************************************************************* //
