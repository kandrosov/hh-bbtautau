/*! Code to merge two root files taking first file as a reference and adding missing info from the second.
Some parts of the code are taken from copyFile.C written by Rene Brun.
This file is part of https://github.com/hh-italian-group/hh-bbtautau. */

#pragma once

#include <iostream>

#include <TROOT.h>
#include <TKey.h>
#include <TSystem.h>
#include <TTree.h>
#include <memory>
#include "AnalysisTools/Core/include/RootExt.h"

class MergeRootFiles {
public:
    MergeRootFiles(const std::string& originalFileName , const std::string& referenceFileName,
                  const std::string& outputFileName)
        : originalFile(root_ext::OpenRootFile(originalFileName)),
          referenceFile(root_ext::OpenRootFile(referenceFileName)),
          outputFile(root_ext::CreateRootFile(outputFileName)) {}

    void Run()
    {
        std::cout << "Copying original file..." << std::endl;
        CopyDirectory(originalFile.get(), outputFile.get(), false);
        std::cout << "Copying reference file..." << std::endl;
        CopyDirectory(referenceFile.get(), outputFile.get(), true);
        std::cout << "Original and reference files has been merged." << std::endl;
    }

private:
    /// Copy all objects and subdirs of the source directory to the destination directory.
    static void CopyDirectory(TDirectory *source, TDirectory *destination, bool isReference)
    {
        std::cout<< "CopyDir. Current direcotry: " << destination->GetName() << std::endl;

        TIter nextkey(source->GetListOfKeys());
        for(TKey* key; (key = (TKey*)nextkey());) {
            //std::cout << "Processing key: " << key->GetName() << std::endl;
            const char *classname = key->GetClassName();
            TClass *cl = gROOT->GetClass(classname);
            if (!cl) continue;
            bool objectWritten = false;
            if (cl->InheritsFrom("TDirectory")) {
                TDirectory *subdir_source = static_cast<TDirectory*>(source->Get(key->GetName()));
                TDirectory *subdir_destination;
                if(isReference) {
                    subdir_destination = static_cast<TDirectory*>(destination->Get(subdir_source->GetName()));
                    if(!subdir_destination) {
                        std::cout << "Skipping reference directory '" << subdir_source->GetName()
                                  << "', which is missing in the origin file.";
                        continue;
                    }

                } else
                    subdir_destination = destination->mkdir(subdir_source->GetName());

                CopyDirectory(subdir_source, subdir_destination, isReference);
            } else if(destination->Get(key->GetName())) {

            } else if (cl->InheritsFrom("TTree")) {
                TTree *T = (TTree*)source->Get(key->GetName());
                TTree *newT = T->CloneTree();
                destination->WriteTObject(newT, key->GetName(), "WriteDelete");
                objectWritten = true;
            } else {
                std::unique_ptr<TObject> original_obj(key->ReadObj());
                std::unique_ptr<TObject> obj(original_obj->Clone());
                destination->WriteTObject(obj, key->GetName(), "WriteDelete");
                objectWritten = true;
            }

            if(objectWritten && isReference)
                std::cout << "Object '" << key->GetName() << "' taken from the reference file for '"
                          << destination->GetName()  << "'" << std::endl;
        }
        destination->SaveSelf(kTRUE);
    }

private:
    std::shared_ptr<TFile> originalFile, referenceFile, outputFile;
};