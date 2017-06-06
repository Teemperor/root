// @(#)root/bonjour:$Id$
// Author: Fons Rademakers   29/05/2009

/*************************************************************************
 * Copyright (C) 1995-2009, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TBonjourResolver                                                     //
//                                                                      //
// This class consists of one main member function,                     //
// ResolveBonjourRecord(), that resolves the service to an actual       //
// IP address and port number. The rest of the class wraps the various  //
// bits of Bonjour service resolver. The static callback function       //
// is marked with the DNSSD_API macro to make sure that the callback    //
// has the correct calling convention on Windows.                       //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TBonjourResolver.h"
#include "TBonjourBrowser.h"
#include "TBonjourRecord.h"
#include "TSysEvtHandler.h"
#include "TSystem.h"
#include "TError.h"

#include <arpa/inet.h>


ClassImp(TBonjourResolver);

////////////////////////////////////////////////////////////////////////////////
/// Default ctor.

TBonjourResolver::TBonjourResolver() : fDNSRef(nullptr), fBonjourSocketHandler(nullptr), fPort(0)
{
}

////////////////////////////////////////////////////////////////////////////////
/// Cleanup.

TBonjourResolver::~TBonjourResolver()
{
   delete fBonjourSocketHandler;

   if (fDNSRef) {
      DNSServiceRefDeallocate(fDNSRef);
      fDNSRef = nullptr;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Resolve Bonjour service to IP address and port.
/// Returns -1 in case of error, 0 otherwise.

Int_t TBonjourResolver::ResolveBonjourRecord(const TBonjourRecord &record)
{
   if (fDNSRef) {
      Warning("ResolveBonjourRecord", "resolve already in process");
      return 0;
   }

   DNSServiceErrorType err = DNSServiceResolve(&fDNSRef, 0, 0,
                                               record.GetServiceName(),
                                               record.GetRegisteredType(),
                                               record.GetReplyDomain(),
                                               (DNSServiceResolveReply)BonjourResolveReply,
                                               this);
   if (err != kDNSServiceErr_NoError) {
      Error("ResolveBonjourRecord", "error in DNSServiceResolve (%d)", err);
      return -1;
   }

   Int_t sockfd = DNSServiceRefSockFD(fDNSRef);
   if (sockfd == -1) {
      Error("ResolveBonjourRecord", "invalide sockfd");
      return -1;
   }

   fBonjourSocketHandler = new TFileHandler(sockfd, TFileHandler::kRead);
   fBonjourSocketHandler->Connect("Notified()", "TBonjourResolver", this, "BonjourSocketReadyRead()");
   fBonjourSocketHandler->Add();

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Emit RecordResolved signal.

void TBonjourResolver::RecordResolved(const TInetAddress *hostInfo, Int_t port)
{
   Long_t args[2];
   args[0] = (Long_t) hostInfo;
   args[1] = port;

   Emit("RecordResolved(TInetAddress*,Int_t)", args);
}

////////////////////////////////////////////////////////////////////////////////
/// The Bonjour socket is ready for reading. Tell Bonjour to process the
/// information on the socket, this will invoke the BonjourResolveReply
/// callback. This is a private slot, used in ResolveBonjourRecord.

void TBonjourResolver::BonjourSocketReadyRead()
{
   // in case the resolver has already been deleted
   if (!fDNSRef) return;

   DNSServiceErrorType err = DNSServiceProcessResult(fDNSRef);
   if (err != kDNSServiceErr_NoError)
      Error("BonjourSocketReadyRead", "error in DNSServiceProcessResult");
}

////////////////////////////////////////////////////////////////////////////////
/// Static Bonjour resolver callback function.

void TBonjourResolver::BonjourResolveReply(DNSServiceRef,
                                           DNSServiceFlags, UInt_t,
                                           DNSServiceErrorType errorCode, const char *,
                                           const char *hostTarget, UShort_t port,
                                           UShort_t, const char *txtRecord,
                                           void *context)
{
   TBonjourResolver *resolver = static_cast<TBonjourResolver *>(context);
   if (errorCode != kDNSServiceErr_NoError) {
      ::Error("TBonjourResolver::BonjourResolveReply", "error in BonjourResolveReply");
      //resolver->Error(errorCode);
   } else {
      resolver->fPort = ntohs(port);
      resolver->fHostAddress = gSystem->GetHostByName(hostTarget);
      resolver->fTXTRecord = txtRecord;
      resolver->RecordResolved(&resolver->fHostAddress, resolver->fPort);
   }
}
