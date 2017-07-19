// @(#)root/io:$Id$
// Author: Philippe Canal, Witold Pokorski, and Guilherme Amadio

/*************************************************************************
 * Copyright (C) 1995-2017, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "ROOT/TBufferMerger.hxx"

#include "TBufferFile.h"
#include "TError.h"
#include "TFileMerger.h"
#include "TROOT.h"
#include "TVirtualMutex.h"

namespace ROOT {
namespace Experimental {

TBufferMerger::TBufferMerger(const char *name, Option_t *option, Int_t compress)
   : TBufferMerger(std::unique_ptr<TFile>(TFile::Open(name, option, /*title*/ name, compress)))
{
   if (!fOutputFile) {
      Error("OutputFile", "cannot open the MERGER output file %s", name);
   }
}

TBufferMerger::TBufferMerger(std::unique_ptr<TFile> output)
   : fOutputFile(output.release()), fMergingThread(new std::thread([&]() { this->WriteOutputFile(); }))
{
}

TBufferMerger::~TBufferMerger()
{
   for (auto f : fAttachedFiles)
      if (!f.expired()) Fatal("TBufferMerger", " TBufferMergerFiles must be destroyed before the server");

   this->Push(nullptr);
   fMergingThread->join();
}

std::shared_ptr<TBufferMergerFile> TBufferMerger::GetFile()
{
   R__LOCKGUARD(gROOTMutex);
   std::shared_ptr<TBufferMergerFile> f(new TBufferMergerFile(*this));
   gROOT->GetListOfFiles()->Remove(f.get());
   fAttachedFiles.push_back(f);
   return f;
}

void TBufferMerger::Push(TBufferFile *buffer)
{
   {
      std::lock_guard<std::mutex> lock(fQueueMutex);
      fQueue.push(buffer);
   }
   fDataAvailable.notify_one();
}

void TBufferMerger::WriteOutputFile()
{
   TDirectoryFile::TContext context;
   std::unique_ptr<TMemFile> memfile;
   std::unique_ptr<TBufferFile> buffer;
   TFileMerger merger;

   merger.ResetBit(kMustCleanup);

   {
      R__LOCKGUARD(gROOTMutex);
      merger.OutputFile(std::unique_ptr<TFile>(fOutputFile)); // Takes ownership.
   }

   while (true) {
      std::unique_lock<std::mutex> lock(fQueueMutex);
      fDataAvailable.wait(lock, [this]() { return !this->fQueue.empty(); });

      buffer.reset(fQueue.front());
      fQueue.pop();
      lock.unlock();

      if (!buffer) return;

      Long64_t length;
      buffer->SetReadMode();
      buffer->SetBufferOffset();
      buffer->ReadLong64(length);

      {
         TDirectory::TContext ctxt;
         {
            R__LOCKGUARD(gROOTMutex);
            memfile.reset(new TMemFile(fOutputFile->GetName(), buffer->Buffer() + buffer->Length(), length, "read"));
            buffer->SetBufferOffset(buffer->Length() + length);
            merger.AddFile(memfile.get(), false);
            merger.PartialMerge();
         }
         merger.Reset();
      }
   }
}

} // namespace Experimental
} // namespace ROOT
